/*
 * btfall_core.h - the codec fallback notification's rules and texts. Header only, plain integer C, no system call: the host test (test/test_btfall.c)
 * compiles all of it as it is.
 *
 * When the Bluetooth Codec setting names AAC, aptX or LDAC and a connection uses SBC instead, the console says so once, with a system notification:
 *   - "Your headphones don't support <codec>. Using SBC." when the headphones' codec list is known and does not hold it;
 *   - "Your headphones couldn't use <codec>. Using SBC." when they refused it, the START was rejected until the retries gave up, or the stream never started
 *     (the stream code, btaac_stream.c, reports those with the headphones' address).
 * A headset that refused one codec is marked for that codec and is not offered it again until the console restarts; another codec it lists is still offered (the mark is
 * per codec). Only a mark of the codec on offer is told as "could not use it".
 * Once for each headset and codec, until the console restarts; the count starts again when the Bluetooth Codec setting changes. The kernel module decides (the
 * worker's pass, never a hook), keeps the notices in a small queue and hands the text to the Home screen module (user/main.c), which shows it through the same
 * system call as the low battery notice: bttGetNotice. The texts are kept here, in the kernel module, so the Home screen module (which has little memory)
 * does not carry them.
 */
#ifndef BTFALL_CORE_H
#define BTFALL_CORE_H

#include <stdint.h>

#include "../include/bluetoothturbo.h"

#include "btfall_codec.h"

#define BTFALL_TABLE   16    /* (headset, codec) pairs told this boot: the console remembers 16 headsets */
#define BTFALL_QUEUE   4     /* notices waiting for the Home screen module (it asks every two seconds while headphones are connected, every 30 seconds otherwise); a fifth is dropped */
#define BTFALL_LANGS   20
#define BTFALL_TEXT    192   /* the largest text with the codec's name put in, UTF-8 bytes, with the NUL */

typedef struct BtFallNotice {
	uint8_t kind, codec;
} BtFallNotice;

typedef struct BtFall {
	int      setting;                       /* the Bluetooth Codec setting last seen, -1 before the first look */
	uint32_t n;                             /* pairs told: the first `n` (at most BTFALL_TABLE) of the three arrays; a full table is written round and round */
	uint32_t lo[BTFALL_TABLE], hi[BTFALL_TABLE];
	uint8_t  codec[BTFALL_TABLE];
	BtFallNotice q[BTFALL_QUEUE];
	volatile uint32_t head, tail;           /* the worker writes q[head] and moves head; the syscall takes q[tail] and moves tail */
	uint32_t dropped;                       /* notices lost to a full queue */
} BtFall;

/* What one worker pass saw. */
typedef struct BtFallIn {
	int on;                                 /* the master switch                                                                                           */
	int setting;                            /* the Bluetooth Codec key, a BttCodec: what changes the count                                                   */
	int offered;                            /* the codec the console is offering to new connections (BttCodec): SBC when the key is SBC or the add-on cannot be used */
	int hd_pref;                            /* aptX HD is preferred to plain aptX (so headphones that list only HD can still be given aptX)                  */
	int connected;                          /* the A2DP link is up                                                                                         */
	uint32_t addr_lo, addr_hi;              /* the headphones' address                                                                                     */
	uint32_t codec_mask;                    /* the codecs the headphones list (BTT_HC_*), 0 when not known                                                   */
	int link_codec;                         /* BTT_LINK_CODEC_*: the codec the link uses                                                                    */
	uint32_t banned;                        /* the codecs the headphones were marked for this boot (a refusal earlier: btaac_ban), one bit each by the BTFALL_C_* number: those are not offered to them  */
	int fail_codec;                         /* a BTFALL_C_* the stream code reports as refused or given up on since the last pass (0: none)                */
	uint32_t fail_lo, fail_hi;              /* ... and the address of the headphones it happened with                                                      */
} BtFallIn;

static inline void btfall_init(BtFall *s)
{
	s->setting = -1;
	s->n = 0;
	s->head = s->tail = 0;
	s->dropped = 0;
}

static inline int btfall_seen(const BtFall *s, uint32_t lo, uint32_t hi, int codec)
{
	uint32_t n = s->n < BTFALL_TABLE ? s->n : BTFALL_TABLE;

	for (uint32_t i = 0; i < n; i++)
		if (s->lo[i] == lo && s->hi[i] == hi && s->codec[i] == (uint8_t)codec)
			return 1;
	return 0;
}

static inline void btfall_note(BtFall *s, uint32_t lo, uint32_t hi, int codec)
{
	uint32_t i = s->n % BTFALL_TABLE;

	s->lo[i] = lo;
	s->hi[i] = hi;
	s->codec[i] = (uint8_t)codec;
	s->n++;
}

