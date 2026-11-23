/*
 * btapply_core.h - settings that need a new connection apply at once: when headphones are connected and one of them changes, the headphones
 * are disconnected and connected again from the console, and the new setting is in force when they are back. This is the decision part, with no
 * system call (btapply.c does the calls; the host test compiles this file as it is).
 *
 * What needs a new connection: the stream's codec and its figures are fixed when the stream is configured. That is the Bluetooth Codec, the AAC
 * Bit Rate, Hardware Accelerated AAC (taken when the encoder's session starts, which is at most as late as the stream's next start after a pause of
 * more than a few seconds), SBC Audio Quality (the highest bitpool and the channel mode) and aptX HD When Available (whether the aptX stream is the HD one). Every other setting is applied live (the Bluetooth
 * Latency, the sound filter, channels, balance, the volume, what the console does on a disconnect) or needs more than a reconnect (the Bluetooth
 * Name, Read Headset Battery) or would cost more than it gains (Wireless Controller Response), and is not handled here.
 *
 * The rules, in order, once a relevant setting has changed while headphones are connected:
 *   1. wait BTAPPLY_DEBOUNCE_MS after the last change, so scrolling through a list reconnects once, and at the end the values are compared with what the
 *      link has, not with where the scroll started: a list put back where it was changes nothing;
 *   2. nothing is done (and it is logged why) with Bluetooth Turbo off, with the headphones gone, while the hands-free voice link has the link (a
 *      microphone is open), while Reconnect After Standby is at work, when there is no address, when the code's calls are missing, or when the new value
 *      has no effect on this link: a bit rate or acceleration while the link is SBC, a quality while it is AAC, a rate the headphones already get
 *      (their maximum), an acceleration when the encoder is not running or NEON is not allowed, AAC for headphones that were refused or marked; aptX HD When Available while the
 *      link is not aptX, when the link already is what the choice gives (HD wanted and HD, or not wanted and classic), or when HD was offered at this connection and
 *      the headphones came up on classic aptX (they did not take it: a reconnect would not change that);
 *   3. the console must not mute itself when the headphones go: the "no mute" mode of btdisc.c is held for the time (btapply_hold). If it cannot be
 *      held, nothing is done;
 *   4. disconnect (btcall_disconnect), wait for "no link", wait a second, connect (btcall_connect), wait for the link. The connect is asked again, after 0.5, 1,
 *      2 and 2 s, when the Bluetooth code says it is busy (btapply_busy: its driver is still finishing the disconnect; a second after "no link" is not always enough);
 *      any other answer is final. If a step fails it is logged and left (the headphones reconnect by themselves, or the user does it, as today).
 * A change while an apply is running is noted; when it is over the new link is compared once more (after the debounce) and, if it still differs, it is
 * applied again. A codec asked for headphones that turn out not to take it is remembered, for that codec only (in memory, this start) so it is not repeated: AAC
 * refused does not stop an aptX change. Only a failure of the headphones counts: when the stream code did not offer the codec (its hooks are not in, or it is held), the
 * SBC link says nothing about the headphones and nothing is remembered.
 *
 * The upgrade of an early SBC connection (no setting changed): a headset that reconnects by itself at power-on (10 to 20 s) connects before the codec hooks go in
 * (45 s) or before the codec's self-test has its verdict, and so is on SBC for as long as the connection lasts. When the chosen codec (AAC, aptX or LDAC) is on offer
 * and usable, the headphones are on SBC and the connection was made while it was not (BtApply.early_codec, noted when the link came up), the same forced apply is
 * asked once, so they come back on the codec. Only when the headphones' stored codec list offers the codec (or there is none: the reconnect is what reads it), when
 * they were not marked or refused for it, with Bluetooth Turbo on, no call or microphone, no Reconnect After Standby, the link settled and its stream either idle or
 * started for BTAPPLY_UPGRADE_STREAM_MS (never inside the first connection's start exchange). Once for each headset in a start, whatever the answer; logged with the codec.
 *
 * Refresh Codecs (the button on the device page, BTT_KEY_REFRESH_CODECS) uses the same path without a changed setting: the console reads a headset's
 * codec list only when it opens the stream itself, so for headphones whose list is not known it disconnects them and connects them again. It is a
 * forced apply (no debounce, no comparison) with its own guards: one request at a time (none while an apply is pending or running), none when the list
 * is already known, and then the same gates as above (Bluetooth Turbo off, no headphones, a call or microphone, Reconnect After Standby, no address, no
 * calls, an unsettled link). When the headphones are back it is logged whether the list was read.
 */
#ifndef BTAPPLY_CORE_H
#define BTAPPLY_CORE_H

#include <stdint.h>

#include "../include/bluetoothturbo.h"
#include "btaac_core.h"
#include "btcall.h"
#include "btfall_codec.h"

