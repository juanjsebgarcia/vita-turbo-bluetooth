/*
 * btlink.c - bttGetLinkInfo: the audio link's codec and its figures, as another module can read them. See btlink.h and include/bluetoothturbo.h.
 *
 * Where it comes from: the Bluetooth code's own data, read without a hook, once its code is recognised (btt_bt_seg1: the same link table and
 * encoder the development port's readings use, offsets read in the firmware and the same on every build 3.60-3.74): the slot of the connection that
 * owns the A2DP stream, its stream-started bit, and the SBC encoder's channel mode and highest bitpool. Whether the stream is AAC comes from the
 * AAC stream hooks' state (btaac_hook.h); without the AAC module that state is never touched and every link is SBC.
 */
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/kernel/threadmgr.h>

#include "../include/bluetoothturbo.h"
#include "btaac_api.h"
#include "btaac_hook.h"
#include "btldac_api.h"
#include "btcdcfile.h"
#include "btfix.h"
#include "btlink.h"
#include "btlink_core.h"
#include "btcall.h"
#include "btsignal_core.h"
#include "klog.h"

extern const BtAacApi *volatile btaac_link_api;
extern const BtLdacApi *volatile btldac_link_api;

#define P_A2DP_CONN   0x36528u    /* in the Bluetooth code's data: the connection that owns the A2DP stream (an address in the link table) */
#define P_ENC         0x3652Cu    /* the SBC encoder's structure                                                                           */
#define P_FIRST_PKT   0x37AF4u    /* 1 from the stream start until the first packet has set the encoder up                                  */
#define E_CHMODE      0x14u
#define E_MAXBP       0x28u
#define C_CONN_WORD   0x3E10u     /* in a link slot: the connected-profile word (0x1 hands-free, 0x10000 headset)                          */
#define P_CONN0       0x104A8u    /* the link table: 7 slots of BTAAC_SLOT_SIZE                                                             */

static volatile uint32_t g_seq;
static BtLinkExtra g_extra;       /* the worker's own: written and read by the worker only */
static BttLinkInfo g_info;        /* the snapshot: written between the sequence word's two steps */

/* The radio link's two readings, taken by the worker (here, in btt_link_pass) every BTSIG_PERIOD_MS while the link is up and smoothed (btsignal_core.h). The call is the
 * Bluetooth code's own information call (btcall_signal); nothing is hooked. */
static BtSignal g_sig;
static int g_sig_tried, g_sig_found, g_sig_failed_logged;
static unsigned g_sig_level_logged;

static void signal_pass(BtLinkIn *in)
{
	uint64_t now = ksceKernelGetSystemTimeWide() / 1000u;

	if (btsig_want(&g_sig, in->connected, in->addr_lo, in->addr_hi, now)) {
		unsigned lq = 0;
		int rssi = 0, r;

		if (!g_sig_tried) {
			g_sig_tried = 1;
			g_sig_found = btcall_find_signal() == 0;
			if (!g_sig_found)
				BTT_KLOG("link signal: the Bluetooth code's information call was not found: no signal reading");
		}
		if (g_sig_found) {
			r = btcall_signal(in->addr_lo, in->addr_hi, &lq, &rssi);
			if (r >= 0) {
				btsig_feed(&g_sig, lq, rssi);
			} else if (!g_sig_failed_logged) {
				g_sig_failed_logged = 1;
				BTT_KLOG("link signal: the information call failed (0x%08x): no reading this time; not logged again", (unsigned)r);
			}
		}
	}
	btsig_out(&g_sig, &in->sig_lq, &in->sig_rssi, &in->sig_level);
	if (!in->connected) {
		g_sig_level_logged = BTT_LINK_SIGNAL_NONE;
	} else if (in->sig_level != g_sig_level_logged) {
		/* the word and the readings behind it, only when the word changes: the evidence for tuning the thresholds */
		g_sig_level_logged = in->sig_level;
		BTT_KLOG("link signal: %s (link quality %u, signal %d, %u readings, %u of them all zeros)", btsig_name(in->sig_level), in->sig_lq, in->sig_rssi,
		         (unsigned)g_sig.n_read, (unsigned)g_sig.n_zero);
	}
}

static void begin(void) { g_seq++; __atomic_thread_fence(__ATOMIC_RELEASE); }
static void end(void) { __atomic_thread_fence(__ATOMIC_RELEASE); g_seq++; }

static uint32_t rd(uintptr_t seg1, uint32_t off) { return *(const volatile uint32_t *)(seg1 + off); }

