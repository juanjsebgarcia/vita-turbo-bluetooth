/*
 * btevlog.c - the link log's worker side. See btevlog.h; the hook-side routine is btevlog_hook.c and the logic btevlog_core.h.
 *
 * Each pass: put the event hook in once; take the records the hook noted and apply them (who opened a link, how it ended); write the lines the rules allow; read the profiles
 * each known link has open from the Bluetooth code's link slots (only to note them: nothing is written to the Bluetooth code's data). All of it runs in the worker thread, never
 * in the hook.
 *
 * The slot layout is the one the headset code already reads in every build 3.60-3.74 (bthfp_conn.h: the link table at +0x104A8 of the data segment, 7 slots of 0x41D0 bytes,
 * the ACL-up bit 0x100 in the word at +4, the address at +0x18/+0x1C, 16 channel objects of 0x268 bytes from +0x14B0, each with its PSM at +0x34). A slot whose address is not one of a
 * link the controller reported is not looked at, so a layout that did not hold would show as missing profiles, not as wrong lines.
 */
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysclib.h>

#include "../include/bluetoothturbo.h"
#include "btevlog.h"
#include "btevlog_hook.h"
#include "btflush.h"
#include "btfix.h"
#include "klog.h"

#define LS_CONN0     0x104A8u     /* the link table in the data segment                  */
#define LS_STRIDE    0x41D0u
#define LS_N         7u
#define LS_FLAGS     0x04u        /* bit 0x100: the ACL link is up                         */
#define LS_MAC_LO    0x18u
#define LS_MAC_HI    0x1Cu
#define LS_CH0       0x14B0u      /* the channel objects                                   */
#define LS_CH_STRIDE 0x268u
#define LS_CH_PSM    0x34u
#define LS_CH_FLAGS  0x04u        /* bit 2: set when the remote's connect request created the channel */
#define LS_CH_PROF   0x264u       /* RFCOMM: the profile tag                                */
#define LS_FAST_US   15000000u    /* a link this young is read often                         */

static BtelState el_st;
static BtelLim el_lim;
static uint32_t el_tail, el_lines, el_unknown_lines;
static int el_inited, el_was_on, el_fast;

static void el_mac_text(char *b, size_t n, uint32_t lo, uint32_t hi)
{
	/* shown the usual way, the first three bytes only: the vendor part; the rest is the device's own and stays out of the log */
	snprintf(b, n, "%02x:%02x:%02x:xx:xx:xx", (unsigned)((hi >> 8) & 0xFF), (unsigned)(hi & 0xFF), (unsigned)(lo >> 24));
}

static void el_reason_text(char *b, size_t n, uint32_t code)
{
	const char *w = btel_reason_name(code);

	if (*w)
		snprintf(b, n, "%s (0x%02x)", w, (unsigned)code);
	else
		snprintf(b, n, "error 0x%02x", (unsigned)code);
}

static const char *el_opener_text(uint32_t o)
{
	return o == BTEL_OPEN_VITA ? "the Vita" : o == BTEL_OPEN_HEADSET ? "the remote" : "unknown";
}

static void el_prof_text(char *b, size_t n, const uint8_t *p, unsigned np)
{
	size_t at = 0;

	b[0] = 0;
	for (unsigned i = 0; i < np; i++) {
		if (at + 16 >= n) {                 /* no room: the rest is left out, and says so */
			snprintf(b + at, n - at, ", ...");
			break;
		}
		at += (size_t)snprintf(b + at, n - at, "%s%s", i ? ", " : "", btel_prof_name(BTEL_PF_TOK(p[i])));
	}
	if (!np)
		snprintf(b, n, "none seen");
}

static void el_say(const char *line)
{
	el_lines++;
	btt_klog_line(line);
}

/* Write a line if the rules allow it. */
#define LINK_LOG(now_s, ...) do { if (btel_lim_allow(&el_lim, (now_s))) { char _l[256]; snprintf(_l, sizeof(_l), __VA_ARGS__); el_say(_l); } } while (0)

