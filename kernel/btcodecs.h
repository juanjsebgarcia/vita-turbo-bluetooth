/*
 * btcodecs.h - what codecs a pair of headphones lists, and where that is kept. Header only, plain integer C, no system call, so the signalling
 * hook (kernel/btaac_hook.c) runs the reading part as it is and the host test (test/test_btcodecs.c) compiles all of it as it is.
 *
 * Reading: when the console opens the audio stream it asks each of the headphones' stream endpoints what it offers (AVDTP GET_CAPABILITIES). Every
 * endpoint's reply holds one media codec element: media type, codec type and, for a vendor's codec, the vendor and its codec number. They are turned
 * into one BTT_HC_* bit each (btcdc_scan_caps); the bits of every audio sink endpoint make the headphones' codec list.
 *
 * Keeping: a list is kept per device address, up to 16 devices (the pairing limit), in a small file. A link the headphones open themselves is never
 * asked, so the console shows what it learned the last time it opened one.
 *
 * The codec numbers are the ones public A2DP codec tables give for each codec's vendor and codec identifiers (the BlueZ, PipeWire and BlueALSA tables, and the
 * public open-source Bluetooth stacks for LHDC's feature bits).
 */
#ifndef BTCODECS_H
#define BTCODECS_H

#include <stdint.h>

#include "../include/bluetoothturbo.h"

#define BTCDC_CAT_CODEC   7u       /* the media codec service category */
#define BTCDC_TYPE_SBC    0x00u
#define BTCDC_TYPE_MPEG12 0x01u
#define BTCDC_TYPE_AAC    0x02u
#define BTCDC_TYPE_ATRAC  0x04u
#define BTCDC_TYPE_VENDOR 0xFFu

/* LHDC's third generation (codec 0x4C33) is three codecs under one number: its capability bytes tell them apart. `p7` and `p8` are the two bytes after
 * the codec number and the first (sample rate) byte; the high bit of the first says low latency LLAC, the high bit of the second says version 4, and with
 * neither it is version 3. A sink lists what it can do, so both bits may be set: both are named. */
#define BTCDC_LHDC3_LLAC 0x80u
#define BTCDC_LHDC3_V4   0x80u
static inline uint32_t btcdc_lhdc3(uint32_t p7, uint32_t p8)
{
	uint32_t m = ((p7 & BTCDC_LHDC3_LLAC) ? BTT_HC_LHDC_LLAC : 0u) | ((p8 & BTCDC_LHDC3_V4) ? BTT_HC_LHDC_V4 : 0u);

	return m ? m : BTT_HC_LHDC_V3;
}

/* One codec as the element names it -> a BTT_HC_* bit (more than one for LHDC 0x4C33 when its capability bytes are given: `ci`, `n` of them, the bytes
 * after the codec number: sample rates first; NULL when not known). `vendor` and `codec` matter for a vendor codec only. A vendor identifier that is not a 16 bit company
 * number (the upper half is not 0) names nothing this code knows. A codec only one vendor's own software is known to use, or that public tables give
 * no number for, is "other": a wrong name is a public claim about someone's hardware. */
static inline uint32_t btcdc_classify_caps(uint32_t type, uint32_t vendor, uint32_t codec, const uint8_t *ci, uint32_t n)
{
	switch (type) {
	case BTCDC_TYPE_SBC:    return BTT_HC_SBC;
	case BTCDC_TYPE_MPEG12: return BTT_HC_MP3;
	case BTCDC_TYPE_AAC:    return BTT_HC_AAC;
	case BTCDC_TYPE_ATRAC:  return BTT_HC_ATRAC;
	case BTCDC_TYPE_VENDOR: break;
	default:                return BTT_HC_OTHER;
	}
	if (vendor > 0xFFFFu)
		return BTT_HC_OTHER;
	switch (vendor) {
	case 0x004Fu:   /* the first aptX company number */
		return codec == 0x0001u ? BTT_HC_APTX : BTT_HC_OTHER;
	case 0x00D7u:   /* the aptX family's later company number: HD, Adaptive, true wireless; the low latency one is also listed here by some software */
		return codec == 0x0024u ? BTT_HC_APTX_HD : codec == 0x00ADu ? BTT_HC_APTX_ADAPTIVE : codec == 0x0025u ? BTT_HC_APTX_TWS :
		       codec == 0x0002u ? BTT_HC_APTX_LL : BTT_HC_OTHER;
	case 0x000Au:   /* the older company number: FastStream and the low latency aptX */
		return codec == 0x0002u ? BTT_HC_APTX_LL : codec == 0x0001u ? BTT_HC_FASTSTREAM : BTT_HC_OTHER;
	case 0x012Du:
		return codec == 0x00AAu ? BTT_HC_LDAC : BTT_HC_OTHER;
	case 0x053Au:   /* LHDC: any other number of that vendor is not named */
		switch (codec) {
		case 0x484Cu: return BTT_HC_LHDC_V1;
		case 0x4C32u: return BTT_HC_LHDC_V2;
		case 0x4C33u: return (ci && n >= 3u) ? btcdc_lhdc3(ci[1], ci[2]) : BTT_HC_LHDC;   /* v3, v4 or LLAC: by the feature bits when the element has them */
		case 0x4C35u: return BTT_HC_LHDC_V5;
		case 0x4C4Cu: return BTT_HC_LHDC_LL;
		default:      return BTT_HC_OTHER;
		}
	case 0x0075u:   /* Samsung: two numbers are known, any other is not named */
		return codec == 0x0103u ? BTT_HC_SAMSUNG_SCALABLE : codec == 0x0102u ? BTT_HC_SAMSUNG_HD : BTT_HC_OTHER;
	case 0x08A9u:
		return codec == 0x0001u ? BTT_HC_LC3PLUS : BTT_HC_OTHER;
	case 0x00E0u:
		return codec == 0x0001u ? BTT_HC_OPUS : BTT_HC_OTHER;
	case 0x05F1u:   /* the open source audio server's own Opus, between computers */
		return codec == 0x1005u ? BTT_HC_OPUS : BTT_HC_OTHER;
	}
	return BTT_HC_OTHER;
}

