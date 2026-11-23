/*
 * btaac_stream.c - see btaac_stream.h. The hook bodies are btaac_hook.c (leaf-checked); this file installs them on bytes it has recognised, runs the
 * engine's session by what they saw, and ends it when the stream, the link, the console or the module goes. It runs in the module's worker thread
 * and in the standby handler's context: never in a hook.
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/suspend.h>
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "btaac_api.h"
#include "btaac_gate.h"
#include "btaac_hook.h"
#include "btaac_hook_fn.h"
#include "btaac_lat.h"
#include "btaac_stream.h"
#include "btaptx_api.h"
#include "btfall_codec.h"
#include "btaptx_link.h"
#include "btldac_api.h"
#include "btldac_link.h"
#include "bteq.h"
#include "btfix.h"
#include "btqual.h"
#include "klog.h"

int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr);
int ksceKernelUnregisterSysEventHandler(SceUID uid);   /* not in the SDK header; the stub library has it */

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);

extern const BtAacApi *volatile btaac_link_api;
extern const BtAptxApi *volatile btaptx_link_api;
extern const BtLdacApi *volatile btldac_link_api;

#define P_CONN0         0x104A8u    /* in the Bluetooth code's data: the link table (7 slots of BTAAC_SLOT_SIZE)  */
#define STOP_GRACE_MS   3000u       /* a stream not started for this long ends the session (a pause is shorter)  */
#define SESSION_BPS_KB  BTAAC_DEFAULT_KBPS

static SceUID g_h_disp = -1, g_h_send = -1, g_sev = -1;
static int g_installed, g_failed, g_gate, g_obs_failed;   /* g_obs_failed: the signalling hook alone (reading the codec lists) was refused */
static int g_gate_ok;   /* the Bluetooth code's bytes passed the gate, looked at before any hook of ours was in: never looked at again (see stream_install) */
static uint64_t g_started_ms;
static uint32_t g_seen_state = BTAAC_N_NONE, g_seen_discover;
static uint32_t g_n_start, g_n_stop, g_n_down, g_n_cancelled, g_n_ignored, g_n_broken, g_last_start_err;
static int g_ours;                      /* this module asked for the session now running or starting */
static int g_apt_run;                   /* an aptX session (the add-on's encoder marked as running) is ours */
static uint32_t g_n_apt_start, g_n_apt_stop, g_last_apt_err;
static int g_ldac_ours;                 /* this module asked for the LDAC session now running or starting */
static uint32_t g_n_ldac_start, g_n_ldac_stop, g_last_ldac_err;
static uint32_t g_ldac_mode = BTLDAC_MODE_330, g_lat_level;   /* the LDAC Bit Rate setting; the Bluetooth Latency setting */
static uint32_t g_aptx_hd = 1;   /* the aptX HD When Available setting (its default: on) */
static int g_src_only = 1;       /* the Music Source Only setting (its default: on) */
static int g_src_seen;           /* the state the worker last logged: 0 off (the start: nothing is said until it changes), 1 in force, 2 asked for but the signalling hook is not in */
static uint32_t g_src_seen_disc, g_src_seen_rej, g_src_seen_nore;
/* ---- a START the headphones reject, and a stream that never starts ----
 * The Bluetooth code sends the console's START of the audio stream the moment the stream record reaches 8. A headset that opened the stream itself may not be ready
 * 100 ms later and rejects it (error 0x31, bad state in the study's reading): the code puts the record back to 8, sets the retry byte (rec+0x21), and never asks again, and
 * its "starting" bit (conn+0x3E18, bit 3) stays set, so a second ksceBtStartAudio does nothing either. The stream then stays silent until the headphones reconnect.
 * The worker waits 0.5 s (a headset that starts the stream itself wins: the code accepts it), then clears that bit and calls ksceBtStartAudio again, which does everything
 * the first time did (the record to 9, a new label, START). A rejected retry is asked again after 1 s, then 2 s; after the third the headphones are marked "no AAC" (the
 * next connection is SBC). It is done for an AAC stream only: an SBC stream is never touched.
 * Without the Bluetooth code's lock: the bit is cleared with one atomic and, and the call takes the lock itself. A routine of the code that writes the word at the same time can
 * only put the bit back (the retry then does nothing and is counted), never lose its own change. (Taking the lock by pointer would need two more routines called by
 * their registers' meaning, read at the desk only: more unproven than the window it closes.) The call is made at a raised priority, as btreconn.c makes its own. */
#define LIB_BT_FOR_DRIVER   0xD48CA62Du     /* SceBtForDriver, the library the audio code imports the Bluetooth code's start from            */
#define NID_START_AUDIO     0x8D47CABDu     /* ksceBtStartAudio(address word 0, address word 1, flags)                                        */
#define START_FLAG_A2DP     8u
#define RETRY_MAX           3u
#define RETRY_LOCK_PRIO     0x40            /* the worker's priority while the Bluetooth code holds its lock for the call                    */
#define WATCHDOG_MS         3000u           /* a configured stream the Vita wants, not started for this long: marked                         */
static const uint32_t k_retry_wait_ms[RETRY_MAX] = { 500u, 1000u, 2000u };   /* from the START's rejection (seen) to the retry: the first is the grace   */

typedef int (*start_audio_t)(uint32_t addr_lo, uint32_t addr_hi, uint32_t flags);
static start_audio_t g_start_audio;
static struct {
	uintptr_t conn;                     /* the connection this is for                                                                    */
	uint32_t discover;                  /* and its negotiation (a new DISCOVER is a new connection)                                      */
	uint32_t tries;                     /* retries made for the START that is rejected                                                    */
	uint64_t stuck_ms, wd_ms;           /* when the rejected START / the never-started stream was first seen (0 = not now)               */
	int given_up, marked;               /* three retries failed (no more are made until the stream starts) / the device was marked "no AAC" on this connection */
} g_rt;
static uint32_t g_n_retry, g_n_retry_ok, g_n_retry_bans, g_n_watchdog, g_last_retry_ret;
/* The failure the codec fallback notice (btfall.c) is to tell about, kept until the worker takes it (one at a time: they are seconds apart). */
static struct { uint32_t codec, lo, hi; } g_fail;