static void el_summary_line(uint32_t n, uint32_t now_s)
{
	char m[24], r[56];

	el_mac_text(m, sizeof(m), el_lim.sum_lo, el_lim.sum_hi);
	el_reason_text(r, sizeof(r), el_lim.sum_reason);
	LINK_LOG(now_s, "link log: %u more endings of %s like that, %s, opened by %s (last: link #%u after %u.%u s)", (unsigned)n, m, r, el_opener_text(el_lim.sum_opener),
	         (unsigned)el_lim.sum_id, (unsigned)(el_lim.sum_dur_ds / 10), (unsigned)(el_lim.sum_dur_ds % 10));
}

static void el_emit(const BtelOut *o, uint32_t now_s)
{
	char m[24], r[56], p[68];
	const BtelEnd *e = &o->e;
	uint32_t sum;

	el_mac_text(m, sizeof(m), e->lo, e->hi);
	switch (o->kind) {
	case BTEL_O_OPEN:
		if (!btel_open_quiet(&el_lim, e->lo, e->hi, now_s))
			LINK_LOG(now_s, "link #%u %s up: opened by %s", (unsigned)e->id, m, el_opener_text(e->opener));
		break;
	case BTEL_O_FAIL:
		el_reason_text(r, sizeof(r), e->status);
		LINK_LOG(now_s, "link attempt %s %s failed: %s", e->opener == BTEL_OPEN_HEADSET ? "from" : "to", m, r);
		break;
	case BTEL_O_END:
		el_reason_text(r, sizeof(r), e->reason);
		if (e->kind == BTEL_E_UNKNOWN) {
			if (el_unknown_lines >= BTEL_UNKNOWN_MAX)
				break;                /* voice links end every cycle: the few first say what they are, the rest would spend the budget */
			el_unknown_lines++;
			LINK_LOG(now_s, "link handle 0x%03x ended: %s (a voice link, or one that came up before the log started)", (unsigned)e->handle, r);
			break;
		}
		if (btel_collapse(&el_lim, e, now_s, &sum)) {
			break;
		}
		if (sum)
			el_summary_line(sum, now_s);
		el_prof_text(p, sizeof(p), e->prof, e->nprof);
		LINK_LOG(now_s, "link #%u %s ended after %u.%u s: %s; opened by %s; profiles: %s; %s, %s", (unsigned)e->id, m, (unsigned)(e->dur_ds / 10), (unsigned)(e->dur_ds % 10), r,
		         el_opener_text(e->opener), p, e->auth_bad ? "auth failed" : e->auth_ok ? "auth ok" : "auth not seen",
		         e->enc_off ? "encryption off" : e->enc_on ? "encrypted" : "enc not seen");
		break;
	case BTEL_O_AUTH_BAD:
		el_reason_text(r, sizeof(r), e->status);
		LINK_LOG(now_s, "link #%u %s: authentication failed: %s", (unsigned)e->id, m, r);
		break;
	case BTEL_O_ENC_OFF:
		el_reason_text(r, sizeof(r), e->status);
		LINK_LOG(now_s, "link #%u %s: encryption %s", (unsigned)e->id, m, e->status ? r : "turned off");
		break;
	}
}

static uint32_t el_rd32(uintptr_t a) { return *(const volatile uint32_t *)a; }
static uint32_t el_rd16(uintptr_t a) { return *(const volatile uint16_t *)a; }