/* The same, from the numbers alone (an LHDC 0x4C33 then reads "LHDC", its version not known). */
static inline uint32_t btcdc_classify(uint32_t type, uint32_t vendor, uint32_t codec)
{
	return btcdc_classify_caps(type, vendor, codec, 0, 0);
}

/* The codec bit of one endpoint's capabilities (`n` bytes: the list of {category, length, length bytes} after GET_CAPABILITIES' two header bytes).
 * Returns 0 when the list holds no audio codec element (another media type, no element, or a list that does not hold together: an element running past
 * `n`). Never reads past `n`. */
static inline uint32_t btcdc_scan_caps(const uint8_t *caps, uint32_t n)
{
	uint32_t i = 0;

	while (i + 2u <= n) {
		uint32_t cat = caps[i], losc = caps[i + 1u];

		if (i + 2u + losc > n)
			return 0;
		if (cat == BTCDC_CAT_CODEC) {
			const uint8_t *d = caps + i + 2u;

			if (losc < 2u || (d[0] >> 4) != 0u)
				return 0;                                  /* not audio (or no codec named) */
			if (d[1] != BTCDC_TYPE_VENDOR)
				return btcdc_classify(d[1], 0, 0);
			if (losc < 8u)
				return BTT_HC_OTHER;                        /* a vendor codec without its number */
			/* the bytes after the codec number: d[8] on (an LHDC 0x4C33 element is 11 long: sample rates, then the two feature bytes) */
			return btcdc_classify_caps(BTCDC_TYPE_VENDOR, (uint32_t)d[2] | ((uint32_t)d[3] << 8) | ((uint32_t)d[4] << 16) | ((uint32_t)d[5] << 24),
			                           (uint32_t)d[6] | ((uint32_t)d[7] << 8), d + 8, losc - 8u);
		}
		i += 2u + losc;
	}
	return 0;
}

/* An endpoint as DISCOVER lists it (two bytes): is it an audio sink, which is the role that decodes what is sent. In use or not does not matter here. */
static inline int btcdc_sep_is_audio_sink(const uint8_t info[2])
{
	return (info[1] >> 4) == 0u && ((info[1] >> 3) & 1u);
}

/* ---- what the signalling hook is reading right now, and what it finished ------------------------------------------------------------------------------ */

#define BTCDC_MAX_SEP 8u    /* the Bluetooth code keeps at most 16 bytes of the endpoint list: 8 endpoints */

typedef struct BtCdcRec {
	uint32_t on;                       /* the worker's: record (the hook that reads the replies is in) */
	/* the hook's, for the connection being read */
	uintptr_t conn;                    /* 0: none being read */
	uint32_t nsep, seen, mask;         /* endpoints listed, which of them have answered (a bit each), the codec bits of the audio sinks */
	uint8_t info[BTCDC_MAX_SEP * 2u];  /* the endpoints' two DISCOVER bytes each */
	/* published when every listed endpoint has answered: the worker takes it when done_seq changes */
	uint32_t done_lo, done_hi, done_mask;
	uint32_t done_seq;
	uint32_t n_read, n_done;           /* counters, for the log */
} BtCdcRec;

/* ---- the file ------------------------------------------------------------------------------------------------------------------------------------ */

#define BTCDC_MAGIC       0x4C435442u   /* "BTCL" */
#define BTCDC_FILE_VER    2u            /* 2: LLAC, LHDC and the Samsung bits changed meaning (the numbers they stand for were corrected) */
#define BTCDC_STORE_MAX   16u           /* the pairing limit */
#define BTCDC_FILE_MAX    (12u + BTCDC_STORE_MAX * 12u + 4u)

typedef struct BtCdcEntry { uint32_t lo, hi, mask, age; } BtCdcEntry;   /* mask 0: a free slot */
typedef struct BtCdcStore { BtCdcEntry e[BTCDC_STORE_MAX]; uint32_t clock; } BtCdcStore;

static inline void btcdc_store_clear(BtCdcStore *s)
{
	for (uint32_t i = 0; i < BTCDC_STORE_MAX; i++)
		s->e[i] = (BtCdcEntry){ 0, 0, 0, 0 };
	s->clock = 0;
}

