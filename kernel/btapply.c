/*
 * btapply.c - settings that need a new connection apply at once: the system side of btapply_core.h.
 *
 * How the headphones are taken down and brought back (the choice, and why):
 *   - Down: ksceBtStartDisconnect(address), the call the Settings app's own Disconnect button makes. The Bluetooth code closes its channels one by
 *     one (each with an L2CAP disconnect, the audio stream's among them) and then ends the link with the "connection ended by the local user"
 *     reason, so the headphones see a clean, ordinary disconnect. The hands-free connection and the battery reading go with it and come back with the
 *     new connection exactly as they do when headphones are switched on.
 *   - Up: ksceBtStartConnect(address), the call Reconnect After Standby and the Settings app's Connect button make. The console is the initiator, so
 *     the audio stream is opened by the console, which is the path AAC negotiates on without the headphones' own START racing it.
 *   - Gentler ways looked at and not used: ksceBtStopAudio only suspends the stream (the configuration stays, so the codec cannot change; a suspended
 *     stream is also what the headphones' own START after a pause resumes); an L2CAP disconnect of the audio channels alone would need one of the
 *     Bluetooth code's internal routines (not an export) and its channel records, with a new byte-checked window on every build.
 *   - The console must not mute itself on the disconnect (its own policy does, and stays muted until the volume is changed). btdisc.c's "no mute"
 *     mode (Play on Speaker's, which also sends no notice to the music player) is held from before the call until btapply_core's tail is over.
 *
 * Runs only in the worker's pass: no hook, no callback, never the Settings page's thread.
 */
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysclib.h>

#include "../include/bluetoothturbo.h"
#include "btaac_api.h"
#include "btaac_hook.h"
#include "btaac_link.h"
#include "btaptx_link.h"
#include "btldac_link.h"
#include "btapply.h"
#include "btapply_core.h"
#include "btcall.h"
#include "btdisc.h"
#include "btlink.h"
#include "btreconn.h"
#include "klog.h"

static BtApply ga;
static volatile int ga_manual;
static volatile int ga_refresh;         /* Refresh Codecs was asked and the worker has not taken it yet (one at a time) */
static int ga_calls = -1;               /* -1 not looked for yet                                                       */
static uint32_t ga_logged;              /* the newest event logged                                                     */

void btt_btapply_pass(int on, int codec, int kbps, int accel, int quality, int aptx_hd)
{
	BtApplyIn in;
	BtLinkExtra x;
	BttLinkInfo li;
	uint64_t now = ksceKernelGetSystemTimeWide() / 1000u;
	int act;

	if (ga_calls < 0) {
		ga_calls = btcall_find_disconnect() == 0;
		if (!ga_calls)
			BTT_KLOG("bluetooth apply: the Bluetooth code's connect calls were not found - settings apply at the next connection");
	}
	btt_link_read(&li);
	btt_link_extra(&x);
	memset(&in, 0, sizeof(in));
	in.now_ms = now;
	in.on = on;
	in.manual = ga_manual;
	ga_manual = 0;
	in.refresh = __atomic_exchange_n(&ga_refresh, 0, __ATOMIC_ACQ_REL);
	if (in.refresh) {
		BTT_KLOG("bluetooth apply: Refresh Codecs pressed (list %s)", li.codec_mask ? "known" : "not known");
	}
	in.codec_known = li.codec_mask != 0;
	in.codec_mask = li.codec_mask;
	in.streaming = li.streaming != 0;
	in.set.codec = codec;
	in.set.kbps = kbps;
	in.set.accel = accel;
	in.set.quality = quality;
	in.set.aptx_hd = aptx_hd;
	in.calls_ok = ga_calls;
	in.aac_usable = btt_aac_codec_effective(codec) == BTT_CODEC_AAC;
	in.aptx_usable = btt_aptx_codec_effective(codec) == BTT_CODEC_APTX;
	in.ldac_usable = btt_ldac_codec_effective(codec) == BTT_CODEC_LDAC;
	in.hd_want = aptx_hd && btt_aptx_hd_usable() && !btaac_neg.hd_off && (!li.codec_mask || (li.codec_mask & BTT_HC_APTX_HD));
	in.link_hd = li.codec == BTT_LINK_CODEC_APTX && li.bitrate_kbps >= 500;   /* aptX HD is 576 kb/s, aptX 384 */
	in.neon_avail = (btt_aac_avail() & BTT_AAC_AVAIL_NEON) != 0;
	in.link = li.connected != 0;
	in.link_codec = (int)li.codec;
	in.voice = x.voice;
	in.bps_now = x.bps;
	in.max_bps = x.max_bps;
	in.aac_running = x.aac_running;
	in.neon_now = li.accel != 0;
	in.mac_ok = x.mac_ok;
	in.mac_lo = x.mac_lo;
	in.mac_hi = x.mac_hi;
	in.standby_busy = btt_btreconn_busy();
	in.declined = x.mac_ok ? btaac_ban_bits(x.mac_lo, x.mac_hi) : 0;   /* the codecs these headphones refused or broke (BTAAC_BAN_* are the bits the core reads) */
	in.local_off = x.mac_ok ? btaac_local_bits(x.mac_lo, x.mac_hi) : 0;   /* the codecs the module's own engine or add-on failed to run for these headphones: not offered, but not a refusal */
	in.offered = btaac_neg.enable != 0;   /* the stream code's hooks are in and the codec is not held: an SBC link is then the headphones' doing, not ours */
	in.disc_ok = btt_btdisc_in() == BTT_DISC_SPEAKER;
	in.bt_state = btapply_wants_state(&ga) && ga_calls ? btcall_state(ga.mac_lo, ga.mac_hi) : -1;

	act = btapply_step(&ga, &in);
	if (act == BTAPPLY_A_DISCONNECT) {
		int r = btcall_disconnect(ga.mac_lo, ga.mac_hi);

		BTT_KLOG("bluetooth apply: ksceBtStartDisconnect returned 0x%08x", (unsigned)r);
		btapply_done(&ga, act, r, now);
	} else if (act == BTAPPLY_A_CONNECT) {
		int r = btcall_connect(ga.mac_lo, ga.mac_hi);

		btapply_done(&ga, act, r, now);
		if (r >= 0)
			BTT_KLOG("bluetooth apply: ksceBtStartConnect returned 0x%08x (attempt %d)", (unsigned)r, ga.tries);
		else if (btapply_busy(r) && ga.phase == BTAPPLY_PH_UP)
			BTT_KLOG("bluetooth apply: ksceBtStartConnect returned 0x%08x (attempt %d of %d): the Bluetooth code is busy, asking again in %u ms", (unsigned)r, ga.tries,
			         BTAPPLY_CONNECT_TRIES, (unsigned)btapply_retry_ms(ga.tries));
		else
			BTT_KLOG("bluetooth apply: ksceBtStartConnect returned 0x%08x (attempt %d of %d)", (unsigned)r, ga.tries, BTAPPLY_CONNECT_TRIES);
	}
	if (ga.ev_seq != ga_logged) {
		ga_logged = ga.ev_seq;
		BTT_KLOG("bluetooth apply: %s", btapply_text(ga.ev));
	}
}

int btt_btapply_hold(void)
{
	return ga.hold;
}

int btt_btapply_request_refresh(void)
{
	if (__atomic_exchange_n(&ga_refresh, 1, __ATOMIC_ACQ_REL))
		return BTT_ERR_BUSY;     /* one request at a time: the worker has not taken the last one yet */
	return BTT_OK;
}