/* The codec the connection `conn` was configured for, as a notice names it. */
static uint32_t fail_codec_of_neg(void)
{
	return btaac_neg.ldac ? BTFALL_C_LDAC : btaac_neg.aptx ? (btaac_neg.apt_hd ? BTFALL_C_APTX_HD : BTFALL_C_APTX) : BTFALL_C_AAC;
}

/* Records that the codec of `conn` failed and the headphones are (or, for the next connection, will be) on SBC. Not for aptX HD unless `sbc_now`: a refused HD stream
 * leaves the device its plain aptX (btaac_ban_codec), so "using SBC" would be untrue for the next connection. */
static void fail_note(uintptr_t conn, int sbc_now)
{
	uint32_t c = fail_codec_of_neg();

	if (c == BTFALL_C_APTX_HD && !sbc_now)
		return;
	g_fail.lo = btaac_rd32(conn, BTAAC_C_ADDR_LO);
	g_fail.hi = btaac_rd32(conn, BTAAC_C_ADDR_HI) & 0xFFFFu;
	__atomic_store_n(&g_fail.codec, c, __ATOMIC_RELEASE);
}

int btt_aac_stream_take_failure(uint32_t *lo, uint32_t *hi)
{
	uint32_t c = __atomic_exchange_n(&g_fail.codec, 0u, __ATOMIC_ACQ_REL);

	*lo = g_fail.lo;
	*hi = g_fail.hi;
	return (int)c;
}

static uint32_t g_kbps = SESSION_BPS_KB;   /* the bit rate asked for: the AAC Bit Rate setting, 256 until the worker has passed it on */
static uint32_t g_preroll = BTAAC_PREROLL_DEFAULT;   /* units that must be ready before the stream starts (btaac_pace.h) */

static void gates_down(void)
{
	btaac_neg.pcm_gate = 0;             /* first: the sender stops feeding, then the packets stop */
	btaac_neg.send_gate = 0;
}

/* Asks for the session to end, if one is running or starting, and waits (at most `ms`) until it has. */
static void stream_end(const BtAacApi *a, uint32_t ms)
{
	BtAacInfo i;

	gates_down();
	a->get_info(&i);
	if (i.state != BTAAC_S_RUNNING && i.state != BTAAC_S_STARTING)
		return;
	a->stop();
	g_n_stop++;
	g_ours = 0;
	for (uint32_t t = 0; t < ms; t++) {
		a->get_info(&i);
		if (i.state != BTAAC_S_RUNNING && i.state != BTAAC_S_STARTING)
			return;
		ksceKernelDelayThread(1000);
	}
}

/* Ends an aptX session if one is running: the gates down, then the add-on's record cleared (a file write: the worker's thread, or the standby handler). */
static void apt_end(void)
{
	gates_down();
	if (g_apt_run) {
		g_apt_run = 0;
		btt_aptx_session_stop();
		g_n_apt_stop++;
	}
	btaac_neg.apt_ready = 0;
}

/* Ends an LDAC session if one is ours or running: the gates down, then the add-on asked to stop (it clears its crash record in its own thread), waiting at most `ms`. */
static void ldac_end(uint32_t ms)
{
	gates_down();
	if (!btt_ldac_session_running())
		return;
	btt_ldac_session_stop();
	g_n_ldac_stop++;
	g_ldac_ours = 0;
	for (uint32_t t = 0; t < ms; t++) {
		if (!btt_ldac_session_running())
			return;
		ksceKernelDelayThread(1000);
	}
}

/* The standby and shutdown event: end the session before the console goes down, so the engine's crash record is clean. The power code sends many events for one
 * power-down (a few dozen, more while it retries), and the first ones can still be cancelled: 0x100 (interrupts masked; a handler may veto), then either 0x101
 * (cancelled: the console keeps running) or 0x102 (interrupts masked), then 0x20F down to 0x200 with interrupts on. 0x20F is the first event after the point of
 * no return and the first one a handler may wait in, so it is the one that counts as a standby and ends the session (reboot and power-off go the same way). A
 * cancelled attempt must leave the session alone; no other event does anything; and nothing here may wait or sleep for the others. `resume` is 0 going down. */
#define SEV_DOWN_COMMITTED  0x20F
#define SEV_DOWN_CANCELLED  0x101
static int stream_sysevent(int resume, int eventid, void *args, void *opt)
{
	const BtAacApi *a = btaac_link_api;

	if (resume)
		return 0;
	if (eventid == SEV_DOWN_CANCELLED) {
		g_n_cancelled++;
		return 0;
	}
	if (eventid != SEV_DOWN_COMMITTED || (!a && !btaptx_link_api && !btldac_link_api)) {
		g_n_ignored++;
		return 0;
	}
	g_n_down++;
	apt_end();
	ldac_end(100);
	if (a)
		stream_end(a, 100);
	return 0;
}

/* `observe`: only the signalling hook goes in (it reads the headphones' codec lists and changes nothing: no codec is offered). The rest is put in later, when a
 * codec is wanted, by a call with `observe` 0, which leaves a hook already in as it is. */