/* The codec bits kept for a device; 0 when none are. */
static inline uint32_t btcdc_store_get(const BtCdcStore *s, uint32_t lo, uint32_t hi)
{
	for (uint32_t i = 0; i < BTCDC_STORE_MAX; i++)
		if (s->e[i].mask && s->e[i].lo == lo && s->e[i].hi == (hi & 0xFFFFu))
			return s->e[i].mask;
	return 0;
}

/* Keeps a device's list (a mask of 0 is not a list: nothing is kept). The same device takes its slot again; a new one takes a free slot, or the one
 * written longest ago. Returns 1 when what is kept changed (the file needs writing). */
static inline int btcdc_store_put(BtCdcStore *s, uint32_t lo, uint32_t hi, uint32_t mask)
{
	uint32_t pick = 0;
	int found = 0;

	if (!mask)
		return 0;
	hi &= 0xFFFFu;
	for (uint32_t i = 0; i < BTCDC_STORE_MAX; i++)
		if (s->e[i].mask && s->e[i].lo == lo && s->e[i].hi == hi) {
			pick = i;
			found = 1;
			break;
		}
	if (found && s->e[pick].mask == mask)
		return 0;                       /* the same list again: nothing to write, and not made newer (the file keeps no more than this) */
	if (!found) {
		int free_slot = 0;

		for (uint32_t i = 0; i < BTCDC_STORE_MAX; i++)
			if (!s->e[i].mask) {
				pick = i;
				free_slot = 1;
				break;
			}
		if (!free_slot)
			for (uint32_t i = 1; i < BTCDC_STORE_MAX; i++)
				if (s->e[i].age < s->e[pick].age)
					pick = i;
	}
	{
		int changed = !found || s->e[pick].mask != mask;

		s->e[pick].lo = lo;
		s->e[pick].hi = hi;
		s->e[pick].mask = mask;
		s->e[pick].age = ++s->clock;
		return changed;
	}
}

static inline uint32_t btcdc_fnv(const uint8_t *p, uint32_t n)
{
	uint32_t h = 2166136261u;

	for (uint32_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static inline void btcdc_put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static inline uint32_t btcdc_get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* The file's bytes: magic, version, count, then each kept device (address low, high, mask) oldest first, then a checksum of everything before it. Into `out`
 * (BTCDC_FILE_MAX bytes at most); the length. */
static inline uint32_t btcdc_store_save(const BtCdcStore *s, uint8_t *out)
{
	uint32_t n = 0, w = 12, last = 0;

	for (uint32_t i = 0; i < BTCDC_STORE_MAX; i++)
		if (s->e[i].mask)
			n++;
	btcdc_put32(out, BTCDC_MAGIC);
	btcdc_put32(out + 4, BTCDC_FILE_VER);
	btcdc_put32(out + 8, n);
	for (uint32_t k = 0; k < n; k++) {        /* the next oldest each time: ages are unique */
		uint32_t pick = BTCDC_STORE_MAX, best = 0;

		for (uint32_t i = 0; i < BTCDC_STORE_MAX; i++)
			if (s->e[i].mask && s->e[i].age > last && (pick == BTCDC_STORE_MAX || s->e[i].age < best)) {
				pick = i;
				best = s->e[i].age;
			}
		if (pick == BTCDC_STORE_MAX)
			break;
		last = best;
		btcdc_put32(out + w, s->e[pick].lo);
		btcdc_put32(out + w + 4, s->e[pick].hi);
		btcdc_put32(out + w + 8, s->e[pick].mask);
		w += 12;
	}
	btcdc_put32(out + 8, (w - 12u) / 12u);
	btcdc_put32(out + w, btcdc_fnv(out, w));
	return w + 4u;
}

/* Reads a file back (the store is left empty for anything that does not hold together: another magic or version, a count over the limit, a length or a
 * checksum that is wrong, a device twice, a mask of 0). Returns 1 when the file was used. */
static inline int btcdc_store_load(BtCdcStore *s, const uint8_t *in, uint32_t len)
{
	uint32_t n;

	btcdc_store_clear(s);
	if (len < 16u || btcdc_get32(in) != BTCDC_MAGIC || btcdc_get32(in + 4) != BTCDC_FILE_VER)
		return 0;
	n = btcdc_get32(in + 8);
	if (n > BTCDC_STORE_MAX || len != 16u + 12u * n || btcdc_get32(in + 12u + 12u * n) != btcdc_fnv(in, 12u + 12u * n))
		return 0;
	for (uint32_t k = 0; k < n; k++) {
		const uint8_t *e = in + 12u + 12u * k;
		uint32_t lo = btcdc_get32(e), hi = btcdc_get32(e + 4), mask = btcdc_get32(e + 8);

		if (!mask || (hi & ~0xFFFFu) || btcdc_store_get(s, lo, hi)) {
			btcdc_store_clear(s);
			return 0;
		}
		btcdc_store_put(s, lo, hi, mask);
	}
	return 1;
}

#endif /* BTCODECS_H */