/* Queues a notice (the worker only). Returns 1 when it was queued, 0 when the queue was full. */
static inline int btfall_push(BtFall *s, int kind, int codec)
{
	uint32_t h = s->head;

	if (h - __atomic_load_n(&s->tail, __ATOMIC_ACQUIRE) >= BTFALL_QUEUE) {
		s->dropped++;
		return 0;
	}
	s->q[h % BTFALL_QUEUE].kind = (uint8_t)kind;
	s->q[h % BTFALL_QUEUE].codec = (uint8_t)codec;
	__atomic_store_n(&s->head, h + 1u, __ATOMIC_RELEASE);
	return 1;
}

/* Takes the oldest waiting notice (any thread; two callers never get the same one). 1 when there was one. */
static inline int btfall_pop(BtFall *s, BtFallNotice *out)
{
	for (;;) {
		uint32_t t = __atomic_load_n(&s->tail, __ATOMIC_ACQUIRE);

		if (t == __atomic_load_n(&s->head, __ATOMIC_ACQUIRE))
			return 0;
		*out = s->q[t % BTFALL_QUEUE];
		if (__atomic_compare_exchange_n(&s->tail, &t, t + 1u, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return 1;
	}
}

/* The codec the offer is for, as a notice names it, and the codec-list bits that would let the headphones take it. 0 for SBC. */
static inline int btfall_offered_codec(const BtFallIn *in, uint32_t *bits)
{
	switch (in->offered) {
	case BTT_CODEC_AAC:
		*bits = BTT_HC_AAC;
		return BTFALL_C_AAC;
	case BTT_CODEC_APTX:
		*bits = BTT_HC_APTX | (in->hd_pref ? BTT_HC_APTX_HD : 0u);
		return BTFALL_C_APTX;
	case BTT_CODEC_LDAC:
		*bits = BTT_HC_LDAC;
		return BTFALL_C_LDAC;
	}
	*bits = 0;
	return BTFALL_C_NONE;
}

/* One worker pass. Queues at most one notice for a refusal and one for a missing codec. Returns how many it queued. */
static inline int btfall_step(BtFall *s, const BtFallIn *in)
{
	int queued = 0;
	uint32_t bits = 0;
	int c;

	if (in->setting != s->setting) {        /* the codec setting changed: what was told is told again, if it is still so */
		s->setting = in->setting;
		s->n = 0;
	}
	if (!in->on)
		return 0;
	if (in->fail_codec > BTFALL_C_NONE && in->fail_codec < BTFALL_C_COUNT && !btfall_seen(s, in->fail_lo, in->fail_hi, in->fail_codec)) {
		btfall_note(s, in->fail_lo, in->fail_hi, in->fail_codec);
		queued += btfall_push(s, BTFALL_K_REFUSED, in->fail_codec);
	}
	c = btfall_offered_codec(in, &bits);
	if (c != BTFALL_C_NONE && in->connected && in->link_codec == BTT_LINK_CODEC_SBC && !btfall_seen(s, in->addr_lo, in->addr_hi, c)) {
		if (in->codec_mask != 0 && !(in->codec_mask & bits)) {
			btfall_note(s, in->addr_lo, in->addr_hi, c);
			queued += btfall_push(s, BTFALL_K_UNSUPPORTED, c);
		} else if (in->banned & (1u << c)) {
			btfall_note(s, in->addr_lo, in->addr_hi, c);
			queued += btfall_push(s, BTFALL_K_REFUSED, c);
		}
	}
	return queued;
}

static inline const char *btfall_codec_name(int codec)
{
	switch (codec) {
	case BTFALL_C_AAC:     return "AAC";
	case BTFALL_C_APTX:    return "aptX";
	case BTFALL_C_APTX_HD: return "aptX HD";
	case BTFALL_C_LDAC:    return "LDAC";
	}
	return "";
}

#include "bluetoothturbo_fallback_table.h"

/* The notice's text for a system language (0..19, English for anything else): the table's row with {c} replaced by the codec's name. Returns the length in
 * bytes (without the NUL), or -1 for an unknown kind or codec or a buffer too small. */
static inline int btfall_text(int lang, int kind, int codec, char *out, int size)
{
	const char *t, *name = btfall_codec_name(codec);
	int n = 0;

	if (kind < 0 || kind >= BTFALL_K_COUNT || !name[0] || !out || size < 2)
		return -1;
	if (lang < 0 || lang >= BTFALL_LANGS)
		lang = 1;
	for (t = g_fallback_text[lang][kind]; *t; t++) {
		if (t[0] == '{' && t[1] == 'c' && t[2] == '}') {
			for (const char *p = name; *p; p++) {
				if (n >= size - 1)
					return -1;
				out[n++] = *p;
			}
			t += 2;
		} else {
			if (n >= size - 1)
				return -1;
			out[n++] = *t;
		}
	}
	out[n] = 0;
	return n;
}

#endif
