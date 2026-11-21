/*
 * bthfp_mod.c - the headset battery code's module side: the hook on the Bluetooth code's headset line routine, the console's
 * service table with a hands-free gateway record, the console-started hands-free connection (and, when the console's own
 * headset-profile link is what is up, its close and swap), and the level the headset reports. btt_hfp_sync is called once per
 * worker pass with the mode in force; main.c gives it 4 while Bluetooth Turbo is on and 0 while it is off.
 *
 *   mode 0:  off. The hook, if it was ever put in, passes every line to the console; the service table is the stock one again;
 *            no connect is made, nothing is closed; the level is forgotten. Nothing at all is installed in a start that never
 *            had the mode on.
 *   mode 1:  the hook answers the hands-free handshake and the battery commands on the headset link as it is (bthfp.c).
 *   mode 3:  the answers, and the console's service table moved to a copy of this module's (the stock records byte for byte and a
 *            complete hands-free gateway record, bthfp_sdp.h), and, once a headset's sound link is up and it offers a hands-free
 *            channel, the connect the console would never make itself (bthfp_conn.h).
 *   mode 4:  mode 3, and when what is up is the console's own headset-profile link (its policy opens that first when the headset
 *            has a headset record) the worker closes it with the RFCOMM channel's own close and opens hands-free in its place.
 *            This is the mode the product runs.
 * Modes 1 and 3 are subsets of 4, kept so the mode stays one number that a Settings row can later choose from.
 *
 * The console's service records are one table in the Bluetooth code's text; the headset gateway record is the first, 50 bytes
 * from text 0x1F868. A headset keeps the records it read when it was paired, so a headset paired before the table was moved is
 * paired again once to learn the new record.
 *
 * The microphone (bthfp_mic.h): in modes 3 and 4 two text patches and a sweep of the device records give a hands-free headset the console's
 * microphone bit, and a voice request parked on the link starts the hands-free connect (bthfp_conn.h); all undone with the mode.
 *
 * Every change is made only on the stock bytes (the same bytes on all twelve firmware builds 3.60-3.74), read back after it is made and after it is taken out. Another plugin's change of the same bytes stops it and
 * nothing is written. Module stop undoes everything.
 *
 * bthfp_sdp.h and bthfp_conn.h are part of this file: included here, in this order, and nowhere else.
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/kernel/threadmgr.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "bthfp_hook.h"
#include "bthfp_mod.h"
#include "btbatt.h"
#include "btfix.h"
#include "klog.h"

int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr);

#define OFF_AT       0x118E4u    /* the routine that takes one received headset line */
#define OFF_AT_TAIL  0x118F0u    /* its code after the address loads */
#define OFF_REPLY    0x116E4u    /* the console's reply routine: the connection, a format, what it takes (sends the text to the headset) */

#define HFP_LOCK_PRIO     0x40      /* the worker's priority while it holds the Bluetooth code's lock */
#define HFP_WORKER_PRIO   0x70
#define HFP_AV_SEG1_BYTES 0x2478u   /* the audio policy's data segment: its size in every firmware 3.60-3.74 */
#define AV_DEVBITS        0x0248u   /* ... the audio device state                                              */
#define HFP_AV_BT_BITS    ((1u << 4) | (1u << 11))   /* its device bits: Bluetooth audio out, Bluetooth microphone */

/* push.w {r3-fp,lr}; movw r4,#imm; movt r4,#imm (the immediates are the loader's); then the code after them */
static const uint8_t k_at_head[12] = { 0x2D, 0xE9, 0xF8, 0x4F, 0x4F, 0xF2, 0xB4, 0x34, 0xC8, 0xF2, 0x01, 0x14 };
static const uint8_t k_at_mask[12] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFB, 0x00, 0x0F, 0xF0, 0xFB, 0x00, 0x0F };
static const uint8_t k_at_tail[28] = {
	0x82, 0x46, 0xA4, 0xF1, 0x38, 0x05, 0x88, 0x46, 0x93, 0x46, 0x05, 0xF1, 0x18, 0x09, 0x54, 0xF8, 0x38, 0x6C, 0x08, 0x21,
	0x94, 0xF8, 0x00, 0xF0, 0x0C, 0x44, 0x30, 0x46 };

