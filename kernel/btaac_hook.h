/*
 * btaac_hook.h - the hooks that make a stream AAC (btaac_hook.c) and the state they share with the worker (btaac_stream.c) and the audio
 * sender's hook (bteq_hook.c). Installing them, the session and the rest is btaac_stream.c.
 *
 * Three places in the Bluetooth code take part (all offsets are in btaac_gate_data.h, proven on every build 3.60-3.74):
 *   H1  the receive routine of the audio streaming signalling (0x9F4C): reads what the sink offers (observe only, the original always runs
 *       with every argument as it came), keeps the first free sink endpoint that offers AAC LC at 48 kHz, two channels, and sees the sink's
 *       answer to our configuration. If the sink rejects it, the saved SBC configuration is sent again with the Bluetooth code's own sender.
 *   H2  the routine that narrows the sink's choice into the configuration (0x732C, shared with the connection quality hook, btqual_hook.c):
 *       after the original has run on the SBC choice (so every SBC value the stream needs stays valid), the stream record's endpoint and
 *       capability bytes are replaced by the AAC configuration (btaac_h2_after below). The code then sends exactly that.
 *   H3  the routine that sends one media packet on the open stream (0xAD6C, called once per 512-frame block by the sender's code after it has
 *       done all its own gating and bookkeeping): for the AAC stream it sends what the engine made, in LATM and RTP, instead of the SBC packet.
 *
 * H1 also answers when the headphones open the stream (they are the initiator and the code only answers): see the "acceptor" part of btaac_hook.c.
 * With AAC wanted, DISCOVER lists a third endpoint (an AAC source, after the code's two), GET_CAPABILITIES for it is answered here, and
 * a SET_CONFIGURATION for it is checked and then handed to the code as a SET_CONFIGURATION for its own SBC endpoint (so its state machine, media channel
 * and gates run exactly as for SBC), after which the record is rewritten to the AAC configuration. OPEN, START, SUSPEND, CLOSE and ABORT for endpoint 3 are
 * handed to the code as the same commands for endpoint 1. With SBC wanted none of this exists: commands pass through unchanged.
 *
 * All three run in the Bluetooth code's own threads (signalling: its receive thread; media: the audio sender's thread): no logging, no locks,
 * no waits, no allocation, no floating point, and no call but the original routines and the engine's two leaves (btaac_link_leaf.c). The one
 * other call is SceBt's own frame sender, by pointer, in the reject fallback, from the same thread and context as the stock code makes it.
 *
 * The negotiation state is one connection's (the Bluetooth code's link slot, validated against the table's bounds before it is touched): the
 * console streams audio to one pair of headphones at a time.
 */
#ifndef BTAAC_HOOK_H
#define BTAAC_HOOK_H

#include <stdint.h>

#include "btaac_sep.h"
#include "btcodecs.h"
#include "btaac_link.h"
#include "btaac_pace.h"
#include "btaptx_sep.h"
#include "btaptx_link.h"
#include "btldac_sep.h"
#include "btldac_link.h"
#include "btfall_codec.h"

/* The negotiation's state for the connection `btaac_neg.conn`. */
enum {
	BTAAC_N_NONE = 0,       /* SBC (the console's own) for this connection: nothing found, switched off, or declined      */
	BTAAC_N_PROBING,        /* the sink's endpoints are being read (after DISCOVER); `have` says an AAC endpoint was found  */
	BTAAC_N_PENDING,        /* the AAC configuration has been sent: the sink's answer is awaited                           */
	BTAAC_N_CONFIGURED,     /* the sink accepted: the stream is AAC and the engine is to feed it                           */
	BTAAC_N_FALLBACK,       /* the sink rejected AAC: the SBC configuration was sent again; this device is not offered AAC again this boot */
	BTAAC_N_BROKEN,         /* AAC failed after it was configured: no sound until the headphones reconnect (SBC from then on) */
	BTAAC_N_OFFERED         /* the headphones opened the stream (we are the acceptor) and were offered endpoint 3; SBC until they configure it    */
};

/* Who opened the stream (btaac_neg.init). */
enum { BTAAC_INIT_NONE = 0, BTAAC_INIT_VITA = 1, BTAAC_INIT_HEADSET = 2 };
/* btaac_neg.acc_flags: what happened on a connection the headphones opened (cleared by each DISCOVER). */
#define BTAAC_ACC_ADVERTISED  1u   /* endpoint 3 was listed in the reply to their DISCOVER */
#define BTAAC_ACC_ASKED       2u   /* they asked for its capabilities                      */
#define BTAAC_ACC_CONFIGURED  4u   /* they configured it and the code accepted             */