static int stream_install(int observe)
{
	tai_module_info_t ti;
	uintptr_t base = 0, seg1;
	int g;

	memset(&ti, 0, sizeof(ti));
	ti.size = sizeof(ti);
	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceBt", &ti) < 0 || module_get_offset(KERNEL_PID, ti.modid, 0, 0, &base) < 0 || !base) {
		BTT_KLOG("aac stream: Bluetooth code not found - SBC");
		return -1;
	}
	/* The gate compares every window, among them the first bytes of the routines this module hooks. Once a hook of ours is in, taiHEN's jump is in those bytes, so a second look
	 * (the reader's hook went in first, now a codec is wanted; or a retry after a hook that did not go in) would refuse our own work. The verdict of the first look, made
	 * before any hook of ours was in, is kept for the boot: the code does not change. A refusal is not kept (it is looked at again after off and on). */
	g = g_gate_ok ? 0 : btaac_gate((const volatile uint8_t *)base);
	g_gate = g;
	if (g > (int)BTAAC_NWIN) {
		unsigned h = (unsigned)g - 1u - BTAAC_NWIN;

		BTT_KLOG("aac stream: Bluetooth code not recognised (window %d) - SBC", g);
		BTT_KLOG("aac stream: window %d (hashed) at +0x%x: live %08x, expected %08x", g, (unsigned)k_hw_off[h], (unsigned)btaac_hwin_live((const volatile uint8_t *)base, h),
		         (unsigned)k_hw_hash[h]);
		return -1;
	}
	if (g) {
		const BtAacWin *k = &k_btaac_wins[g - 1];
		int d = btaac_win_diff((const volatile uint8_t *)base, (unsigned)g - 1);
		const volatile uint8_t *live = (const volatile uint8_t *)base + k->off + (uint32_t)(d > 0 ? d : 0);
		const uint8_t *want = k->want + (d > 0 ? d : 0);

		BTT_KLOG("aac stream: Bluetooth code not recognised (window %d) - SBC", g);
		BTT_KLOG("aac stream: window %d at text +0x%x differs at +%d: live %02x %02x %02x %02x, expected %02x %02x %02x %02x", g, (unsigned)k->off, d,
		         live[0], live[1], live[2], live[3], want[0], want[1], want[2], want[3]);
		return -1;
	}
	g_gate_ok = 1;
	seg1 = btt_bt_seg1();
	if (!seg1) {
		BTT_KLOG("aac stream: the Bluetooth code's link table is not readable - SBC");
		return -1;
	}
	btaac_neg.conn_lo = seg1 + P_CONN0;
	btaac_neg.conn_hi = btaac_neg.conn_lo + BTAAC_SLOTS * BTAAC_SLOT_SIZE;
	btaac_neg.l2_send = (base + BTAAC_OFF_L2) | 1u;     /* Thumb code */
	btaac_neg.l2_fmt = base + BTAAC_OFF_FMT;
	btaac_neg.l2_fmt_acc = base + BTAAC_OFF_FMT_ACC;
	btaac_neg.l2_fmt_rej = base + BTAAC_OFF_FMT_REJ;
	if (!observe && btt_btqual_hq_hook() < 0) {   /* the configuration routine's hook (shared with the connection quality settings), H2 */
		BTT_KLOG("aac stream: the configuration hook did not go in - SBC");
		return -1;
	}
	if (g_h_disp < 0) {   /* a hook that got in on an earlier try stays in (passing through while nothing is offered): not hooked twice */
		btaac_disp_ref = 0;
		g_h_disp = taiHookFunctionOffsetForKernel(KERNEL_PID, (tai_hook_ref_t *)&btaac_disp_ref, ti.modid, 0, BTAAC_OFF_DISP, 1,
		                                          (const void *)btaac_disp_hook);
		if (g_h_disp < 0) {
			BTT_KLOG("aac stream: signalling hook failed 0x%08x - SBC", (unsigned)g_h_disp);
			g_h_disp = -1;
			btaac_disp_ref = 0;
			return -1;
		}
	}
	if (observe) {
		BTT_KLOG("aac stream: the signalling hook is in: it reads the headphones' codec lists and, with Music Source Only on, leaves the console's speaker endpoint out of its answers; no codec is offered");
		return 0;
	}
	if (g_h_send < 0) {
		btaac_send_ref = 0;
		g_h_send = taiHookFunctionOffsetForKernel(KERNEL_PID, (tai_hook_ref_t *)&btaac_send_ref, ti.modid, 0, BTAAC_OFF_SEND, 1,
		                                          (const void *)btaac_send_hook);
		if (g_h_send < 0) {
			BTT_KLOG("aac stream: media hook failed 0x%08x - SBC", (unsigned)g_h_send);
			g_h_send = -1;
			btaac_send_ref = 0;
			return -1;
		}
	}
	if (g_sev < 0) {
		g_sev = ksceKernelRegisterSysEventHandler("BluetoothTurboAac", stream_sysevent, NULL);
		if (g_sev < 0) {
			BTT_KLOG("aac stream: standby handler failed 0x%08x (the session still ends with the stream)", (unsigned)g_sev);
			g_sev = -1;
		}
	}
	if (!g_start_audio) {
		uintptr_t f = 0;

		if (module_get_export_func(KERNEL_PID, "SceBt", LIB_BT_FOR_DRIVER, NID_START_AUDIO, &f) >= 0 && f)
			g_start_audio = (start_audio_t)f;
		else
			BTT_KLOG("aac stream: the Bluetooth code's start call was not found: a START the headphones reject is not asked for again");
	}
	BTT_KLOG("aac stream: hooks on: the codec the Bluetooth Codec setting names is offered to the next headphones that connect");
	return 0;
}

static const char *state_words(uint32_t s)
{
	switch (s) {
	case BTAAC_N_NONE:       return "SBC";
	case BTAAC_N_PROBING:    return "reading the headphones' endpoints";
	case BTAAC_N_PENDING:    return "AAC configuration sent";
	case BTAAC_N_CONFIGURED: return "AAC";
	case BTAAC_N_FALLBACK:   return "SBC (AAC rejected)";
	}
	return "AAC failed";
}