/* The console's reply routine is recognised by a hash of its first 52 bytes (the same on all twelve firmware builds), not by a copy
 * of them: bytes_fnv of the window must be REPLY_FNV. Two address loads in it (a movw at +14, a movt at +22) hold numbers the
 * loader fills in, so only their opcode and register bits are hashed. */
#define REPLY_LEN  0x34u
static const uint8_t k_reply_at[] = { 14, 22 };
#define REPLY_FNV  0x4B3DFFAF3C45CF83ull

static SceUID g_at_uid = -1;
static SceUID g_at_modid = -1;             /* the Bluetooth code's module and its text, once found */
static uintptr_t g_at_base;
static int g_at_looked, g_at_failed;       /* tried once; a refusal is not retried until the module is loaded again */
static int g_hfp_reply_state;              /* 0 not looked at, 1 recognised (bthfp_reply set), 2 not recognised */

static int bytes_same(const volatile uint8_t *p, const uint8_t *b, const uint8_t *mask, int n)
{
	for (int i = 0; i < n; i++)
		if ((p[i] & (mask ? mask[i] : 0xFF)) != (b[i] & (mask ? mask[i] : 0xFF)))
			return 0;
	return 1;
}

/* The FNV-1a 64-bit hash of the n bytes at p. Each 4-byte group that starts at one of the nat offsets in `at` is a movw/movt whose
 * 16-bit immediate the loader fills in: of those bytes only the opcode and register bits count (the mask 0xF0 0xFB 0x00 0x0F). */
static uint64_t bytes_fnv(const volatile uint8_t *p, uint32_t n, const uint8_t *at, uint32_t nat)
{
	static const uint8_t load_mask[4] = { 0xF0, 0xFB, 0x00, 0x0F };
	uint64_t h = 0xCBF29CE484222325ull;

	for (uint32_t i = 0; i < n; i++) {
		uint8_t v = p[i];

		for (uint32_t j = 0; j < nat; j++)
			if (i >= at[j] && i < at[j] + 4u)
				v &= load_mask[i - at[j]];
		h = (h ^ v) * 0x100000001B3ull;
	}
	return h;
}


/* ---- the audio policy's data segment ------------------------------------------------------------ */

static uintptr_t g_av_seg1;
static int g_av_tried;

/* The policy's data segment, once, and only if both ends of it resolve; 0 otherwise. */
uintptr_t btt_hfp_av_seg1(void)
{
	tai_module_info_t ti;
	uintptr_t last = 0;

	if (g_av_tried)
		return g_av_seg1;
	g_av_tried = 1;
	memset(&ti, 0, sizeof(ti));
	ti.size = sizeof(ti);
	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceAVConfig", &ti) < 0 ||
	    module_get_offset(KERNEL_PID, ti.modid, 1, 0, &g_av_seg1) < 0 ||
	    module_get_offset(KERNEL_PID, ti.modid, 1, HFP_AV_SEG1_BYTES - 1, &last) < 0 || !g_av_seg1 || !last)
		g_av_seg1 = 0;
	return g_av_seg1;
}

/* ---- the hook --------------------------------------------------------------------------------- */

