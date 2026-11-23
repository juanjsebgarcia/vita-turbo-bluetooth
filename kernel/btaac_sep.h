/*
 * btaac_sep.h - the decisions of AAC negotiation that need no system call: which of a sink's stream endpoints is the one to send
 * AAC to, and the configuration bytes that choice becomes. Header only, plain integer C, general registers only, so the hooks in
 * kernel/btaac_hook.c run exactly this and the host test (test/test_btaac_stream.c) compiles it as it is.
 *
 * What the Bluetooth code does by itself: it asks every endpoint a sink lists for its capabilities and keeps the first free SBC sink
 * (the first of each role in the order of the sink's DISCOVER reply). The hooks add an observer of those replies that keeps the first
 * free sink endpoint offering AAC, and, once the code has narrowed its SBC choice into the configuration it is about to send, replace
 * that configuration with the AAC one built here.
 *
 * Wire format (AVDTP 1.3 service capabilities; A2DP 1.3 for the MPEG-2/4 AAC codec element): a list of {category, length, length bytes}.
 * The media codec element is category 7, length 8 for AAC: media type (audio = 0 in the high nibble), codec type 2, then six bytes
 *   [0] object types: 0x80 MPEG-2 AAC LC, 0x40 MPEG-4 AAC LC (0x20 LTP, 0x10 scalable)
 *   [1] sampling frequencies 8000 .. 44100 (bit 7 .. bit 0: 8000 11025 12000 16000 22050 24000 32000 44100)
 *   [2] bit 7 48000, bit 6 64000, bit 5 88200, bit 4 96000; bit 3 one channel, bit 2 two channels
 *   [3] bit 7 VBR, bits 6..0 and [4] [5]: the bit rate, 23 bits, in bits a second (0 = not stated)
 */
#ifndef BTAAC_SEP_H
#define BTAAC_SEP_H

#include <stdint.h>

#include "btaac_core.h"

#define BTAAC_CAT_TRANSPORT   1u
#define BTAAC_CAT_CODEC       7u
#define BTAAC_CODEC_TYPE_AAC  2u
#define BTAAC_AAC_LOSC        8u        /* media type, codec type, six bytes */
#define BTAAC_OBJ_MPEG2_LC    0x80u
#define BTAAC_OBJ_MPEG4_LC    0x40u
#define BTAAC_CONFIG_LEN      12u       /* media transport (2) + media codec (10): what a configuration is, no content protection */
#define BTAAC_CONFIG_BUF      20u       /* room for the longest configuration we send (aptX HD's 17 bytes) */
#define BTAAC_CAPS_BUF        18u       /* the capability buffer of a stream record */

/* A sink endpoint that offers what we send. */
typedef struct BtAacSink {
	uint8_t  seid;        /* the endpoint's number as AVDTP carries it: SEID in bits 7..2 (the byte SET_CONFIGURATION names it by) */
	uint8_t  object;      /* the object type bit to configure: BTAAC_OBJ_MPEG2_LC, or BTAAC_OBJ_MPEG4_LC if only that is offered     */
	uint8_t  vbr;         /* the sink accepts variable bit rate (we send constant: this is for the log)                            */
	uint32_t max_bps;     /* the sink's highest bit rate; 0 = it does not say                                                      */
} BtAacSink;

/* The two bytes DISCOVER lists an endpoint with: SEID (byte 0 bits 7..2), in use (byte 0 bit 1), media type (byte 1 bits 7..4),
 * role (byte 1 bit 3: 1 = sink). */
static inline int btaac_sep_free_audio_sink(const uint8_t info[2])
{
	return !((info[0] >> 1) & 1u) && (info[1] >> 4) == 0u && ((info[1] >> 3) & 1u);
}

/* Walks the capabilities a sink replied with to its media codec element and says whether it is AAC we can send: an AAC-LC object type
 * (MPEG-2 or MPEG-4), 48 kHz and two channels. `caps` is the reply after its two header bytes, `n` its length. Fills `out` (seid not
 * set here) and returns 1, or returns 0 (nothing filled): another codec, a missing element, a malformed list (an element's length running
 * past the end). Never reads past `n`. */
static inline int btaac_sep_scan(const uint8_t *caps, uint32_t n, BtAacSink *out)
{
	uint32_t i = 0;

	while (i + 2u <= n) {
		uint32_t cat = caps[i], losc = caps[i + 1u];

		if (i + 2u + losc > n)
			return 0;
		if (cat == BTAAC_CAT_CODEC && losc >= BTAAC_AAC_LOSC && (caps[i + 2u] >> 4) == 0u && caps[i + 3u] == BTAAC_CODEC_TYPE_AAC) {
			const uint8_t *c = caps + i + 4u;

			if (!(c[0] & (BTAAC_OBJ_MPEG2_LC | BTAAC_OBJ_MPEG4_LC)) || !(c[2] & 0x80u) || !(c[2] & 0x04u))
				return 0;
			out->object = (c[0] & BTAAC_OBJ_MPEG2_LC) ? BTAAC_OBJ_MPEG2_LC : BTAAC_OBJ_MPEG4_LC;
			out->vbr = (uint8_t)(c[3] >> 7);
			out->max_bps = ((uint32_t)(c[3] & 0x7Fu) << 16) | ((uint32_t)c[4] << 8) | c[5];
			out->seid = 0;
			return 1;
		}
		i += 2u + losc;
	}
	return 0;
}