/* What the hooks did, into the log: they may not log themselves. */
static void log_negotiation(void)
{
	uint32_t s = btaac_neg.state, d = btaac_neg.n_discover;

	if (s == g_seen_state && d == g_seen_discover)
		return;
	if (d != g_seen_discover && s == BTAAC_N_PROBING)
		BTT_KLOG("aac stream: new connection: reading the headphones' endpoints");
	if (d != g_seen_discover && btaac_neg.init == BTAAC_INIT_HEADSET)
		BTT_KLOG("aac stream: the headphones opened the stream: %s", (btaac_neg.acc_flags & BTAAC_ACC_ADVERTISED) ? "AAC offered (endpoint 3)" : "SBC only");
	g_seen_discover = d;
	if (s == g_seen_state)
		return;
	g_seen_state = s;
	switch (s) {
	case BTAAC_N_NONE:
		BTT_KLOG("aac stream: this connection stays SBC (%s; declined %u)", btaac_neg.have ? "an AAC endpoint was found but not used" : "the headphones offer no AAC endpoint we can use",
		         (unsigned)btaac_neg.n_declined);
		break;
	case BTAAC_N_PENDING:
		if (btaac_neg.ldac) {
			BTT_KLOG("aac stream: LDAC configuration sent: endpoint %u, 48 kHz stereo", (unsigned)(btaac_neg.seid >> 2));
			break;
		}
		if (btaac_neg.aptx) {
			BTT_KLOG("aac stream: aptX%s configuration sent: endpoint %u, 48 kHz stereo, %u kb/s%s", btaac_neg.apt_hd ? " HD" : "", (unsigned)(btaac_neg.seid >> 2),
			         btaac_neg.apt_hd ? (unsigned)BTAPTX_HD_KBPS : (unsigned)BTAPTX_KBPS, btaac_neg.apt_hd ? " (experimental)" : "");
			break;
		}
		BTT_KLOG("aac stream: AAC configuration sent: endpoint %u, %s LC, 48 kHz stereo, %u b/s (the headphones' highest %u)", (unsigned)(btaac_neg.seid >> 2),
		         btaac_neg.object == BTAAC_OBJ_MPEG2_LC ? "MPEG-2" : "MPEG-4", (unsigned)btaac_neg.bps, (unsigned)btaac_neg.max_bps);
		break;
	case BTAAC_N_CONFIGURED:
		if (btaac_neg.ldac)
			BTT_KLOG("aac stream: the headphones %s: LDAC, 48 kHz stereo", btaac_neg.acceptor ? "configured endpoint 5" : "accepted LDAC");
		else if (btaac_neg.aptx)
			BTT_KLOG("aac stream: the headphones %s: aptX%s, 48 kHz stereo, %u kb/s", btaac_neg.acceptor ? "configured endpoint 4" : "accepted aptX", btaac_neg.apt_hd ? " HD" : "",
			         btaac_neg.apt_hd ? (unsigned)BTAPTX_HD_KBPS : (unsigned)BTAPTX_KBPS);
		else if (btaac_neg.acceptor)
			BTT_KLOG("aac stream: the headphones configured endpoint 3: AAC, %s LC, %u b/s (theirs %u)", btaac_neg.object == BTAAC_OBJ_MPEG2_LC ? "MPEG-2" : "MPEG-4",
			         (unsigned)btaac_neg.bps, (unsigned)btaac_neg.max_bps);
		else
			BTT_KLOG("aac stream: the headphones accepted AAC");
		break;
	case BTAAC_N_OFFERED:
		break;
	case BTAAC_N_FALLBACK:
		if (btaac_conn_ok(btaac_neg.conn))
			fail_note(btaac_neg.conn, 1);   /* this connection is SBC now (the configuration was sent again) */
		BTT_KLOG("aac stream: the headphones rejected AAC: the SBC configuration was sent again (%u sent, %u not possible); no AAC for them until the next boot",
		         (unsigned)btaac_neg.n_resend, (unsigned)btaac_neg.n_resend_none);
		break;
	default:
		BTT_KLOG("aac stream: %s", state_words(s));
	}
}

/* The engine or an add-on could not start a session (or its session ended) after the headphones were configured for the codec: nothing but its packets may be sent on this stream, so
 * there is no sound until the headphones reconnect. The device is marked so that the reconnect does not negotiate the codec again and fall silent again, but the mark is the module's
 * own (btaac_local_for): it is not the headphones' refusal, so it raises no codec notice ("couldn't use") and the apply's decision does not read it as a refusal either (it reads it as
 * the codec not being on offer, like the hooks being out). The install and the gate never mark. */
static void local_note(const char *codec, const char *why, int err)
{
	BTT_KLOG("aac stream: not offered this start: the %s add-on could not start a session (%s, %d); a mark of the module's own, not a refusal by the headphones", codec, why, err);
}

static void mark_broken(const BtAacApi *a, uintptr_t conn, const char *why, int err)
{
	stream_end(a, 50);
	btaac_local_for(conn, BTAAC_BAN_AAC);
	__atomic_store_n(&btaac_neg.state, (uint32_t)BTAAC_N_BROKEN, __ATOMIC_RELEASE);
	g_n_broken++;
	BTT_KLOG("aac stream: AAC failed (%s, %d): no sound on this connection until the headphones reconnect; SBC from then on", why, err);
	local_note("AAC", why, err);
}

/* The device of this connection is not offered AAC again this boot; once for a connection. */
static void rt_mark(uintptr_t conn)
{
	if (!g_rt.marked) {
		g_rt.marked = 1;
		btaac_ban_codec(conn);
		fail_note(conn, 0);
	}
}

static void rt_reset(uintptr_t conn)
{
	memset(&g_rt, 0, sizeof(g_rt));
	g_rt.conn = conn;
	g_rt.discover = btaac_neg.n_discover;
}

