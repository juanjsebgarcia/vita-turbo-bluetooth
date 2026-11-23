/*
 * btldac_sep.h - the decisions of LDAC negotiation that need no system call (the counterpart of btaptx_sep.h): which of a sink's endpoints is the one to send LDAC to, the
 * configuration bytes, and what we offer and accept when the headphones open the stream. Header only, plain integer C, so the hooks (kernel/btaac_hook.c) run exactly this
 * and the host test (test/test_btldac_stream.h) compiles it as it is.
 *
 * Wire format (AVDTP 1.3 service capabilities; the vendor codec element of A2DP): category 7, length 10:
 *   media type (audio = 0 in the high nibble), codec type 0xFF (vendor), vendor id (4 bytes, little endian: 0x0000012D), codec id (2 bytes: 0x00AA),
 *   then two bytes: the sampling frequencies (0x20 44.1, 0x10 48, 0x08 88.2, 0x04 96, 0x02 176.4, 0x01 192 kHz) and the channel modes (0x04 mono, 0x02 dual, 0x01 stereo).
 * The bit rate is not in the element: it is the encoder's choice and can change while the stream runs. Sources: the public LDAC header (ldacBT.h) and the
 * public LDAC constants (a2dp_vendor_ldac_constants.h); the configuration sent: 01 00  07 0A 00 FF 2D 01 00 00 AA 00 10 01.
 */
#ifndef BTLDAC_SEP_H
#define BTLDAC_SEP_H

#include <stdint.h>

#include "btaac_sep.h"
#include "btldac_core.h"

#define BTLDAC_CODEC_TYPE_VENDOR  0xFFu
#define BTLDAC_VENDOR_ID          0x0000012Du
#define BTLDAC_CODEC_ID           0x00AAu
#define BTLDAC_LOSC               10u        /* media type, codec type, vendor (4), codec (2), the frequency byte, the channel byte */
#define BTLDAC_FREQ_48K           0x10u
#define BTLDAC_CHAN_STEREO        0x01u
#define BTLDAC_CONFIG_LEN         14u        /* media transport (2) + media codec (12): no content protection */
#define BTLDAC_SEID_BYTE          0x14u      /* our endpoint 5 (AVDTP's raw SEID byte): the LDAC source listed after the code's two, AAC's 3 and aptX's 4 */

/* 1 if the codec element's body `d` (media type onward, BTLDAC_LOSC bytes) names LDAC. */
static inline int btldac_sep_is_ldac(const uint8_t *d)
{
	return (d[0] >> 4) == 0u && d[1] == BTLDAC_CODEC_TYPE_VENDOR &&
	       d[2] == (BTLDAC_VENDOR_ID & 0xFFu) && d[3] == ((BTLDAC_VENDOR_ID >> 8) & 0xFFu) && d[4] == ((BTLDAC_VENDOR_ID >> 16) & 0xFFu) && d[5] == (BTLDAC_VENDOR_ID >> 24) &&
	       d[6] == (BTLDAC_CODEC_ID & 0xFFu) && d[7] == (BTLDAC_CODEC_ID >> 8);
}

/* Walks a sink's capabilities (the reply after its two header bytes, `n` long) to its media codec element and says whether it is LDAC at 48 kHz in stereo. Fills `out`
 * (seid not set here; object, vbr and max_bps are 0). Never reads past `n`. A malformed list (an element running past the end) is no; other codec elements are skipped. */
static inline int btldac_sep_scan(const uint8_t *caps, uint32_t n, BtAacSink *out)
{
	uint32_t i = 0;

	while (i + 2u <= n) {
		uint32_t cat = caps[i], losc = caps[i + 1u];

		if (i + 2u + losc > n)
			return 0;
		if (cat == BTAAC_CAT_CODEC && losc == BTLDAC_LOSC && btldac_sep_is_ldac(caps + i + 2u)) {
			const uint8_t *d = caps + i + 2u;

			if (!(d[8] & BTLDAC_FREQ_48K) || !(d[9] & BTLDAC_CHAN_STEREO))
				return 0;
			out->object = 0;
			out->vbr = 0;
			out->max_bps = 0;
			out->seid = 0;
			return 1;
		}
		i += 2u + losc;
	}
	return 0;
}

/* The configuration SET_CONFIGURATION carries: media transport, then the media codec element narrowed to 48 kHz stereo: 14 bytes into `out`. */
static inline void btldac_sep_config(uint8_t *out)
{
	out[0] = (uint8_t)BTAAC_CAT_TRANSPORT;
	out[1] = 0;
	out[2] = (uint8_t)BTAAC_CAT_CODEC;
	out[3] = (uint8_t)BTLDAC_LOSC;
	out[4] = 0;                                   /* audio */
	out[5] = (uint8_t)BTLDAC_CODEC_TYPE_VENDOR;
	out[6] = (uint8_t)(BTLDAC_VENDOR_ID & 0xFFu);
	out[7] = (uint8_t)((BTLDAC_VENDOR_ID >> 8) & 0xFFu);
	out[8] = (uint8_t)((BTLDAC_VENDOR_ID >> 16) & 0xFFu);
	out[9] = (uint8_t)(BTLDAC_VENDOR_ID >> 24);
	out[10] = (uint8_t)(BTLDAC_CODEC_ID & 0xFFu);
	out[11] = (uint8_t)(BTLDAC_CODEC_ID >> 8);
	out[12] = (uint8_t)BTLDAC_FREQ_48K;
	out[13] = (uint8_t)BTLDAC_CHAN_STEREO;
}

/* Checks the capability bytes of a SET_CONFIGURATION for endpoint 5 (`n` of them, at most BTAAC_CONFIG_MAX): media transport, then one media codec element that is LDAC
 * with exactly 48 kHz and stereo chosen (the one configuration we advertise). Returns 0, or the AVDTP error to reject with and in `*cat` the category it concerns. */
static inline uint32_t btldac_sep_check_config(const uint8_t *caps, uint32_t n, BtAacSink *out, uint32_t *cat)
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
			if (have_c)
				return BTAAC_ERR_BAD_LENGTH;
			if (losc < 2u || (d[0] >> 4) != 0u || d[1] != BTLDAC_CODEC_TYPE_VENDOR)
				return BTAAC_ERR_UNSUPPORTED;                  /* SBC, AAC, another media type: not what endpoint 5 is */
			if (losc != BTLDAC_LOSC)
				return BTAAC_ERR_BAD_LENGTH;
			if (!btldac_sep_is_ldac(d) || d[8] != BTLDAC_FREQ_48K || d[9] != BTLDAC_CHAN_STEREO)
				return BTAAC_ERR_UNSUPPORTED;                  /* another vendor codec, or a frequency or channel mode we do not send */
			out->object = 0;
			out->vbr = 0;
			out->max_bps = 0;
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

#endif /* BTLDAC_SEP_H */