/* The profiles each known link has open, in the order they are found (a channel that opens is noted once; one that closed is forgotten, so it is noted again if it reopens). */
static void el_scan_profiles(void)
{
	uintptr_t seg1 = btt_bt_seg1();

	if (!seg1)
		return;
	for (uint32_t i = 0; i < LS_N; i++) {
		uintptr_t c = seg1 + LS_CONN0 + (uintptr_t)i * LS_STRIDE;
		BtelLink *l;

		if (!(el_rd32(c + LS_FLAGS) & 0x100u))
			continue;
		l = btel_by_addr(&el_st, el_rd32(c + LS_MAC_LO), el_rd32(c + LS_MAC_HI));
		if (!l)
			continue;
		for (uint32_t k = 0; k < BTEL_CH; k++) {
			uintptr_t ch = c + LS_CH0 + (uintptr_t)k * LS_CH_STRIDE;
			uint32_t psm = el_rd16(ch + LS_CH_PSM);

			if (!psm) {
				l->ch_psm[k] = 0;
				continue;
			}
			if (l->ch_psm[k] == psm)
				continue;
			{
				unsigned av = 0, tok;

				for (uint32_t j = 0; j < BTEL_CH; j++)
					if (j != k && l->ch_psm[j] == 0x19)
						av++;
				tok = btel_psm_token(psm, psm == 3u ? el_rd32(ch + LS_CH_PROF) : 0u, av);
				l->ch_psm[k] = (uint16_t)psm;
				btel_add_profile(l, tok, (el_rd32(ch + LS_CH_FLAGS) & 2u) != 0);
			}
		}
	}
}

void btt_btevlog_pass(int on)
{
	uint64_t now = ksceKernelGetSystemTimeWide();
	uint32_t now_s = (uint32_t)(now / 1000000u), now_low = (uint32_t)now, lost0, n;
	BtelRec rec;
	BtelOut out;
	int hooked;

	if (!el_inited) {
		btel_init(&el_st);
		btel_lim_init(&el_lim);
		el_tail = btevlog_ring.head;   /* what was noted before the first pass is not ours to read (nothing is, the hook starts off) */
		el_inited = 1;
	}
	hooked = btt_btflush_evt_sync(on);
	if (on && hooked && !el_was_on) {
		memset(el_st.link, 0, sizeof(el_st.link));   /* started or switched on: links seen before are not known */
		memset(el_st.pend, 0, sizeof(el_st.pend));
		el_tail = btevlog_ring.head;
	}
	el_was_on = on && hooked;
	btevlog_on = (uint32_t)el_was_on;

	lost0 = el_st.lost;
	while (btel_take(&btevlog_ring, &el_tail, &rec, &el_st.lost)) {
		btel_apply(&el_st, &rec, btel_wide(now, now_low, rec.t), now_s, &out);
		el_emit(&out, now_s);
	}
	if (el_st.lost != lost0)
		LINK_LOG(now_s, "link log: %u event(s) were not read in time", (unsigned)(el_st.lost - lost0));
	el_scan_profiles();

	n = btel_collapse_idle(&el_lim, now_s, 0);
	if (n)
		el_summary_line(n, now_s);
	n = btel_lim_take_held(&el_lim, now_s);
	if (n && btel_lim_force(&el_lim)) {
		char l[96];

		snprintf(l, sizeof(l), "link log: %u more link line(s) were held back in the last minute", (unsigned)n);
		el_say(l);
	}
	if (btel_lim_take_capped(&el_lim))
		BTT_KLOG("link log: %u lines written; no more link lines until the next start", (unsigned)BTEL_BOOT_MAX);

	el_fast = 0;
	for (int i = 0; i < BTEL_ACTIVE; i++)
		if (el_st.link[i].used && now - el_st.link[i].t_open < LS_FAST_US)
			el_fast = 1;
}

int btt_btevlog_fast(void)
{
	return el_fast;
}

void btt_btevlog_restore(void)
{
	uint32_t n;

	btevlog_on = 0;
	el_was_on = 0;
	n = btel_collapse_idle(&el_lim, (uint32_t)(ksceKernelGetSystemTimeWide() / 1000000u), 1);
	if (n)
		el_summary_line(n, (uint32_t)(ksceKernelGetSystemTimeWide() / 1000000u));
}