/* Asks the Bluetooth code to send the START again (see the block comment above). */
static void rt_retry(uintptr_t conn)
{
	uint32_t lo = btaac_rd32(conn, BTAAC_C_ADDR_LO), hi = btaac_rd32(conn, BTAAC_C_ADDR_HI), rec;
	int prio = ksceKernelGetThreadCurrentPriority(), r;

	g_rt.tries++;
	g_rt.stuck_ms = 0;
	g_n_retry++;
	ksceKernelChangeThreadPriority(0, RETRY_LOCK_PRIO);
	__atomic_fetch_and((volatile uint32_t *)(conn + BTAAC_C_WANT), ~(uint32_t)BTAAC_STARTING_BIT, __ATOMIC_ACQ_REL);
	r = g_start_audio(lo, hi, START_FLAG_A2DP);
	ksceKernelChangeThreadPriority(0, prio);
	g_last_retry_ret = (uint32_t)r;
	rec = btaac_rd32(conn, BTAAC_C_REC_B);
	BTT_KLOG("aac stream: asked for the START again (attempt %u of %u): the call returned 0x%08x, the stream record is at %u (%s)", (unsigned)g_rt.tries, (unsigned)RETRY_MAX,
	         (unsigned)r, (unsigned)rec, rec == BTAAC_REC_STARTING ? "START sent" : "nothing sent");
}

/* One pass over a connection that is configured as AAC and is being offered it (`on`): the rejected START's retry, and the watchdog for a stream that never starts. */
static void start_watch(uintptr_t conn, int on, uint64_t now)
{
	uint32_t rec, w18, w24, prof, voice;
	int retrying, stuck;

	if (!on) {
		if (g_rt.conn)
			rt_reset(0);
		return;
	}
	rec = btaac_rd32(conn, BTAAC_C_REC_B);
	if (conn != g_rt.conn || btaac_neg.n_discover != g_rt.discover || rec < 5u) {   /* another connection, a new negotiation, or the stream is gone */
		rt_reset(conn);
		if (rec < 5u)
			return;
	}
	w18 = btaac_rd32(conn, BTAAC_C_WANT);
	w24 = btaac_rd32(conn, BTAAC_C_PARKED);
	prof = btaac_rd32(conn, BTAAC_C_PROFILES);
	if (prof & BTAAC_PROF_STARTED) {                 /* started (by our retry, or by the headphones): nothing to watch */
		if (g_rt.tries && !g_rt.given_up) {
			g_n_retry_ok++;
			BTT_KLOG("aac stream: the stream started after %u retry(ies) of the START", (unsigned)g_rt.tries);
		}
		g_rt.tries = 0;
		g_rt.stuck_ms = g_rt.wd_ms = 0;
		g_rt.given_up = 0;
		return;
	}
	voice = (w18 | w24 | prof) & BTAAC_VOICE_BIT;    /* the hands-free voice link has the link: no sound is wanted from the stream */
	/* the rejected START: the record back at 8 with the retry byte set, the console still wanting the sound (the "starting" bit, which the code leaves set after a reject) */
	stuck = rec == BTAAC_REC_READY && *(const volatile uint8_t *)(conn + BTAAC_C_REC_B + BTAAC_R_RETRY) == 1u && !voice && (w18 & BTAAC_STARTING_BIT);
	retrying = stuck && g_start_audio && !g_rt.given_up;
	if (!stuck && g_rt.tries && !g_rt.given_up && rec != BTAAC_REC_STARTING)   /* the console stopped wanting the sound (or the call sent nothing and left the bit clear): the next rejected START begins afresh */
		g_rt.tries = 0;
	if (stuck) {
		if (!g_rt.stuck_ms) {
			g_rt.stuck_ms = now;
			if (g_rt.tries < RETRY_MAX)
				BTT_KLOG("aac stream: the headphones rejected the START (stream record %u, retry %u of %u): %s", (unsigned)rec, (unsigned)g_rt.tries + 1u, (unsigned)RETRY_MAX,
				         g_start_audio ? "waiting for them to start it themselves, then asking again" : "the Bluetooth code's start call is not available: not asking again");
		}
		if (g_rt.tries >= RETRY_MAX) {
			if (!g_rt.given_up) {
				g_rt.given_up = 1;
				g_n_retry_bans++;
				rt_mark(conn);
				BTT_KLOG("aac stream: the START was rejected %u times: no sound on this connection until the headphones reconnect; SBC from then on", (unsigned)RETRY_MAX + 1u);
			}
		} else if (g_start_audio && now - g_rt.stuck_ms >= k_retry_wait_ms[g_rt.tries])
			rt_retry(conn);
	} else
		g_rt.stuck_ms = 0;
	/* the watchdog: the stream is configured and in some state before streaming, the console wants the sound, and nothing has started it for WATCHDOG_MS. Not while the retry is
	 * at work on it (that takes longer than this). Nothing is written to the Bluetooth code: the device is marked so that its next connection is SBC. */
	if (rec >= 5u && rec <= BTAAC_REC_STARTING && ((w18 | w24) & BTAAC_STARTING_BIT) && !voice && !retrying) {
		if (!g_rt.wd_ms)
			g_rt.wd_ms = now;
		else if (now - g_rt.wd_ms > WATCHDOG_MS && !g_rt.marked) {
			g_n_watchdog++;
			rt_mark(conn);
			BTT_KLOG("aac stream: configured but never started (stream record %u after %u ms, the console wants the sound): no sound on this connection until the headphones reconnect; SBC from then on",
			         (unsigned)rec, (unsigned)(now - g_rt.wd_ms));
		}
	} else
		g_rt.wd_ms = 0;
}