#define BTAPPLY_DEBOUNCE_MS     1500u    /* after the last change                                                                       */
#define BTAPPLY_SETTLE_MS       4000u    /* the link is trusted (its stream configured) this long after it came up                       */
#define BTAPPLY_SETTLE_WAIT_MS  15000u   /* waited for that at most, then the change is dropped                                          */
#define BTAPPLY_HOLD_WAIT_MS    3000u    /* the no-mute mode must be in within this long                                                  */
#define BTAPPLY_DOWN_TIMEOUT_MS 8000u    /* from the disconnect call to "no link"                                                       */
#define BTAPPLY_DOWN_SETTLE_MS  1000u    /* after "no link": the console's own handling of the disconnect finishes                       */
#define BTAPPLY_BACK_TIMEOUT_MS 25000u   /* from the connect call to the link being back                                                */
#define BTAPPLY_TAIL_MS         5000u    /* the no-mute mode is kept this long after the link is back (or after a failure)               */
#define BTAPPLY_CONNECT_TRIES   5        /* the connect call is made this many times at most while the code says it is busy (btapply_busy)  */
#define BTAPPLY_NOAAC_N         4
#define BTAPPLY_UPGRADE_STREAM_MS 3000u  /* an early SBC link is upgraded only when its stream is idle, or has been started this long                */
#define BTAPPLY_UPGRADED_N      4        /* the headsets upgraded this start (once each); a fifth is never upgraded                                  */

/* The connect call's answers that mean "not now": the Bluetooth code's driver is not idle yet (0x802F0204: the disconnect is still being finished), or a link exists or
 * is being made (0x802F0203, 0x802F020E: the headphones came back by themselves, which the next pass sees). The call is made again after a short wait; any other answer
 * (the address is not registered, Bluetooth is off, ...) is given up at once. */
static inline int btapply_busy(int ret)
{
	uint32_t r = (uint32_t)ret;

	return r == 0x802F0204u || r == 0x802F0203u || r == 0x802F020Eu;
}

/* The wait before the connect call is made again, after `tries` calls were refused as busy: 0.5 s, 1 s, 2 s, 2 s (5.5 s in all). */
static inline uint32_t btapply_retry_ms(int tries)
{
	return tries <= 1 ? 500u : tries == 2 ? 1000u : 2000u;
}

enum { BTAPPLY_PH_IDLE = 0, BTAPPLY_PH_HOLD, BTAPPLY_PH_DOWN, BTAPPLY_PH_UP, BTAPPLY_PH_BACK, BTAPPLY_PH_TAIL, BTAPPLY_PH_COOL };
enum { BTAPPLY_A_NONE = 0, BTAPPLY_A_DISCONNECT, BTAPPLY_A_CONNECT };

/* What happened last (btapply.c logs each as it comes; the development port shows the last). */
enum {
	BTAPPLY_R_NONE = 0,
	BTAPPLY_R_START,          /* an apply began                                                                    */
	BTAPPLY_R_OK,             /* the headphones are back with the new setting                                      */
	BTAPPLY_R_OK_SBC,         /* back, but on SBC: the headphones do not take the codec (remembered, for that codec) */
	BTAPPLY_R_NOOP,           /* nothing the change did has an effect on this link                                 */
	BTAPPLY_R_DECLINED,       /* a codec was asked, the headphones were refused or marked for it: no               */
	BTAPPLY_R_VOICE,          /* the voice link has the link (a microphone is open)                                */
	BTAPPLY_R_STANDBY,        /* Reconnect After Standby is at work                                                */
	BTAPPLY_R_DISABLED,       /* Bluetooth Turbo is off                                                            */
	BTAPPLY_R_LINK_GONE,      /* the headphones went away before it began                                          */
	BTAPPLY_R_NO_ADDR,        /* the link's address could not be read                                              */
	BTAPPLY_R_NO_CALLS,       /* the Bluetooth code's connect or disconnect call was not found                     */
	BTAPPLY_R_UNSETTLED,      /* the link never settled                                                            */
	BTAPPLY_R_HOLD,           /* the no-mute mode could not be held (the audio policy's code was not recognised)    */
	BTAPPLY_R_DOWN_ERR,       /* the disconnect call was refused                                                   */
	BTAPPLY_R_DOWN_TIMEOUT,   /* the link did not go down                                                          */
	BTAPPLY_R_UP_ERR,         /* the connect call was refused                                                      */
	BTAPPLY_R_UP_TIMEOUT,     /* the link did not come back                                                        */
	BTAPPLY_R_SELF,           /* the headphones came back by themselves before the connect call                    */
	BTAPPLY_R_REF_ASK,        /* Refresh Codecs: asked, the reconnect follows                                       */
	BTAPPLY_R_REF_KNOWN,      /* Refresh Codecs: not done, the list is already known                                */
	BTAPPLY_R_REF_BUSY,       /* Refresh Codecs: not done, a reconnect is pending or running                        */
	BTAPPLY_R_REF_READ,       /* Refresh Codecs: the headphones are back and their list was read                    */
	BTAPPLY_R_REF_NONE,       /* Refresh Codecs: the headphones are back but did not give their list                */
	BTAPPLY_R_OK_LOCAL,       /* back, on SBC, but the codec was not on offer (the module's own hooks are out): nothing is remembered about the headphones */
	BTAPPLY_R_UP_AAC,         /* an early SBC connection is upgraded to AAC (the reconnect begins)                                                  */
	BTAPPLY_R_UP_APTX,        /* ... to aptX                                                                                                        */
	BTAPPLY_R_UP_LDAC,        /* ... to LDAC                                                                                                        */
	BTAPPLY_R_COUNT
};