/* Offsets in a link slot of the Bluetooth code's data (the same in every build 3.60-3.74). */
#define BTAAC_C_ADDR_LO    0x18u     /* the device address, low word                                                         */
#define BTAAC_C_ADDR_HI    0x1Cu
#define BTAAC_C_LIST       0x3B35u   /* the endpoints DISCOVER returned: 16 bytes, two per endpoint                         */
#define BTAAC_C_INDEX      0x3B46u   /* the byte offset in that list of the endpoint being asked for its capabilities        */
#define BTAAC_C_REC_B      0x3B70u   /* the stream record of the source role: +0 state, +4 our endpoint, +5 theirs, +6 the capabilities, +0x18 their length, +0x20 the label */
#define BTAAC_C_MEDIA_CH   0x3B8Cu   /* the media channel (a pointer); +8 in it is the largest packet the sink takes (L2CAP MTU) */
#define BTAAC_C_PROFILES   0x3E1Cu   /* bit 3: the audio stream is started                                                   */
#define BTAAC_C_WANT       0x3E18u   /* bit 3: the console's START of the audio stream was asked for and is not over ("starting"); it stays set after the sink rejects the START */
#define BTAAC_C_PARKED     0x3E24u   /* bit 3: the same request, held until the profile is up                                */
#define BTAAC_R_RETRY      0x21u     /* in the stream record: 1 = the sink rejected our START (the record went back to 8), 0 once a START or SUSPEND is sent */
#define BTAAC_VOICE_BIT    0x20000000u   /* in the three words above: the hands-free voice link wants the link                */
#define BTAAC_SLOT_SIZE    0x41D0u   /* the link table has 7 slots of this size (BTAAC_C_*, and the table's start, are checked by the gate) */
#define BTAAC_SLOTS        7u
#define BTAAC_REC_OPEN     10u       /* the stream record's state once the media channel is open                              */
#define BTAAC_REC_READY     8u       /* open, not streaming (also where a rejected START leaves it); 9 = our START sent, the answer awaited */
#define BTAAC_REC_STARTING  9u
#define BTAAC_PROF_STARTED 0x8u
#define BTAAC_STARTING_BIT 0x8u      /* the same bit of BTAAC_C_WANT and BTAAC_C_PARKED */
#define BTAAC_BAN_N        4u
/* What a mark is for: the codecs the headphones refused or broke (one bit each, numbered as the notice's codec names are). A mark for one codec never keeps the headphones from another. */
#define BTAAC_BAN_AAC      (1u << BTFALL_C_AAC)
#define BTAAC_BAN_APTX     (1u << BTFALL_C_APTX)
#define BTAAC_BAN_LDAC     (1u << BTFALL_C_LDAC)
#define BTAAC_BAN_ALL      (BTAAC_BAN_AAC | BTAAC_BAN_APTX | BTAAC_BAN_LDAC)
/* A mark of the module's own (an engine or add-on that could not start a session) keeps the codec from being offered again this boot, like the headphones' mark, but it is a
 * separate one: the same bits shifted up in the same word. btaac_ban_bits (what the codec notice and the apply's decision read as "the headphones refused it") never sees it. */
#define BTAAC_LOCAL_SHIFT  8u