static void stream_pass(const BtAacApi *a, int accel)
{
	BtAacInfo i;
	uintptr_t conn = btaac_neg.conn;
	uint64_t now = ksceKernelGetSystemTimeWide() / 1000u;
	int configured = btaac_neg.state == BTAAC_N_CONFIGURED && btaac_conn_ok(conn);
	int alive = configured && btaac_rd32(conn, BTAAC_C_REC_B) == BTAAC_REC_OPEN;
	int started = alive && (btaac_rd32(conn, BTAAC_C_PROFILES) & BTAAC_PROF_STARTED);

	a->get_info(&i);
	start_watch(conn, configured && btaac_neg.enable, now);
	if (started)
		g_started_ms = now;
	if (!alive) {
		/* not an AAC stream (any more): nothing may feed or be sent, and a session this module made ends */
		if (btaac_neg.send_gate || btaac_neg.pcm_gate || (g_ours && (i.state == BTAAC_S_RUNNING || i.state == BTAAC_S_STARTING))) {
			stream_end(a, 50);
			BTT_KLOG("aac stream: the AAC stream is gone: session ended (%u units made, %u sent)", (unsigned)i.aus_out, (unsigned)btaac_neg.n_sent);
		}
		if (i.state != BTAAC_S_RUNNING && i.state != BTAAC_S_STARTING)
			g_ours = 0;
		return;
	}
	if (i.state == BTAAC_S_FAILED && g_ours) {
		g_ours = 0;
		mark_broken(a, conn, "the engine ended its session", (int)i.last_error);
		return;
	}
	if (started && (i.state == BTAAC_S_IDLE || i.state == BTAAC_S_FAILED)) {   /* (a failure of an earlier connection's session does not count against this one) */
		uint32_t kbps = btaac_neg.bps / 1000u, max_kbps = btaac_neg.max_bps / 1000u;
		int r = a->start(kbps ? kbps : SESSION_BPS_KB, 48000u, 2u, max_kbps, accel, btaac_media_mtu(conn));

		if (r == BTAAC_OK) {
			g_ours = 1;
			g_n_start++;
		} else if (r != BTAAC_ERR_BUSY) {
			g_last_start_err = (uint32_t)(-r);
			mark_broken(a, conn, "the engine would not start a session", r);
		}
		return;
	}
	if (i.state == BTAAC_S_RUNNING) {
		if (started) {
			if (!btaac_neg.send_gate) {
				/* a new run of the stream: the pacing and the clocks start afresh (the gates are down, so no hook is using them) */
				btaac_pace_reset((BtAacPace *)&btaac_neg.pace);
				btaac_neg.pcm_gate = 1;         /* the blocks first, then the packets */
				btaac_neg.send_gate = 1;
				BTT_KLOG("aac stream: session running (%u kb/s, %s build, pre-roll %u unit(s), engine thread priority 0x%x): AAC is playing", (unsigned)(i.bps / 1000u),
				         i.neon_in_use ? "NEON" : "C", (unsigned)btaac_neg.preroll, (unsigned)i.prio);
			}
		} else if (now - g_started_ms > STOP_GRACE_MS) {
			stream_end(a, 50);
			BTT_KLOG("aac stream: the stream has been stopped for %u s: session ended (%u units made, %u sent)", (unsigned)(STOP_GRACE_MS / 1000u),
			         (unsigned)i.aus_out, (unsigned)btaac_neg.n_sent);
		}
	}
}

/* aptX broke after the headphones were configured for it (the add-on would not start a session): the same rule as AAC's, no sound until they reconnect. */
static void mark_broken_aptx(uintptr_t conn, const char *why, int err)
{
	apt_end();
	btaac_local_codec(conn);
	__atomic_store_n(&btaac_neg.state, (uint32_t)BTAAC_N_BROKEN, __ATOMIC_RELEASE);
	g_n_broken++;
	BTT_KLOG("aac stream: aptX failed (%s, %d): no sound on this connection until the headphones reconnect; SBC from then on", why, err);
	local_note(btaac_neg.apt_hd ? "aptX HD" : "aptX", why, err);
}

/* One pass over a connection that is configured as aptX: the session (the encoder's reset and the crash record) starts when the stream starts and ends when it has been
 * stopped for 3 s, the link goes or the console goes down; the gates (the sender's blocks first, then the packets) follow it. There is no engine to wait for: the encoder is
 * called inline by the media hook. */
static void stream_pass_aptx(void)
{
	uintptr_t conn = btaac_neg.conn;
	uint64_t now = ksceKernelGetSystemTimeWide() / 1000u;
	int configured = btaac_neg.state == BTAAC_N_CONFIGURED && btaac_conn_ok(conn);
	int alive = configured && btaac_rd32(conn, BTAAC_C_REC_B) == BTAAC_REC_OPEN;
	int started = alive && (btaac_rd32(conn, BTAAC_C_PROFILES) & BTAAC_PROF_STARTED);

	start_watch(conn, configured && btaac_neg.enable, now);
	if (started)
		g_started_ms = now;
	if (!alive) {
		if (btaac_neg.send_gate || btaac_neg.pcm_gate || g_apt_run) {
			apt_end();
			BTT_KLOG("aac stream: the aptX stream is gone: session ended (%u blocks sent)", (unsigned)btaac_neg.n_sent);
		}
		return;
	}
	if (started) {
		if (!btaac_neg.send_gate) {
			int tone = 0;
			if (!tone && !g_apt_run) {
				int r = btt_aptx_session_start(btaac_neg.apt_hd ? 1 : 0);

				if (r != BTAPTX_OK) {
					g_last_apt_err = (uint32_t)(-r);
					mark_broken_aptx(conn, "the add-on would not start a session", r);
					return;
				}
				g_apt_run = 1;
				g_n_apt_start++;
			}
			btaac_neg.apt_ready = 0;
			btaac_neg.pcm_gate = 1;
			btaac_neg.send_gate = 1;
			BTT_KLOG("aac stream: session running: aptX%s is playing%s", btaac_neg.apt_hd ? " HD" : "", tone ? " (a recorded tone, no encoder)" : "");
		}
	} else if (btaac_neg.send_gate && now - g_started_ms > STOP_GRACE_MS) {
		apt_end();
		BTT_KLOG("aac stream: the stream has been stopped for %u s: aptX session ended (%u blocks sent)", (unsigned)(STOP_GRACE_MS / 1000u), (unsigned)btaac_neg.n_sent);
	}
}