typedef struct BtApplySet {
	int codec;      /* BttCodec                              */
	int kbps;       /* the AAC bit rate asked, kb/s           */
	int accel;      /* 0/1                                   */
	int quality;    /* BttQuality                            */
	int aptx_hd;    /* 0/1: aptX HD When Available            */
} BtApplySet;

typedef struct BtApplyIn {
	uint64_t now_ms;
	int on;               /* Bluetooth Turbo's master switch                                                     */
	int manual;           /* a test build asked for an apply now (no debounce, no comparison)                    */
	int refresh;          /* Refresh Codecs was asked since the last pass (a button press, once)                  */
	int codec_known;      /* the stored codec list of the headphones on the link is not empty                      */
	BtApplySet set;
	int calls_ok;         /* the Bluetooth code's disconnect and connect calls were found                         */
	int aac_usable;       /* the effective codec for an AAC request is AAC (the AAC module is usable)             */
	int aptx_usable;      /* the effective codec for an aptX request is aptX (the aptX module is usable)          */
	int ldac_usable;      /* the effective codec for an LDAC request is LDAC (the LDAC module is usable)          */
	int hd_want;          /* aptX HD would be offered to these headphones: aptX HD When Available is on, the HD encoder is usable and not
	                       * turned off for this start, and the headphones list aptX HD (or their list is not known)                      */
	int link_hd;          /* the link is aptX HD (its bit rate is not classic aptX's)                              */
	int neon_avail;       /* the NEON build may be used                                                          */
	int link;             /* headphones have an A2DP link                                                        */
	int link_codec;       /* BTT_LINK_CODEC_*                                                                    */
	int voice;            /* the hands-free voice link wants the link                                            */
	uint32_t bps_now;     /* AAC: the rate in force, b/s                                                         */
	uint32_t max_bps;     /* AAC: the headphones' highest, b/s (0 = not known)                                   */
	int aac_running;      /* AAC: the encoder's session is running                                              */
	int neon_now;         /* AAC: it runs the NEON build                                                         */
	int mac_ok;
	uint32_t mac_lo, mac_hi;
	int standby_busy;     /* Reconnect After Standby has a wake-up in hand                                       */
	uint32_t declined;    /* the codecs (BTFALL_C_* bit numbers: 1u << BTFALL_C_AAC, _APTX, _LDAC) the headphones refused or broke this start     */
	uint32_t local_off;   /* the codecs (the same bits) not offered to them this start because the module's own engine or add-on could not start a session: not the headphones' doing,
	                       * so it is read like the codec not being on offer (the stream code's hooks being out), never as a refusal                                                */
	int offered;          /* the stream code is offering the wanted codec (its hooks are in and it is not held): an SBC link is then the headphones' doing */
	int disc_ok;          /* the "no mute" mode of btdisc.c is in                                                */
	int bt_state;         /* btcall_state for the device (asked while the phase is DOWN or UP), else ignored      */
	int streaming;        /* the stream is started (sound is being sent)                                         */
	uint32_t codec_mask;  /* the codecs the headphones list (BTT_HC_*), 0 when not known                         */
} BtApplyIn;

typedef struct BtApply {
	int phase, hold, pending, force, refresh, have_seen, link_prev, goal_aac, goal_aptx, goal_ldac, q_at_link, aac_at_link, aptx_at_link, ldac_at_link, hd_at_link;
	uint64_t t_phase, change_at, link_since, t_disc;
	BtApplySet seen;
	uint32_t mac_lo, mac_hi;
	int tries;
	uint32_t no_aac_lo[BTAPPLY_NOAAC_N], no_aac_hi[BTAPPLY_NOAAC_N], no_aac_cdc[BTAPPLY_NOAAC_N];   /* headphones that came back on SBC, and the codec (1u << BTFALL_C_*) they were asked for */
	int offered;                  /* the codec was on offer all through this apply (and at the start): only then does a link that came back on SBC mark the headphones */
	int ready_prev;               /* the chosen codec was on offer and usable at the previous pass                                                       */
	uint32_t early_codec;         /* the codec (1u << BTFALL_C_*) chosen when the link came up while it was not ready: 0 when the connection was made with it ready */
	uint32_t upgrade;             /* the codec (the same bits) of the upgrade asked and not yet begun, else 0                                            */
	int stream_prev;
	uint64_t stream_since;        /* when the stream was last seen to start                                                                              */
	uint32_t up_lo[BTAPPLY_UPGRADED_N], up_hi[BTAPPLY_UPGRADED_N];   /* the headsets upgraded this start                                              */
	int up_n;
	int no_aac_n;
	/* for the log and the development port */
	int ev;                       /* the newest BTAPPLY_R_*; ev_seq counts them, btapply.c logs a new one */
	uint32_t ev_seq;
	uint32_t n_start, n_ok, n_fail, n_skip, n_refresh;
	int last, last_ret;
} BtApply;