static int hook_install(void)
{
	tai_module_info_t ti;
	uintptr_t base = 0;
	SceUID uid;

	g_at_looked = 1;
	memset(&ti, 0, sizeof(ti));
	ti.size = sizeof(ti);
	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceBt", &ti) < 0 ||
	    module_get_offset(KERNEL_PID, ti.modid, 0, 0, &base) < 0 || !base) {
		BTT_KLOG("bluetooth hfp: Bluetooth code not found - no battery reading");
		return -1;
	}
	g_at_modid = ti.modid;
	g_at_base = base;
	if (!bytes_same((const volatile uint8_t *)base + OFF_AT, k_at_head, k_at_mask, sizeof(k_at_head)) ||
	    !bytes_same((const volatile uint8_t *)base + OFF_AT_TAIL, k_at_tail, 0, sizeof(k_at_tail))) {
		BTT_KLOG("bluetooth hfp: headset line code not recognised - no battery reading");
		return -1;
	}
	bthfp_ref = 0;
	uid = taiHookFunctionOffsetForKernel(KERNEL_PID, (tai_hook_ref_t *)&bthfp_ref, ti.modid, 0,
	                                     OFF_AT, 1, (const void *)bthfp_hook);
	if (uid < 0) {
		BTT_KLOG("bluetooth hfp: hook failed 0x%08x - no battery reading", (unsigned)uid);
		bthfp_ref = 0;
		return -1;
	}
	g_at_uid = uid;
	BTT_KLOG("bluetooth hfp: hook on (headset lines answered as stock until the mode is on)");
	/* The answering mode needs the console's reply routine: only on its stock bytes, and only called from now on. */
	if (bytes_fnv((const volatile uint8_t *)base + OFF_REPLY, REPLY_LEN, k_reply_at, sizeof(k_reply_at)) == REPLY_FNV) {
		bthfp_reply = (uintptr_t)base + OFF_REPLY + 1;       /* +1: Thumb code */
		g_hfp_reply_state = 1;
	} else {
		g_hfp_reply_state = 2;
		BTT_KLOG("bluetooth hfp: the console's reply routine is not recognised - no answers");
	}
	return 0;
}

/* The hook is in; tried once (a refusal is not retried until the module is loaded again). 1 = in. */
static int hook_ensure(void)
{
	if (g_at_uid < 0 && !g_at_looked && !g_at_failed && hook_install() < 0)
		g_at_failed = 1;
	return g_at_uid >= 0;
}

/* Module stop: the hook out (the only time it comes out). */
static void hook_release(void)
{
	bthfp_mode = 0;                 /* first: a call in flight takes the original's path from now on */
	if (g_at_uid >= 0) {
		taiHookReleaseForKernel(g_at_uid, bthfp_ref);
		g_at_uid = -1;
	}
	for (int i = 0; i < 100 && bthfp_in; i++)
		ksceKernelDelayThread(1000);
	bthfp_reply = 0;
	bthfp_ref = 0;
}

#include "bthfp_sdp.h"     /* the service table: the copy, its record, the 14 words */
#include "bthfp_conn.h"    /* the connect: the Bluetooth code's lock and routines, the headset's slot, the state machine */
#include "bthfp_mic.h"     /* the microphone: the two recompute patches and the sweep of the device records */

static SceUID g_tab_uid[HFP_NSITES];       /* the 14 injects of the table move (0 = none)                  */
static int g_tab_in, g_tab_failed, g_tab_looked;
static int g_hfp_mode;                    /* the mode in force: 0, 1, 2, 3 or 4                    */
static uint32_t g_hfp_seen_seq;           /* the report sequence word the worker has taken up to   */
static uint32_t g_hfp_last_level = 1000;  /* the level logged last (none yet)                      */

/* ---- the table move ---------------------------------------------------------------------- */

/* Takes every inject of the move out (the caller holds the Bluetooth lock, or the module is stopping and could not get it).
 * Returns how many words were released. */
static int hfp_tab_release_locked(void)
{
	int n = 0;

	for (int i = 0; i < HFP_NSITES; i++) {
		if (g_tab_uid[i] > 0) {
			taiInjectReleaseForKernel(g_tab_uid[i]);
			n++;
		}
		g_tab_uid[i] = 0;
	}
	g_tab_in = 0;
	return n;
}

/* The table back to the stock one: with the Bluetooth lock (so no routine is walking the copy), or, at module stop (`stop`) when
 * the lock cannot be had, without it: leaving the Bluetooth code pointing into memory that goes away is the worse of the two.
 * Returns 1 when nothing of the move is in (any more). */