typedef struct BtAacNeg {
	/* the worker's, read by the hooks */
	uint32_t enable;           /* 1: offer AAC to the next sink (the AAC module is usable, the Bluetooth Codec is AAC, Bluetooth Turbo is on) */
	uint32_t send_gate;        /* 1: the engine's session is running: the open AAC stream's packets are the engine's                         */
	uint32_t pcm_gate;         /* 1: the audio sender's blocks go to the engine (the same, raised first and dropped first)                    */
	uint32_t kbps;             /* the bit rate to ask for, kb/s                                                                                 */
	uint32_t want_aptx;        /* with `enable`: the codec offered is aptX, not AAC                                                              */
	uint32_t want_aptx_hd;     /* with want_aptx: aptX HD (experimental) is preferred when the headphones offer it (the add-on's HD self-test passed, not turned off) */
	uint32_t want_ldac;        /* with `enable`: the codec offered is LDAC (one codec is looked for at a time)                                                     */
	uint32_t ldac_keep;        /* the LDAC packet ring's bound (btldac_keep), 0 = not bounded                                                                         */
	uint32_t src_only;         /* 1: the Vita offers sources only (Music Source Only): the list DISCOVER returns has no sink, a configuration naming the sink is rejected; independent of `enable` */
	uint32_t hd_off;           /* an aptX HD stream was rejected or broke this boot: HD is not offered again (the devices keep classic aptX)       */
	/* set once at install */
	uintptr_t conn_lo, conn_hi; /* the link table's bounds (an address range): a pointer outside it is not a link                              */
	uintptr_t l2_send;         /* the Bluetooth code's signalling frame sender, as a Thumb function pointer                                     */
	uintptr_t l2_fmt;          /* the format of SET_CONFIGURATION                                                                               */
	uintptr_t l2_fmt_acc;      /* the format of an accept ("11 #": two bytes, then bytes and their length) and ...                               */
	uintptr_t l2_fmt_rej;      /* ... of a reject ("1111")                                                                                      */
	/* the negotiation, written by the hooks (the worker only marks a stream broken) */
	uintptr_t conn;
	uint32_t state;            /* BTAAC_N_*                                                                                                     */
	uint32_t init;             /* BTAAC_INIT_*: who opened the stream on this connection                                                         */
	uint32_t acceptor;         /* 1: the headphones opened it and the code is the acceptor (endpoint 3 is ours on this connection)               */
	uint32_t acc_flags;        /* BTAAC_ACC_*                                                                                                   */
	uint32_t aptx;             /* this connection's non-SBC codec is aptX (set with the endpoint chosen or configured; AAC when 0)               */
	uint32_t apt_hd;           /* ... and it is aptX HD (only on a connection the Vita opened)                                                   */
	uint32_t ldac;             /* this connection's non-SBC codec is LDAC (aptx is 0 then)                                                                      */
	uint32_t ep_byte;          /* the endpoint we offered on a connection the headphones opened: BTAAC_SEID_AAC_BYTE, BTAPTX_SEID_BYTE or BTLDAC_SEID_BYTE */
	uint32_t config_len;       /* the length of `config`                                                                                         */
	uint32_t apt_ready;        /* aptX: the sender's hook left a block in btaac_apt_pcm that the media hook has not sent yet                      */
	uint32_t have;             /* an AAC endpoint was found: seid, object, vbr, max_bps are valid                                                */
	uint32_t seid, object, vbr, max_bps, bps;
	uint32_t keep;             /* the engine's output kept at most this many units deep (btaac_lat_limit); 0 = not bounded                       */
	uint32_t preroll;          /* units that must be ready before the stream starts, and again after a dry ring (btaac_pace.h); 0 = no pacing    */
	BtAacPace pace;            /* the media hook's pacing and its counters                                                                      */
	uint32_t sbc_seid, sbc_len;                       /* the SBC choice the code made, for the fallback */
	uint8_t  sbc_caps[BTAAC_CAPS_BUF];
	uint8_t  config[BTAAC_CONFIG_BUF];                /* the configuration sent                          */
	uint32_t ban_lo[BTAAC_BAN_N], ban_hi[BTAAC_BAN_N], ban_cdc[BTAAC_BAN_N], ban_n;   /* devices that rejected or broke a codec this boot, and which (BTAAC_BAN_*); ban_n counts the marks made, the table is written round */
	/* the media side (the audio sender's thread) */
	uint32_t seq, ts, ssrc, last_au, have_last;
	/* counters, for the log and the development port */
	uint32_t n_discover, n_caps, n_chosen, n_rewrite, n_accept, n_reject, n_resend, n_resend_none;
	uint32_t n_sent, n_pieces, n_no_au, n_send_err, n_wrap_err, n_swallowed, n_pcm_blocks, n_declined;
	uint32_t last_mtu, last_au_len, last_latm_len, max_au_len;
	uint32_t n_spaced, n_waiting;   /* media hook calls that sent nothing because a unit went on the call before (spacing), or the pre-roll was not yet met */
	uint32_t n_over, n_back;   /* units that did not fit one packet (the cap failed or the limit is smaller than the session was made for), and the pieces after their first, sent in the same call */
	uint32_t n_ldac_pkts, n_ldac_calls, n_ldac_calls2, n_ldac_dry, n_ldac_mtu, n_ldac_trim;   /* LDAC: packets sent; calls that sent; calls that sent 2; calls with none ready; refusals for the packet limit; packets trimmed */
	uint32_t n_apt_blocks, n_apt_late;   /* aptX: blocks the sender handed over; blocks the sender left that were overwritten before they were sent (congestion dropped them) */
	uint32_t n_src_discover, n_src_reject, n_src_noreply;   /* sources only: DISCOVER answered without the sink; configurations of the sink rejected; an answer that could not be sent */
	uint32_t n_acc_discover, n_acc_caps, n_acc_config, n_acc_reject, n_acc_stock_no, n_acc_cmd, n_acc_sbc, n_trim, n_trimmed_units;
} BtAacNeg;