static inline int btapply_same(const BtApplySet *a, const BtApplySet *b)
{
	return a->codec == b->codec && a->kbps == b->kbps && a->accel == b->accel && a->quality == b->quality && a->aptx_hd == b->aptx_hd;
}

static inline void btapply_ev(BtApply *st, int ev)
{
	st->ev = ev;
	st->ev_seq++;
}

static inline void btapply_drop(BtApply *st, int why)
{
	st->pending = 0;
	st->force = 0;
	st->refresh = 0;
	st->upgrade = 0;
	st->last = why;
	st->n_skip++;
	btapply_ev(st, why);
}

static inline int btapply_no_aac(const BtApply *st, uint32_t lo, uint32_t hi, uint32_t codec_bit)
{
	for (int i = 0; i < st->no_aac_n; i++)
		if (st->no_aac_lo[i] == lo && st->no_aac_hi[i] == hi && (st->no_aac_cdc[i] & codec_bit))
			return 1;
	return 0;
}

/* The headphones with this address came back on SBC when asked for the codec `codec_bit`: not asked for it again (a mark for another codec of theirs is kept as it is). */
static inline void btapply_mark(BtApply *st, uint32_t lo, uint32_t hi, uint32_t codec_bit)
{
	for (int i = 0; i < st->no_aac_n; i++)
		if (st->no_aac_lo[i] == lo && st->no_aac_hi[i] == hi) {
			st->no_aac_cdc[i] |= codec_bit;
			return;
		}
	if (st->no_aac_n < BTAPPLY_NOAAC_N) {
		st->no_aac_lo[st->no_aac_n] = lo;
		st->no_aac_hi[st->no_aac_n] = hi;
		st->no_aac_cdc[st->no_aac_n++] = codec_bit;
	}
}

/* 1 if the stream code is offering the codec (a BTFALL_C_* number) to these headphones: its hooks are in and the codec is not held, and the module's own engine or add-on has not failed for them this start. */
static inline int btapply_offered(const BtApplyIn *in, int codec)
{
	return in->offered && !(in->local_off & (1u << codec));
}

/* The codec the settings choose, as a BTFALL_C_* number, when the stream code is offering it and its module is usable; BTFALL_C_NONE otherwise (SBC chosen, or not ready). */
static inline int btapply_chosen_ready(const BtApplyIn *in)
{
	int c = in->set.codec == BTT_CODEC_AAC && in->aac_usable ? BTFALL_C_AAC :
	        in->set.codec == BTT_CODEC_APTX && in->aptx_usable ? BTFALL_C_APTX :
	        in->set.codec == BTT_CODEC_LDAC && in->ldac_usable ? BTFALL_C_LDAC : BTFALL_C_NONE;

	return c != BTFALL_C_NONE && btapply_offered(in, c) ? c : BTFALL_C_NONE;
}

/* 1 when the headphones' codec list lets the codec be asked: it lists it, or there is no list (the reconnect reads it). aptX also takes the HD entry when HD would be offered. */
static inline int btapply_list_offers(const BtApplyIn *in, int codec)
{
	if (!in->codec_mask)
		return 1;
	return codec == BTFALL_C_AAC ? (in->codec_mask & BTT_HC_AAC) != 0 :
	       codec == BTFALL_C_LDAC ? (in->codec_mask & BTT_HC_LDAC) != 0 :
	       (in->codec_mask & BTT_HC_APTX) != 0 || (in->hd_want && (in->codec_mask & BTT_HC_APTX_HD) != 0);
}

static inline int btapply_was_upgraded(const BtApply *st, uint32_t lo, uint32_t hi)
{
	for (int i = 0; i < st->up_n; i++)
		if (st->up_lo[i] == lo && st->up_hi[i] == hi)
			return 1;
	return 0;
}

/* The codec (a BTFALL_C_* number) an early SBC link should be upgraded to now, or BTFALL_C_NONE. Every guard the forced apply has is here too, so the apply
 * begins in the pass that asks for it (nothing is dropped and asked again). */
static inline int btapply_upgrade_codec(const BtApply *st, const BtApplyIn *in, uint64_t now)
{
	int c = btapply_chosen_ready(in);

	if (c == BTFALL_C_NONE || st->early_codec != (1u << c) || !in->on || !in->link || in->link_codec != BTT_LINK_CODEC_SBC)
		return BTFALL_C_NONE;
	if (in->voice || in->standby_busy || !in->mac_ok || !in->calls_ok || now - st->link_since < BTAPPLY_SETTLE_MS)
		return BTFALL_C_NONE;
	if (in->streaming && now - st->stream_since < BTAPPLY_UPGRADE_STREAM_MS)
		return BTFALL_C_NONE;   /* the headset's own start exchange may still be going on */
	if (!btapply_list_offers(in, c) || (in->declined & (1u << c)) || btapply_no_aac(st, in->mac_lo, in->mac_hi, 1u << c))
		return BTFALL_C_NONE;
	if (btapply_was_upgraded(st, in->mac_lo, in->mac_hi) || st->up_n >= BTAPPLY_UPGRADED_N)
		return BTFALL_C_NONE;
	return c;
}