static int hfp_tab_release(int stop)
{
	int locked, any = 0;

	for (int i = 0; i < HFP_NSITES; i++)
		any |= g_tab_uid[i] > 0;
	if (!any)
		return 1;
	locked = hfp_lock(!stop) == 0;
	if (!locked && !stop)
		return 0;                       /* the next pass tries again */
	hfp_tab_release_locked();
	if (locked)
		hfp_unlock(!stop);
	else
		ksceKernelDelayThread(50000);   /* a routine that was already walking the copy has long finished */
	if (sdp_tab_bytes(0) == 1)
		BTT_KLOG("bluetooth hfp: the service table is the stock one again (forget the headset and pair it again)");
	else
		BTT_KLOG("bluetooth hfp: the service table is not back to the stock words (state %d)", sdp_tab_bytes(0));
	return 1;
}

static void hfp_tab_fail(const char *why, int state)
{
	g_tab_failed = 1;
	BTT_KLOG("bluetooth hfp: service table not moved: %s (state %d)", why, state);
}

/* Moves the table to the copy: only on the stock words and the stock table, with the lock held, read back, undone if it did not
 * take. Never advertises a record nothing would answer (the caller has checked the hook). */
static void hfp_tab_apply(void)
{
	uint8_t want[4];
	uint32_t start = sdp_copy_start();
	int b, bad = 0, ours = 0;

	if (g_tab_in || g_tab_failed || g_at_modid < 0 || !g_at_base)
		return;
	g_tab_looked = 1;
	if (hfp_routines_check() != 1) {
		hfp_tab_fail("the Bluetooth lock is not recognised", g_rt_state);
		return;
	}
	b = sdp_tab_bytes(0);
	if (b != 1 || !sdp_stock_bytes_ok()) {
		hfp_tab_fail(b != 1 ? "the 14 words are not all the stock ones" : "the table is not the stock table", b);
		return;
	}
	memcpy(g_sdp_tab, (const void *)(g_at_base + SDP_STOCK_OFF), SDP_STOCK_LEN);   /* the console records, checked above */
	memcpy(g_sdp_tab + SDP_STOCK_LEN, k_sdp_hfp, SDP_REC_LEN);
	bad = sdp_tab_problem(g_sdp_tab, SDP_TAB_LEN);
	if (bad || thumb_modimm(SDP_TAB_LEN) < 0) {
		hfp_tab_fail("the copy is not a table", bad);
		return;
	}
	__atomic_thread_fence(__ATOMIC_SEQ_CST);      /* the copy is written before any word names it */
	if (hfp_lock(1) != 0) {
		hfp_tab_fail("the Bluetooth lock could not be taken", 0);
		return;
	}
	b = sdp_tab_bytes(0);
	if (b == 1 && sdp_stock_bytes_ok()) {
		for (int i = 0; i < HFP_NSITES; i++) {
			SceUID uid;

			site_bytes(&k_sites[i], start, start + SDP_TAB_LEN, SDP_TAB_LEN, want);
			uid = taiInjectDataForKernel(KERNEL_PID, g_at_modid, 0, k_sites[i].off, want, 4);
			if (uid <= 0) {
				bad = i + 1;
				break;
			}
			g_tab_uid[i] = uid;
		}
		if (!bad && sdp_tab_bytes(&ours) != 2)
			bad = 1000 + ours;
		if (bad)
			hfp_tab_release_locked();
	} else {
		bad = 200;                                   /* changed between the first look and the lock */
	}
	hfp_unlock(1);
	if (bad) {
		hfp_tab_fail(bad == 200 ? "the table changed while taking the lock" : bad >= 1000 ? "the words did not take - undone" : "a word was refused - undone", bad);
		return;
	}
	g_tab_in = 1;
	BTT_KLOG("bluetooth hfp: the service table is now a copy: the stock records and a complete hands-free gateway record (from the next service search; forget the headset and pair it again)");
}