extern volatile BtAacNeg btaac_neg;   /* kernel/btaac_neg.c */
extern volatile BtCdcRec btaac_cdc;   /* the codec list being read (btcodecs.h; kernel/btaac_neg.c) */
extern int16_t btaac_apt_pcm[1024];   /* aptX: the last block the sender handed over (512 frames, stereo), encoded by the media hook only when it sends a packet */

/* 1 if `c` is one of the Bluetooth code's link slots. */
static inline int btaac_conn_ok(uintptr_t c)
{
	if (!btaac_neg.conn_lo)
		return 0;
	for (uintptr_t p = btaac_neg.conn_lo; p < btaac_neg.conn_hi; p += BTAAC_SLOT_SIZE)
		if (p == c)
			return 1;
	return 0;
}

static inline uint32_t btaac_rd32(uintptr_t conn, uint32_t off)
{
	return *(const volatile uint32_t *)(conn + off);
}

/* Both kinds of mark of the device with this address, as the table holds them (the headphones' in the low bits, the module's own shifted up); 0 when none. */
static inline uint32_t btaac_ban_word(uint32_t lo, uint32_t hi)
{
	uint32_t r = 0;

	for (uint32_t i = 0; i < BTAAC_BAN_N; i++)
		if (btaac_neg.ban_n > i && btaac_neg.ban_lo[i] == lo && btaac_neg.ban_hi[i] == (hi & 0xFFFFu))
			r |= btaac_neg.ban_cdc[i];
	return r;
}

/* The codecs (BTAAC_BAN_*) the headphones with this address refused or broke this boot; 0 when none. The module's own marks are not here (btaac_local_bits). */
static inline uint32_t btaac_ban_bits(uint32_t lo, uint32_t hi)
{
	return btaac_ban_word(lo, hi) & BTAAC_BAN_ALL;
}

/* The codecs (BTAAC_BAN_*) that are not offered to the device this boot because the module's own engine or add-on could not start a session; 0 when none. */
static inline uint32_t btaac_local_bits(uint32_t lo, uint32_t hi)
{
	return (btaac_ban_word(lo, hi) >> BTAAC_LOCAL_SHIFT) & BTAAC_BAN_ALL;
}

/* The codec being offered (the Bluetooth Codec setting, when its module is usable) as a mark names it; AAC when none of the others is. */
static inline uint32_t btaac_want_bit(void)
{
	return btaac_neg.want_ldac ? BTAAC_BAN_LDAC : btaac_neg.want_aptx ? BTAAC_BAN_APTX : BTAAC_BAN_AAC;
}

/* The codec the negotiation of this connection is for (set when its endpoint is chosen or configured), as a mark names it. */
static inline uint32_t btaac_neg_bit(void)
{
	return btaac_neg.ldac ? BTAAC_BAN_LDAC : btaac_neg.aptx ? BTAAC_BAN_APTX : BTAAC_BAN_AAC;
}

/* 1 if the device of this link was marked, this boot, for the codec now being offered to it (by the headphones' doing or the module's own). A mark for another codec does not count. */
static inline int btaac_banned(uintptr_t conn)
{
	uint32_t w = btaac_ban_word(btaac_rd32(conn, BTAAC_C_ADDR_LO), btaac_rd32(conn, BTAAC_C_ADDR_HI));

	return (((w | (w >> BTAAC_LOCAL_SHIFT)) & BTAAC_BAN_ALL) & btaac_want_bit()) != 0;
}