/* 0 when something the settings ask differs, in effect, from what the link has; else the BTAPPLY_R_* that says why nothing does. */
static inline int btapply_mismatch(const BtApply *st, const BtApplyIn *in)
{
	/* a codec the stream code is not offering (its hooks are not in, or it is held) cannot come from a reconnect: it is not wanted here */
	int want_aac = btapply_offered(in, BTFALL_C_AAC) && in->aac_usable && in->set.codec == BTT_CODEC_AAC;
	int link_aac = in->link_codec == BTT_LINK_CODEC_AAC;

	int declined = want_aac && !link_aac && ((in->declined & (1u << BTFALL_C_AAC)) || btapply_no_aac(st, in->mac_lo, in->mac_hi, 1u << BTFALL_C_AAC) || st->aac_at_link);

	int want_aptx = btapply_offered(in, BTFALL_C_APTX) && in->aptx_usable && in->set.codec == BTT_CODEC_APTX;
	int link_aptx = in->link_codec == BTT_LINK_CODEC_APTX;
	int declined_aptx = want_aptx && !link_aptx && ((in->declined & (1u << BTFALL_C_APTX)) || btapply_no_aac(st, in->mac_lo, in->mac_hi, 1u << BTFALL_C_APTX) || st->aptx_at_link);

	int want_ldac = btapply_offered(in, BTFALL_C_LDAC) && in->ldac_usable && in->set.codec == BTT_CODEC_LDAC;
	int link_ldac = in->link_codec == BTT_LINK_CODEC_LDAC;
	int declined_ldac = want_ldac && !link_ldac && ((in->declined & (1u << BTFALL_C_LDAC)) || btapply_no_aac(st, in->mac_lo, in->mac_hi, 1u << BTFALL_C_LDAC) || st->ldac_at_link);

	if (declined_ldac)
		want_ldac = 0;   /* and for LDAC */
	if (declined_aptx)
		want_aptx = 0;   /* the same for aptX */
	if (declined)
		want_aac = 0;   /* a reconnect would not give AAC: only what an SBC link has can differ */
	if (want_aac != link_aac || want_aptx != link_aptx || want_ldac != link_ldac)
		return 0;
	if (link_ldac)
		return BTAPPLY_R_NOOP;   /* the LDAC bit rate setting is passed to the running stream at once; no AAC rate, no accelerated build and no SBC settings apply */
	if (link_aptx) {
		/* aptX has no rate, no accelerated build and no SBC settings: only HD or classic can differ. The link was made with HD wanted and is classic: these
		 * headphones did not take it (no HD endpoint, or the stream was refused), and a reconnect would not change that */
		if ((in->hd_want ? 1 : 0) != (in->link_hd ? 1 : 0) && !(in->hd_want && !in->link_hd && st->hd_at_link))
			return 0;
		return BTAPPLY_R_NOOP;
	}
	if (link_aac) {
		if (btaac_clamp_bps((uint32_t)in->set.kbps * 1000u, 48000u, 2u, in->max_bps) != in->bps_now)
			return 0;
		if (in->aac_running && (in->set.accel && in->neon_avail ? 1 : 0) != (in->neon_now ? 1 : 0))
			return 0;
		return BTAPPLY_R_NOOP;
	}
	if (in->set.quality != st->q_at_link)
		return 0;
	return declined || declined_aptx || declined_ldac ? BTAPPLY_R_DECLINED : BTAPPLY_R_NOOP;
}

/* Over before anything was touched: counted as a skip, not a failure. */
static inline void btapply_abort(BtApply *st, int why, uint64_t now)
{
	st->phase = BTAPPLY_PH_IDLE;
	st->t_phase = now;
	st->hold = 0;
	st->refresh = 0;
	st->last = why;
	st->n_skip++;
	btapply_ev(st, why);
}

/* A step failed after the disconnect was asked: the no-mute mode is kept for BTAPPLY_TAIL_MS more (the disconnect may still land). */
static inline void btapply_fail(BtApply *st, int why, uint64_t now)
{
	st->last = why;
	st->n_fail++;
	st->refresh = 0;
	btapply_ev(st, why);
	st->phase = BTAPPLY_PH_COOL;
	st->t_phase = now;
}