/* LDAC broke after the headphones were configured for it: the same rule as AAC's, no sound until they reconnect, and the device is marked so that its next connection is SBC.
 * `headphones` is 1 for the headphones' own limit (their packet size is below what LDAC needs): that is their doing and marks them as a refusal; the add-on not starting or ending a
 * session is the module's own (see local_note). */
static void mark_broken_ldac(uintptr_t conn, const char *why, int err, int headphones)
{
	ldac_end(50);
	if (headphones)
		btaac_ban_codec(conn);
	else
		btaac_local_codec(conn);
	__atomic_store_n(&btaac_neg.state, (uint32_t)BTAAC_N_BROKEN, __ATOMIC_RELEASE);
	g_n_broken++;
	BTT_KLOG("aac stream: LDAC failed (%s, %d): no sound on this connection until the headphones reconnect; SBC from then on", why, err);
	if (!headphones)
		local_note("LDAC", why, err);
}

/* One pass over a connection that is configured as LDAC (stream_pass's counterpart: the engine is the add-on's, reached through the table). */
static void stream_pass_ldac(void)
{
	uintptr_t conn = btaac_neg.conn;
	uint64_t now = ksceKernelGetSystemTimeWide() / 1000u;
	int configured = btaac_neg.state == BTAAC_N_CONFIGURED && btaac_conn_ok(conn);
	int alive = configured && btaac_rd32(conn, BTAAC_C_REC_B) == BTAAC_REC_OPEN;
	int started = alive && (btaac_rd32(conn, BTAAC_C_PROFILES) & BTAAC_PROF_STARTED);
	int st = btt_ldac_state();

	start_watch(conn, configured && btaac_neg.enable, now);
	if (started)
		g_started_ms = now;
	if (!alive) {
		if (btaac_neg.send_gate || btaac_neg.pcm_gate || (g_ldac_ours && (st == BTLDAC_S_RUNNING || st == BTLDAC_S_STARTING))) {
			ldac_end(50);
			BTT_KLOG("aac stream: the LDAC stream is gone: session ended (%u packets sent)", (unsigned)btaac_neg.n_ldac_pkts);
		}
		if (!btt_ldac_session_running())
			g_ldac_ours = 0;
		return;
	}
	if (st == BTLDAC_S_FAILED && g_ldac_ours) {
		g_ldac_ours = 0;
		mark_broken_ldac(conn, "the engine ended its session", btt_ldac_last_error(), 0);
		return;
	}
	if (started && (st == BTLDAC_S_IDLE || st == BTLDAC_S_FAILED)) {   /* (a failure of an earlier connection's session does not count against this one) */
		uint32_t mtu = btaac_media_mtu(conn);
		int r = btt_ldac_session_start(g_ldac_mode, mtu);

		if (r == BTLDAC_OK) {
			g_ldac_ours = 1;
			g_n_ldac_start++;
		} else if (r == BTLDAC_ERR_MTU) {
			g_last_ldac_err = (uint32_t)(-r);
			btaac_neg.n_ldac_mtu++;
			mark_broken_ldac(conn, "the headphones' packet limit is below the 679 bytes LDAC needs", (int)mtu, 1);
		} else if (r != BTLDAC_ERR_BUSY) {
			g_last_ldac_err = (uint32_t)(-r);
			mark_broken_ldac(conn, "the engine would not start a session", r, 0);
		}
		return;
	}
	if (st == BTLDAC_S_RUNNING) {
		if (started) {
			if (!btaac_neg.send_gate) {
				/* a new run of the stream: the clocks start afresh (the gates are down, so no hook is using them) */
				btaac_neg.pcm_gate = 1;         /* the blocks first, then the packets */
				btaac_neg.send_gate = 1;
				BTT_KLOG("aac stream: session running (LDAC, setting %u, media channel limit %u): LDAC is playing", (unsigned)g_ldac_mode, (unsigned)btaac_media_mtu(conn));
			}
		} else if (now - g_started_ms > STOP_GRACE_MS) {
			ldac_end(50);
			BTT_KLOG("aac stream: the stream has been stopped for %u s: LDAC session ended (%u packets sent)", (unsigned)(STOP_GRACE_MS / 1000u), (unsigned)btaac_neg.n_ldac_pkts);
		}
	}
}

static void log_source_only(int on);

void btt_aac_stream_pass(int on, int codec, int accel)
{
	const BtAacApi *a = btaac_link_api;
	int cx = on ? btt_codec_effective(codec) : BTT_CODEC_SBC;
	int want = cx != BTT_CODEC_SBC;   /* (the effective codec is not SBC only when its module is there and usable)*/
	int hooks = want;                 /* the hooks are wanted: a codec is to be sent */
	if (!hooks)
		g_failed = 0;                      /* a refused install may be tried again after off and on */
	if (!on)
		g_obs_failed = 0;
	if (hooks && !g_installed && !g_failed) {
		if (stream_install(0) < 0)
			g_failed = 1;
		else
			g_installed = 1;
	} else if (on && !hooks && g_h_disp < 0 && !g_obs_failed) {   /* no codec to send: the signalling hook alone goes in, to read what the headphones offer */
		if (stream_install(1) < 0)
			g_obs_failed = 1;
	}
	btaac_neg.src_only = on && g_src_only && g_h_disp >= 0;   /* the hook is in whenever Bluetooth Turbo is on (and the code is recognised) */
	log_source_only(on);
	btaac_cdc.on = on && g_h_disp >= 0;    /* off with the master switch: the hook stays in and passes straight through */
	btaac_neg.kbps = g_kbps;
	btaac_neg.preroll = g_preroll;
	btaac_neg.want_aptx = cx == BTT_CODEC_APTX;
	btaac_neg.want_ldac = cx == BTT_CODEC_LDAC;
	btaac_neg.ldac_keep = btldac_keep(g_lat_level, g_ldac_mode);
	btaac_neg.want_aptx_hd = btaac_neg.want_aptx && g_aptx_hd && !btaac_neg.hd_off && btt_aptx_hd_usable();   /* HD is preferred to classic aptX when the headphones offer it, unless aptX HD When Available is off */
	btaac_neg.enable = want && g_installed && btt_bteq_hook_in();   /* not offered while the sender's hook is out: the engine would get no sound */
	if (!g_installed)
		return;
	log_negotiation();
	if (btaac_neg.ldac) {   /* this connection is LDAC: an AAC or aptX session of an earlier connection, if one is still running, ends */
		if (a && g_ours)
			stream_end(a, 50);
		if (g_apt_run)
			apt_end();
		btt_ldac_set_mode(g_ldac_mode);   /* the bit rate setting applies to a running stream at once */
		stream_pass_ldac();
		return;
	}
	if (g_ldac_ours || btt_ldac_session_running())   /* an LDAC session of an earlier connection */
		ldac_end(50);
	if (btaac_neg.aptx) {   /* this connection is aptX: an AAC session of an earlier connection, if one is still running, ends */
		if (a && g_ours)
			stream_end(a, 50);
		stream_pass_aptx();
		return;
	}
	if (g_apt_run)   /* an aptX session of an earlier connection */
		apt_end();
	if (a)
		stream_pass(a, accel);
}