void btt_link_pass(int on, int latency)
{
	BtLinkIn in;
	BttLinkInfo k;
	uintptr_t seg1 = on ? btt_bt_seg1() : 0;

	memset(&in, 0, sizeof(in));
	memset(&g_extra, 0, sizeof(g_extra));
	in.on = on;
	in.latency = latency;
	in.recognised = seg1 != 0;
	if (seg1) {
		uintptr_t p = *(const volatile uintptr_t *)(seg1 + P_A2DP_CONN), t0 = seg1 + P_CONN0, t1 = t0 + BTAAC_SLOTS * BTAAC_SLOT_SIZE, q;
		int slot = 0;

		for (q = t0; q < t1; q += BTAAC_SLOT_SIZE)
			if (q == p)
				slot = 1;
		if (slot) {
			in.connected = 1;
			in.addr_lo = btaac_rd32(p, BTAAC_C_ADDR_LO);
			in.addr_hi = btaac_rd32(p, BTAAC_C_ADDR_HI) & 0xFFFFu;
			in.codec_mask = btt_codecs_lookup(in.addr_lo, in.addr_hi);   /* what the console learned when it last opened a stream to them */
			in.conn_word = btaac_rd32(p, C_CONN_WORD);
			in.streaming = (btaac_rd32(p, BTAAC_C_PROFILES) & BTAAC_PROF_STARTED) != 0;
			in.enc_valid = in.streaming && !rd(seg1, P_FIRST_PKT);
			g_extra.mac_lo = btaac_rd32(p, BTAAC_C_ADDR_LO);
			g_extra.mac_hi = btaac_rd32(p, BTAAC_C_ADDR_HI) & 0xFFFFu;
			g_extra.mac_ok = (g_extra.mac_lo | g_extra.mac_hi) != 0;
			g_extra.voice = ((btaac_rd32(p, BTAAC_C_WANT) | btaac_rd32(p, BTAAC_C_PARKED) | btaac_rd32(p, BTAAC_C_PROFILES)) & BTAAC_VOICE_BIT) != 0;
			in.sbc_chmode = rd(seg1, P_ENC + E_CHMODE);
			in.sbc_bitpool = rd(seg1, P_ENC + E_MAXBP);
			if (btaac_neg.state == BTAAC_N_CONFIGURED && btaac_neg.conn == p) {
				BtAacInfo ai;

				if (btaac_neg.ldac) {
					in.ldac_stream = 1;
					if (btldac_link_api) {
						BtLdacInfo li;

						btldac_link_api->get_info(&li);
						in.ldac_kbps = li.kbps;
					}
				} else if (btaac_neg.aptx) {
					in.aptx_stream = 1;
					in.aptx_hd = btaac_neg.apt_hd ? 1 : 0;
				} else {
					in.aac_stream = 1;
					in.aac_bps = btaac_neg.bps;
					g_extra.bps = btaac_neg.bps;
					g_extra.max_bps = btaac_neg.max_bps;
					if (btaac_link_api) {
						btaac_link_api->get_info(&ai);
						g_extra.aac_running = ai.state == BTAAC_S_RUNNING;
						in.aac_neon = (int)ai.neon_in_use && ai.state == BTAAC_S_RUNNING;
					}
				}
			}
		}
	}
	signal_pass(&in);
	btlink_compose(&in, &k);
	begin();
	g_info = k;
	end();
}

void btt_link_extra(BtLinkExtra *out)
{
	*out = g_extra;
}

void btt_link_read(BttLinkInfo *out)
{
	uint32_t s1, s2;

	/* a few tries: the writer is quick and runs at most ten times a second */
	for (int i = 0; i < 8; i++) {
		s1 = g_seq;
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		*out = g_info;
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		s2 = g_seq;
		if (s1 == s2 && !(s1 & 1))
			return;
	}
	memset(out, 0, sizeof(*out));
}

/* The call: the caller's size is read first, and the answer is copied out with the system's checked copies, never through the caller's pointer. */
int bttGetLinkInfo(BttLinkInfo *out)
{
	BttLinkInfo k;
	uint32_t size = 0, n;

	if (!out || ksceKernelMemcpyUserToKernel(&size, out, sizeof(size)) < 0 || size < BTT_LINK_SIZE_MIN)
		return BTT_ERR_ARG;
	btt_link_read(&k);
	k.version = BTT_LINK_VERSION;
	n = size < sizeof(k) ? size : (uint32_t)sizeof(k);
	k.size = n;
	if (ksceKernelMemcpyKernelToUser(out, &k, n) < 0)
		return BTT_ERR_ARG;
	return BTT_OK;
}