/* One worker pass. Returns the call to make (BTAPPLY_A_*), after which the caller reports its answer with btapply_done. */
static inline int btapply_step(BtApply *st, const BtApplyIn *in)
{
	uint64_t now = in->now_ms;
	int r;

	if (in->link && !st->link_prev) {
		st->link_since = now;
		st->q_at_link = in->set.quality;   /* the quality the connection was made under */
		st->aac_at_link = btapply_offered(in, BTFALL_C_AAC) && in->aac_usable && in->set.codec == BTT_CODEC_AAC;   /* AAC was on offer when it was made: if it is SBC now, these headphones did not take it */
		st->aptx_at_link = btapply_offered(in, BTFALL_C_APTX) && in->aptx_usable && in->set.codec == BTT_CODEC_APTX;   /* ... and the same for aptX */
		st->ldac_at_link = btapply_offered(in, BTFALL_C_LDAC) && in->ldac_usable && in->set.codec == BTT_CODEC_LDAC;   /* ... and for LDAC */
		st->hd_at_link = btapply_offered(in, BTFALL_C_APTX) && in->aptx_usable && in->set.codec == BTT_CODEC_APTX && in->hd_want;   /* aptX HD was offered when it was made: if the link is classic aptX, these headphones did not take HD */
	}
	{
		int ready = btapply_chosen_ready(in);

		if (in->link && !st->link_prev) {
			/* a codec was chosen and the connection was made while it was not ready (the hooks not in yet, or its self-test not done): the link says nothing of the
			 * headphones, and btapply_upgrade_codec may bring it up later. "Ready" has to have held at the pass before as well, as a link seen first in the
			 * pass the hooks went in was made before them. */
			int chosen = in->set.codec == BTT_CODEC_AAC ? BTFALL_C_AAC : in->set.codec == BTT_CODEC_APTX ? BTFALL_C_APTX : in->set.codec == BTT_CODEC_LDAC ? BTFALL_C_LDAC : BTFALL_C_NONE;

			st->early_codec = chosen != BTFALL_C_NONE && !(ready == chosen && st->ready_prev) ? 1u << chosen : 0;
		}
		if (!in->link)
			st->early_codec = 0;
		st->ready_prev = ready != BTFALL_C_NONE;
	}
	if (in->streaming && !st->stream_prev)
		st->stream_since = now;
	st->stream_prev = in->streaming;
	st->link_prev = in->link;
	if (!st->have_seen) {
		st->seen = in->set;
		st->have_seen = 1;
	}
	if (!btapply_same(&st->seen, &in->set)) {
		st->seen = in->set;
		if (in->on && (in->link || st->phase != BTAPPLY_PH_IDLE)) {   /* also while an apply runs: the new connection may have been negotiated before the change */
			st->pending = 1;
			st->change_at = now;
		}
	}
	if (in->refresh) {
		/* one at a time: a pending or running apply is left alone (its own request would be lost by clearing it) */
		if (st->pending || st->phase != BTAPPLY_PH_IDLE) {
			st->n_skip++;
			btapply_ev(st, BTAPPLY_R_REF_BUSY);
		} else if (in->codec_known) {
			st->n_skip++;
			btapply_ev(st, BTAPPLY_R_REF_KNOWN);
		} else {
			st->pending = 1;
			st->force = 1;
			st->refresh = 1;
			st->n_refresh++;
			st->change_at = now >= BTAPPLY_DEBOUNCE_MS ? now - BTAPPLY_DEBOUNCE_MS : 0;
			btapply_ev(st, BTAPPLY_R_REF_ASK);
		}
	}
	if (in->manual) {
		st->pending = 1;
		st->force = 1;
		st->change_at = now >= BTAPPLY_DEBOUNCE_MS ? now - BTAPPLY_DEBOUNCE_MS : 0;
	}

	if (!st->pending && st->phase == BTAPPLY_PH_IDLE && (r = btapply_upgrade_codec(st, in, now)) != BTFALL_C_NONE) {
		st->pending = 1;
		st->force = 1;
		st->upgrade = 1u << r;
		st->change_at = now >= BTAPPLY_DEBOUNCE_MS ? now - BTAPPLY_DEBOUNCE_MS : 0;
	}

	switch (st->phase) {
	case BTAPPLY_PH_IDLE:
		if (!st->pending)
			return BTAPPLY_A_NONE;
		if (!in->on)
			btapply_drop(st, BTAPPLY_R_DISABLED);
		else if (!in->link)
			btapply_drop(st, BTAPPLY_R_LINK_GONE);
		else if (now - st->change_at < BTAPPLY_DEBOUNCE_MS)
			return BTAPPLY_A_NONE;
		else if (in->voice)
			btapply_drop(st, BTAPPLY_R_VOICE);
		else if (in->standby_busy)
			btapply_drop(st, BTAPPLY_R_STANDBY);
		else if (!in->mac_ok)
			btapply_drop(st, BTAPPLY_R_NO_ADDR);
		else if (!in->calls_ok)
			btapply_drop(st, BTAPPLY_R_NO_CALLS);
		else if (now - st->link_since < BTAPPLY_SETTLE_MS) {
			if (now - st->change_at >= BTAPPLY_DEBOUNCE_MS + BTAPPLY_SETTLE_WAIT_MS)
				btapply_drop(st, BTAPPLY_R_UNSETTLED);
		} else if (!st->force && (r = btapply_mismatch(st, in)) != 0)
			btapply_drop(st, r);
		else {
			st->pending = 0;
			st->force = 0;
			st->tries = 0;
			st->mac_lo = in->mac_lo;
			st->mac_hi = in->mac_hi;
			st->goal_aac = in->aac_usable && in->set.codec == BTT_CODEC_AAC;
			st->goal_aptx = in->aptx_usable && in->set.codec == BTT_CODEC_APTX;
			st->goal_ldac = in->ldac_usable && in->set.codec == BTT_CODEC_LDAC;
			st->offered = btapply_offered(in, st->goal_aac ? BTFALL_C_AAC : st->goal_aptx ? BTFALL_C_APTX : BTFALL_C_LDAC);   /* for the goal's codec (LDAC's bit when there is no goal: nothing reads it then) */
			st->hold = 1;
			st->phase = BTAPPLY_PH_HOLD;
			st->t_phase = now;
			st->n_start++;
			if (st->upgrade) {
				st->up_lo[st->up_n] = in->mac_lo;   /* once for each headset in a start: the answer does not matter (btapply_upgrade_codec made room) */
				st->up_hi[st->up_n++] = in->mac_hi;
				btapply_ev(st, st->goal_aac ? BTAPPLY_R_UP_AAC : st->goal_aptx ? BTAPPLY_R_UP_APTX : BTAPPLY_R_UP_LDAC);
				st->upgrade = 0;
			} else {
				btapply_ev(st, BTAPPLY_R_START);
			}
		}
		return BTAPPLY_A_NONE;
	case BTAPPLY_PH_HOLD:
		if (in->voice) {
			btapply_abort(st, BTAPPLY_R_VOICE, now);   /* a microphone opened meanwhile: nothing was touched yet */
		} else if (!in->link) {
			btapply_abort(st, BTAPPLY_R_LINK_GONE, now);
		} else if (in->disc_ok) {
			st->phase = BTAPPLY_PH_DOWN;
			st->t_phase = st->t_disc = now;
			return BTAPPLY_A_DISCONNECT;
		} else if (now - st->t_phase >= BTAPPLY_HOLD_WAIT_MS) {
			btapply_abort(st, BTAPPLY_R_HOLD, now);
		}
		return BTAPPLY_A_NONE;
	case BTAPPLY_PH_DOWN:
		if (in->bt_state == BTCALL_STATE_NO_LINK) {
			st->phase = BTAPPLY_PH_UP;
			st->t_phase = now;
		} else if (now - st->t_phase >= BTAPPLY_DOWN_TIMEOUT_MS)
			btapply_fail(st, BTAPPLY_R_DOWN_TIMEOUT, now);
		return BTAPPLY_A_NONE;
	case BTAPPLY_PH_UP:
		if (now - st->t_phase < (st->tries ? btapply_retry_ms(st->tries) : BTAPPLY_DOWN_SETTLE_MS))
			return BTAPPLY_A_NONE;
		if (in->bt_state != BTCALL_STATE_NO_LINK && in->bt_state >= 0) {
			st->phase = BTAPPLY_PH_BACK;       /* the headphones came back by themselves */
			st->t_phase = now;
			btapply_ev(st, BTAPPLY_R_SELF);
			return BTAPPLY_A_NONE;
		}
		return BTAPPLY_A_CONNECT;
	case BTAPPLY_PH_BACK:
		if (in->link && st->link_since > st->t_disc) {
			st->phase = BTAPPLY_PH_TAIL;
			st->t_phase = now;
		} else if (now - st->t_phase >= BTAPPLY_BACK_TIMEOUT_MS)
			btapply_fail(st, BTAPPLY_R_UP_TIMEOUT, now);
		return BTAPPLY_A_NONE;
	case BTAPPLY_PH_TAIL:
		if (!in->link) {                        /* flapped: it is over, the headphones are not steady enough to judge */
			st->hold = 0;
			st->phase = BTAPPLY_PH_IDLE;
			st->last = BTAPPLY_R_OK;
			st->n_ok++;
			btapply_ev(st, st->refresh ? (in->codec_known ? BTAPPLY_R_REF_READ : BTAPPLY_R_REF_NONE) : BTAPPLY_R_OK);
			st->refresh = 0;
		} else if (now - st->t_phase >= BTAPPLY_TAIL_MS && now - st->link_since >= BTAPPLY_SETTLE_MS + 1000u) {
			int sbc = (st->goal_aac && in->link_codec != BTT_LINK_CODEC_AAC) || (st->goal_aptx && in->link_codec != BTT_LINK_CODEC_APTX) || (st->goal_ldac && in->link_codec != BTT_LINK_CODEC_LDAC);
			int bit_codec = st->goal_aac ? BTFALL_C_AAC : st->goal_aptx ? BTFALL_C_APTX : BTFALL_C_LDAC;
			uint32_t bit = 1u << bit_codec;

			int local = sbc && !(st->offered && btapply_offered(in, bit_codec));

			/* SBC is the headphones' doing only when the codec was on offer; with the stream code's hooks out or the codec held, nothing is learnt about them */
			if (sbc && !local)
				btapply_mark(st, st->mac_lo, st->mac_hi, bit);
			st->hold = 0;
			st->phase = BTAPPLY_PH_IDLE;
			st->last = local ? BTAPPLY_R_OK_LOCAL : sbc ? BTAPPLY_R_OK_SBC : BTAPPLY_R_OK;
			st->n_ok++;
			btapply_ev(st, st->refresh ? (in->codec_known ? BTAPPLY_R_REF_READ : BTAPPLY_R_REF_NONE) : st->last);
			st->refresh = 0;
		}
		return BTAPPLY_A_NONE;
	case BTAPPLY_PH_COOL:
		if (now - st->t_phase >= BTAPPLY_TAIL_MS) {
			st->hold = 0;
			st->phase = BTAPPLY_PH_IDLE;
		}
		return BTAPPLY_A_NONE;
	}
	return BTAPPLY_A_NONE;
}