/* The bit rate we send: 256 kb/s, or the sink's highest if that is lower (btaac_clamp_bps also keeps it in the encoder's range). */
static inline uint32_t btaac_sep_bps(const BtAacSink *s, uint32_t kbps)
{
	return btaac_clamp_bps(kbps * 1000u, 48000u, 2u, s->max_bps);
}

/* The configuration SET_CONFIGURATION carries: media transport, then the media codec element narrowed to one object type, 48 kHz, two
 * channels, constant bit rate (every sink supports it), `bps`. 12 bytes (BTAAC_CONFIG_LEN) into `out`. No content protection: it is
 * optional for a source, and without it the packets carry no protection byte. */
static inline void btaac_sep_config(const BtAacSink *s, uint32_t bps, uint8_t *out)
{
	out[0] = (uint8_t)BTAAC_CAT_TRANSPORT;
	out[1] = 0;
	out[2] = (uint8_t)BTAAC_CAT_CODEC;
	out[3] = (uint8_t)BTAAC_AAC_LOSC;
	out[4] = 0;                                   /* audio */
	out[5] = (uint8_t)BTAAC_CODEC_TYPE_AAC;
	out[6] = s->object;
	out[7] = 0;                                   /* no sampling frequency of the low byte */
	out[8] = 0x84;                                /* 48 kHz, two channels */
	out[9] = (uint8_t)((bps >> 16) & 0x7Fu);      /* VBR bit clear */
	out[10] = (uint8_t)(bps >> 8);
	out[11] = (uint8_t)bps;
}

/* ---- AAC when the headphones open the stream (we are the acceptor) -------------------------------------------------------------
 * Our endpoint 3 (AVDTP's raw SEID byte 0x0C) is an AAC source listed after the code's own two. Its capabilities and the check of a
 * configuration the headphones send for it are pure functions of the bytes, here, so the host test runs them as they are. */
#define BTAAC_SEID_SBC_BYTE     0x04u    /* the code's own source endpoint 1: what every command for endpoint 3 is turned into */
#define BTAAC_SEID_AAC_BYTE     0x0Cu    /* endpoint 3: the AAC source we add                                                    */
#define BTAAC_SEID_SINK_BYTE    0x08u    /* the code's own endpoint 2, an audio sink (the speaker role): left out of the list when the Vita offers sources only */
#define BTAAC_ERR_BAD_ACP_SEID  0x12u    /* the endpoint named is not one this side has                                          */
#define BTAAC_ERR_BAD_LENGTH    0x11u    /* AVDTP error codes (the specification's)                                              */
#define BTAAC_ERR_BAD_CATEGORY  0x17u
#define BTAAC_ERR_INVALID_CAPS  0x1Au
#define BTAAC_ERR_UNSUPPORTED   0x29u    /* the configuration is one we do not send                                              */
#define BTAAC_CONFIG_MAX        16u      /* the code's own limit on the capability bytes of a configuration                        */

/* Checks the capability bytes of a SET_CONFIGURATION for endpoint 3 (`n` of them, at most BTAAC_CONFIG_MAX): media transport, then one
 * media codec element naming AAC with exactly one object type of LC, 48 kHz only, two channels, any bit rate (0 = not stated; VBR is
 * allowed, we send constant). Nothing else is accepted (we advertise nothing else, so a conformant sink adds nothing). Returns 0 and
 * fills `out` (seid not set here), or the AVDTP error to reject with, and in `*cat` the service category it concerns (0: none). */
static inline uint32_t btaac_sep_check_config(const uint8_t *caps, uint32_t n, BtAacSink *out, uint32_t *cat)
{
	uint32_t i = 0, have_t = 0, have_c = 0;

	*cat = 0;
	while (i + 2u <= n) {
		uint32_t c = caps[i], losc = caps[i + 1u];
		const uint8_t *d = caps + i + 2u;

		*cat = c;
		if (i + 2u + losc > n)
			return BTAAC_ERR_BAD_LENGTH;
		if (c == BTAAC_CAT_TRANSPORT) {
			if (losc != 0 || have_t)
				return BTAAC_ERR_BAD_LENGTH;
			have_t = 1;
		} else if (c == BTAAC_CAT_CODEC) {
			if (losc >= 2u && ((d[0] >> 4) != 0u || d[1] != BTAAC_CODEC_TYPE_AAC))
				return BTAAC_ERR_UNSUPPORTED;                  /* another codec (SBC, a vendor's) or media type: we do not send it */
			if (losc != BTAAC_AAC_LOSC || have_c)
				return BTAAC_ERR_BAD_LENGTH;
			if ((d[2] != BTAAC_OBJ_MPEG2_LC && d[2] != BTAAC_OBJ_MPEG4_LC) || d[3] != 0u || (d[4] & 0xF0u) != 0x80u || (d[4] & 0x0Cu) != 0x04u)
				return BTAAC_ERR_UNSUPPORTED;
			out->object = d[2];
			out->vbr = (uint8_t)(d[5] >> 7);
			out->max_bps = ((uint32_t)(d[5] & 0x7Fu) << 16) | ((uint32_t)d[6] << 8) | d[7];
			out->seid = 0;
			have_c = 1;
		} else {
			return BTAAC_ERR_BAD_CATEGORY;
		}
		i += 2u + losc;
	}
	if (i != n) {
		*cat = 0;
		return BTAAC_ERR_BAD_LENGTH;
	}
	if (!have_t || !have_c) {
		*cat = have_t ? BTAAC_CAT_CODEC : BTAAC_CAT_TRANSPORT;
		return BTAAC_ERR_INVALID_CAPS;
	}
	*cat = 0;
	return 0;
}

#endif /* BTAAC_SEP_H */