/* Marks the device of this link for the codec `bit` (BTAAC_BAN_*): not offered it again this boot. A device already marked for another codec keeps its place and gains the bit. */
static inline void btaac_ban_for(uintptr_t conn, uint32_t bit)
{
	uint32_t lo = btaac_rd32(conn, BTAAC_C_ADDR_LO), hi = btaac_rd32(conn, BTAAC_C_ADDR_HI) & 0xFFFFu;
	uint32_t n, i;

	for (i = 0; i < BTAAC_BAN_N; i++)
		if (btaac_neg.ban_n > i && btaac_neg.ban_lo[i] == lo && btaac_neg.ban_hi[i] == hi && btaac_neg.ban_cdc[i]) {
			__atomic_fetch_or(&btaac_neg.ban_cdc[i], bit, __ATOMIC_RELEASE);
			return;
		}
	n = __atomic_fetch_add(&btaac_neg.ban_n, 1u, __ATOMIC_RELAXED);
	i = n & (BTAAC_BAN_N - 1u);   /* a power of two: no division */
	__atomic_store_n(&btaac_neg.ban_cdc[i], 0u, __ATOMIC_RELAXED);   /* the entry counts for nothing while it is written */
	btaac_neg.ban_lo[i] = lo;
	btaac_neg.ban_hi[i] = hi;
	__atomic_store_n(&btaac_neg.ban_cdc[i], bit, __ATOMIC_RELEASE);
}

/* Marks the device of this link, for the codec `bit` (BTAAC_BAN_*), as not offered it again this boot for a reason of the module's own. */
static inline void btaac_local_for(uintptr_t conn, uint32_t bit)
{
	btaac_ban_for(conn, bit << BTAAC_LOCAL_SHIFT);
}

/* The codec this connection was negotiated for is marked for the device. */
static inline void btaac_ban(uintptr_t conn)
{
	btaac_ban_for(conn, btaac_neg_bit());
}

/* The codec this connection was configured for failed (rejected by the headphones, broke, or its start was never accepted): the device is not offered it again this boot.
 * aptX HD is experimental and has a way back of its own: its failure turns HD off for the boot and leaves the device its classic aptX. Called only for a failure the headphones
 * or the stream caused, never for one of the module's own installing or checking. */
static inline void btaac_ban_codec(uintptr_t conn)
{
	if (btaac_neg.aptx && btaac_neg.apt_hd)
		btaac_neg.hd_off = 1;
	else
		btaac_ban(conn);
}

/* The engine or add-on of the codec this connection was configured for could not start a session (the module's own failure, not the headphones'): the device is not offered the codec
 * again this boot, so that a reconnect does not negotiate it and fall silent again, but no refusal is recorded (btaac_local_bits). aptX HD, as for btaac_ban_codec, goes off for the boot
 * and the device keeps its plain aptX. */
static inline void btaac_local_codec(uintptr_t conn)
{
	if (btaac_neg.aptx && btaac_neg.apt_hd)
		btaac_neg.hd_off = 1;
	else
		btaac_local_for(conn, btaac_neg_bit());
}

/* The largest packet the sink's L2CAP channel takes, as the Bluetooth code keeps it (the media channel's MTU), made safe (btaac_mtu_sane); the L2CAP default when it cannot be read. */
uint32_t btaac_media_mtu(uintptr_t conn);

/* H2: called by the connection quality hook after the original routine returned, with the arguments it was called with. When the stream
 * is being configured as the source (flag 8) on the connection whose sink offered AAC, state PROBING, the record in state 4 and the
 * capability buffer being the record's own, the code's SBC choice is saved and the record is rewritten: the sink's endpoint, the 12
 * configuration bytes and their length. Anything else leaves the connection as SBC (and the state NONE, so a later answer is not mistaken
 * for ours). Writes nothing at all unless it rewrites. */