/* The answer of the call btapply_step asked for. */
static inline void btapply_done(BtApply *st, int act, int ret, uint64_t now)
{
	st->last_ret = ret;
	if (act == BTAPPLY_A_DISCONNECT) {
		if (ret < 0)
			btapply_fail(st, BTAPPLY_R_DOWN_ERR, now);
	} else if (act == BTAPPLY_A_CONNECT) {
		st->tries++;
		if (ret >= 0) {
			st->phase = BTAPPLY_PH_BACK;
			st->t_phase = now;
		} else if (!btapply_busy(ret) || st->tries >= BTAPPLY_CONNECT_TRIES)
			btapply_fail(st, BTAPPLY_R_UP_ERR, now);   /* refused for good, or busy through every attempt */
		else
			st->t_phase = now;                 /* the code is busy (the disconnect is still being finished): asked again after btapply_retry_ms */
	}
}

/* 1 while the caller must fill in.bt_state this pass. */
static inline int btapply_wants_state(const BtApply *st)
{
	return st->phase == BTAPPLY_PH_DOWN || st->phase == BTAPPLY_PH_UP;
}

static inline const char *btapply_text(int ev)
{
	switch (ev) {
	case BTAPPLY_R_START:        return "applying: the headphones are disconnected and connected again";
	case BTAPPLY_R_OK:           return "the headphones are back, the setting is in force";
	case BTAPPLY_R_OK_SBC:       return "the headphones are back on SBC: they do not take that codec (not tried again this start)";
	case BTAPPLY_R_UP_AAC:       return "upgrading the connection from SBC to AAC now that it is ready: the headphones are disconnected and connected again";
	case BTAPPLY_R_UP_APTX:      return "upgrading the connection from SBC to aptX now that it is ready: the headphones are disconnected and connected again";
	case BTAPPLY_R_UP_LDAC:      return "upgrading the connection from SBC to LDAC now that it is ready: the headphones are disconnected and connected again";
	case BTAPPLY_R_OK_LOCAL:     return "the headphones are back on SBC, but the codec was not on offer (the module's own hooks are not in): nothing is remembered about them";
	case BTAPPLY_R_NOOP:         return "no reconnect: the change has no effect on this connection";
	case BTAPPLY_R_DECLINED:     return "no reconnect: these headphones were refused that codec";
	case BTAPPLY_R_VOICE:        return "no reconnect: the voice link has the connection (a microphone is open)";
	case BTAPPLY_R_STANDBY:      return "no reconnect: Reconnect After Standby is at work";
	case BTAPPLY_R_DISABLED:     return "no reconnect: Bluetooth Turbo is off";
	case BTAPPLY_R_LINK_GONE:    return "no reconnect: the headphones went away";
	case BTAPPLY_R_NO_ADDR:      return "no reconnect: the headphones' address could not be read";
	case BTAPPLY_R_NO_CALLS:     return "no reconnect: the Bluetooth code's connect calls were not found";
	case BTAPPLY_R_UNSETTLED:    return "no reconnect: the connection did not settle";
	case BTAPPLY_R_HOLD:         return "no reconnect: the console could not be kept from muting itself (the audio policy's code was not recognised)";
	case BTAPPLY_R_DOWN_ERR:     return "FAILED: the disconnect was refused (left as it is)";
	case BTAPPLY_R_DOWN_TIMEOUT: return "FAILED: the headphones did not disconnect (left as it is)";
	case BTAPPLY_R_UP_ERR:       return "FAILED: the connect was refused (the headphones must be reconnected by hand)";
	case BTAPPLY_R_UP_TIMEOUT:   return "FAILED: the headphones did not come back (left as it is)";
	case BTAPPLY_R_SELF:         return "the headphones came back by themselves";
	case BTAPPLY_R_REF_ASK:      return "refresh codecs: asked, the headphones are disconnected and connected again so the console reads their codecs";
	case BTAPPLY_R_REF_KNOWN:    return "refresh codecs: not done, the headphones' codecs are already known";
	case BTAPPLY_R_REF_BUSY:     return "refresh codecs: not done, a reconnect is already pending or running";
	case BTAPPLY_R_REF_READ:     return "refresh codecs: the headphones are back and their codecs were read";
	case BTAPPLY_R_REF_NONE:     return "refresh codecs: the headphones are back but did not give their codecs (the list is still not known)";
	}
	return "?";
}

#endif /* BTAPPLY_CORE_H */