int btt_aac_stream_fast(void)
{
	uintptr_t conn = btaac_neg.conn;
	uint32_t rec;

	/* While the stream is configured as AAC and the connection's stream record is in use (any state from configured to closing: a pause, a microphone taking the link
	 * and giving it back all pass through the ones short of streaming), so the session ends and starts again within 0.1 s. With the headphones gone, the record
	 * is idle and the worker is back to its slow pass (an idle console is not woken ten times a second). */
	if (!g_installed || btaac_neg.state != BTAAC_N_CONFIGURED || !btaac_conn_ok(conn))
		return 0;
	rec = btaac_rd32(conn, BTAAC_C_REC_B);
	return rec >= 3u && rec <= 13u;
}

int btt_aac_stream_is_aac(void)
{
	return btaac_neg.state == BTAAC_N_CONFIGURED && !btaac_neg.aptx && !btaac_neg.ldac && btaac_conn_ok(btaac_neg.conn);   /* aptX is one packet a block, as SBC, and LDAC's calls are as many: the SBC limits apply */
}

void btt_aac_stream_set_ldac_mode(unsigned mode)
{
	g_ldac_mode = mode < BTLDAC_MODE_COUNT ? mode : BTLDAC_MODE_330;
}

void btt_aac_stream_set_aptx_hd(int on)
{
	g_aptx_hd = on ? 1u : 0u;
}

void btt_aac_stream_set_latency(int level)
{
	g_lat_level = level >= 0 && level <= 2 ? (uint32_t)level : 0u;
	btaac_neg.keep = btaac_lat_limit((unsigned)level);
}

void btt_aac_stream_set_source_only(int on)
{
	g_src_only = on != 0;
}

/* What Music Source Only did, into the log (the hooks may not log themselves). */
static void log_source_only(int on)
{
	int st = on && g_src_only ? (g_h_disp >= 0 ? 1 : 2) : 0;
	uint32_t d = btaac_neg.n_src_discover, r = btaac_neg.n_src_reject, n = btaac_neg.n_src_noreply;

	if (st != g_src_seen) {
		g_src_seen = st;
		BTT_KLOG("music source only: %s", st == 1 ? "on: devices are offered the Vita's source endpoint only, not its speaker (sink) endpoint" :
		         st == 2 ? "on, but the signalling hook is not in: the console's own list stands" : "off: the console's own endpoint list");
	}
	if (d != g_src_seen_disc) {
		g_src_seen_disc = d;
		BTT_KLOG("music source only: a device's DISCOVER was answered with the source endpoint only (%u so far)", (unsigned)d);
	}
	if (r != g_src_seen_rej) {
		g_src_seen_rej = r;
		BTT_KLOG("music source only: a device tried to use the Vita as a speaker (configure the sink endpoint): refused (%u so far); remove and pair the device again if it does not stream to the Vita's source", (unsigned)r);
	}
	if (n != g_src_seen_nore) {
		g_src_seen_nore = n;
		BTT_KLOG("music source only: could not answer on a device's signalling channel (%u so far): the console's own reply was used", (unsigned)n);
	}
}

void btt_aac_stream_set_kbps(unsigned kbps)
{
	g_kbps = kbps ? kbps : SESSION_BPS_KB;
}

int btt_aac_stream_set_preroll(int units)
{
	if (units < 0 || units > (int)BTAAC_PREROLL_MAX)
		return 0;
	g_preroll = (uint32_t)units;
	btaac_neg.preroll = g_preroll;
	return 1;
}

int btt_aac_stream_wants_pcm_hook(void)
{
	return g_installed;
}

void btt_aac_stream_restore(void)
{
	const BtAacApi *a = btaac_link_api;

	btaac_neg.enable = 0;
	btaac_neg.src_only = 0;
	btaac_cdc.on = 0;
	gates_down();
	apt_end();
	ldac_end(200);
	if (a)
		stream_end(a, 200);
	if (g_sev >= 0) {
		ksceKernelUnregisterSysEventHandler(g_sev);
		g_sev = -1;
	}
	if (g_h_send >= 0) {
		taiHookReleaseForKernel(g_h_send, btaac_send_ref);
		g_h_send = -1;
		for (int i = 0; i < 100 && btaac_send_in; i++)
			ksceKernelDelayThread(1000);
		btaac_send_ref = 0;
	}
	if (g_h_disp >= 0) {
		taiHookReleaseForKernel(g_h_disp, btaac_disp_ref);
		g_h_disp = -1;
		for (int i = 0; i < 100 && btaac_disp_in; i++)
			ksceKernelDelayThread(1000);
		btaac_disp_ref = 0;
	}
	g_installed = 0;
}