static inline void btaac_h2_after(void *connp, uint32_t flags, void *capsp, uint32_t len)
{
	uintptr_t conn = (uintptr_t)connp, rec = conn + BTAAC_C_REC_B;
	uint8_t *rb = (uint8_t *)rec;
	BtAacSink sink;
	uint32_t bps, n;

	if (!connp || flags != 8u || conn != btaac_neg.conn || btaac_neg.state != BTAAC_N_PROBING || !btaac_conn_ok(conn))
		return;
	if ((uintptr_t)capsp != (uintptr_t)(rb + 6) || btaac_rd32(conn, BTAAC_C_REC_B) != 4u || !btaac_neg.have || !btaac_neg.enable ||
	    btaac_banned(conn) || len > BTAAC_CAPS_BUF) {
		btaac_neg.state = BTAAC_N_NONE;
		btaac_neg.n_declined++;
		return;
	}
	sink.seid = (uint8_t)btaac_neg.seid;
	sink.object = (uint8_t)btaac_neg.object;
	sink.vbr = (uint8_t)btaac_neg.vbr;
	sink.max_bps = btaac_neg.max_bps;
	bps = btaac_neg.ldac ? 0u : btaac_neg.aptx ? (btaac_neg.apt_hd ? BTAPTX_HD_KBPS : BTAPTX_KBPS) * 1000u : btaac_sep_bps(&sink, btaac_neg.kbps);
	/* what the code chose, for the fallback */
	btaac_neg.sbc_seid = rb[5];
	btaac_neg.sbc_len = *(volatile uint32_t *)(rb + 0x18);
	for (n = 0; n < BTAAC_CAPS_BUF; n++)
		btaac_neg.sbc_caps[n] = rb[6 + n];
	if (btaac_neg.ldac)
		btldac_sep_config((uint8_t *)btaac_neg.config);
	else if (btaac_neg.aptx && btaac_neg.apt_hd)
		btaptx_sep_config_hd((uint8_t *)btaac_neg.config);
	else if (btaac_neg.aptx)
		btaptx_sep_config((uint8_t *)btaac_neg.config);
	else
		btaac_sep_config(&sink, bps, (uint8_t *)btaac_neg.config);
	btaac_neg.config_len = btaac_neg.ldac ? BTLDAC_CONFIG_LEN : btaac_neg.aptx ? (btaac_neg.apt_hd ? BTAPTX_HD_CONFIG_LEN : BTAPTX_CONFIG_LEN) : BTAAC_CONFIG_LEN;
	for (n = 0; n < btaac_neg.config_len; n++)
		rb[6 + n] = btaac_neg.config[n];
	rb[5] = sink.seid;
	*(volatile uint32_t *)(rb + 0x18) = btaac_neg.config_len;
	btaac_neg.bps = bps;
	btaac_neg.n_rewrite++;
	__atomic_store_n(&btaac_neg.state, (uint32_t)BTAAC_N_PENDING, __ATOMIC_RELEASE);
}

/* The audio sender's hook (bteq_hook.c) hands each block of PCM here, after the sound filter: it goes to the engine only while the
 * stream is AAC, its session is running, and the stream is started (before that, the Bluetooth code throws the blocks away itself). */
static inline void btaac_pcm_block(const int16_t *pcm)
{
	uintptr_t c = btaac_neg.conn;
	uint32_t started;

	if (!btaac_neg.pcm_gate || btaac_neg.state != BTAAC_N_CONFIGURED || !c)
		return;
	started = (btaac_rd32(c, BTAAC_C_PROFILES) & BTAAC_PROF_STARTED) != 0;
	if (!started)
		return;
	if (btaac_neg.ldac) {   /* LDAC: the block goes to the engine's PCM ring (four encoder calls), the packets come back through the media hook (send_ldac) */
		if (btt_ldac_push_pcm(pcm, BTLDAC_BLOCK_FRAMES))
			btaac_neg.n_pcm_blocks++;
		return;
	}
	if (btaac_neg.aptx) {
		/* aptX: the block is kept (copied: the sender goes on to use its own) and encoded by the media hook, in this same thread, only if that sends a packet for it: a
		 * block the Bluetooth code drops (congestion) never reaches the encoder, so the encoder's state is exactly what the sink has been sent */
		typedef uint32_t __attribute__((may_alias)) word_t;   /* the samples are read and written as words: no aliasing question for the compiler */
		const word_t *s = (const word_t *)pcm;
		word_t *d = (word_t *)btaac_apt_pcm;

		if (btaac_neg.apt_ready)
			btaac_neg.n_apt_late++;
		for (uint32_t i = 0; i < 512u; i++)
			d[i] = s[i];
		btaac_neg.apt_ready = 1;
		btaac_neg.n_apt_blocks++;
		return;
	}
	if (btt_aac_push_pcm(pcm, 512u))
		btaac_neg.n_pcm_blocks++;
}

#endif /* BTAAC_HOOK_H */