/* The headset's link, from the audio policy's device bits: 1 up, 0 down, -1 not readable. */
static int hfp_link(void)
{
	uintptr_t b = btt_hfp_av_seg1();

	if (!b)
		return -1;
	return (*(const volatile uint32_t *)(b + AV_DEVBITS) & HFP_AV_BT_BITS) ? 1 : 0;
}

/* Takes up the newest report the hook published, if the worker has not yet. */
static void hfp_drain(uint64_t now)
{
	uint32_t s1 = __atomic_load_n(&bthfp_seq, __ATOMIC_ACQUIRE), s2, level, source, t_us;
	uint64_t arrived;

	if ((s1 & 1) || s1 == g_hfp_seen_seq)
		return;
	level = bthfp_level;
	source = bthfp_source;
	t_us = bthfp_t_us;
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	s2 = bthfp_seq;
	if (s2 != s1)
		return;                       /* being written: the next pass */
	g_hfp_seen_seq = s1;
	/* when it arrived: now, less the (wrapped) difference of the low clock words, good for 71 minutes */
	arrived = now - (uint64_t)(uint32_t)(ksceKernelGetSystemTimeLow() - t_us);
	btt_batt_report((int)level, source, arrived);
	btt_batt_link(1, now);
	if (level != g_hfp_last_level) {
		g_hfp_last_level = level;
		BTT_KLOG("bluetooth hfp: the headset reported %u %% (%s)", (unsigned)level,
		         source == BTT_BATT_SRC_HFIND ? "hands-free indicator" : "vendor command");
	}
}

/* Once per worker pass, after the start-up period: bring the mode in force to `mode`. */
void btt_hfp_sync(int mode)
{
	uint64_t now = ksceKernelGetSystemTimeWide();
	int hooked, link;

	if (mode < 0 || mode > 4)
		mode = 0;
	if (mode == 0) {
		if (g_hfp_mode || g_tab_in || bthfp_mode) {
			BTT_KLOG("bluetooth hfp: off");
			bthfp_mode = 0;
			hfp_tab_release(0);
			btt_batt_reset();
			g_hfp_last_level = 1000;
		}
		hfp_tab_release(0);             /* a release that could not take the lock last pass */
		hfp_mic_release(0);
		hfp_conn_reset(&g_hc);
		g_tab_failed = 0;
		g_mic_failed = 0;
		g_hfp_mode = 0;
		return;
	}
	hooked = hook_ensure() && g_hfp_reply_state == 1;
	if (mode != g_hfp_mode) {
		g_hfp_mode = mode;
		BTT_KLOG("bluetooth hfp: mode %d (%s)", mode, mode == 1 ? "answers on the headset link as it is" :
		         mode == 2 ? "answers, and the in-place hands-free record" :
		         mode == 3 ? "answers, the moved service table and the console-started hands-free connect" :
		         "answers, the moved service table, the connect, and the swap of the console's own headset link");
		if (mode < 3) {
			g_tab_failed = 0;
			g_mic_failed = 0;
		}
		hfp_conn_reset(&g_hc);
	}
	if (hooked && !bthfp_mode)
		bthfp_mode = 1;
	else if (!hooked)
		bthfp_mode = 0;                 /* the hook is not in, or the reply routine is not recognised: nothing is answered */
	if (mode < 3 || !hooked) {
		hfp_tab_release(0);
		hfp_mic_release(0);
	}
	if (mode >= 3 && hooked) {
		hfp_tab_apply();
		hfp_mic_sync(1);
		hfp_conn_poll(now / 1000, bthfp_mode != 0 && bthfp_reply != 0, mode == 4);
	} else {
		hfp_conn_reset(&g_hc);
	}
	hfp_drain(now);
	link = hfp_link();
	if (link >= 0)
		btt_batt_link(link, now);
}

/* Module stop (and the one place that undoes everything): answers off, table back, hook out, level forgotten. */
void btt_hfp_restore(void)
{
	bthfp_mode = 0;
	hfp_mic_release(1);
	hfp_tab_release(1);
	hfp_conn_reset(&g_hc);
	btt_batt_reset();
	g_hfp_mode = 0;
	hook_release();
}
