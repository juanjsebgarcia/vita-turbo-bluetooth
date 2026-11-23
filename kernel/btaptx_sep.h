/*
 * btaptx_sep.h - the decisions of aptX negotiation that need no system call (the counterpart of btaac_sep.h for the second codec): which of a sink's
 * endpoints is the one to send aptX to, the configuration bytes, and what we offer and accept when the headphones open the stream. Header only, plain integer
 * C, so the hooks (kernel/btaac_hook.c) run exactly this and the host test (test/test_btaptx_stream.c) compiles it as it is.
 *
 * Wire format (AVDTP 1.3 service capabilities; the vendor codec element of A2DP): category 7, length 9 for classic aptX:
 *   media type (audio = 0 in the high nibble), codec type 0xFF (vendor), vendor id (4 bytes, little endian: 0x0000004F), codec id (2 bytes: 0x0001),
 *   then one byte: high nibble the sampling frequencies (0x1 48 kHz, 0x2 44.1 kHz, 0x4 32 kHz, 0x8 16 kHz), low nibble the channel modes (0x1 mono, 0x2 stereo).
 * Example capability bytes of an aptX headset: 01 00  07 09 00 FF 4F 00 00 00 01 00 32  04 02 02 00 (44.1 and 48 kHz, stereo; content protection offered).
 */
#ifndef BTAPTX_SEP_H
#define BTAPTX_SEP_H

#include <stdint.h>

#include "btaac_sep.h"
#include "btaptx_core.h"

#define BTAPTX_CODEC_TYPE_VENDOR  0xFFu
#define BTAPTX_VENDOR_ID          0x0000004Fu
#define BTAPTX_CODEC_ID           0x0001u
#define BTAPTX_LOSC               9u         /* media type, codec type, vendor (4), codec (2), the frequency and channel byte */
#define BTAPTX_INFO_48K           0x10u
#define BTAPTX_INFO_STEREO        0x02u
#define BTAPTX_INFO_CONFIG        0x12u      /* what we configure: 48 kHz, stereo */
#define BTAPTX_CONFIG_LEN         13u        /* media transport (2) + media codec (11): no content protection */
#define BTAPTX_SEID_BYTE          0x10u      /* our endpoint 4 (AVDTP's raw SEID byte), the aptX source listed after the code's two and AAC's endpoint 3 */

/* aptX HD (experimental): vendor 0x000000D7, codec 0x0024, and four reserved bytes after the frequency and channel byte: 11 bytes of codec information, 13 with the media
 * type and codec type, a configuration of 17 bytes with the media transport element:  01 00  07 0D 00 FF D7 00 00 00 24 00 12 00 00 00 00.
 * Public descriptions of the structure: BlueZ's profiles/audio/a2dp-codecs.h (a2dp_aptx_hd_t: the classic aptX structure and four reserved bytes), PipeWire's
 * spa/plugins/bluez5/a2dp-codec-caps.h and BlueALSA's src/shared/bluetooth-a2dp.h. Vita-initiated only: the endpoint list we offer a headset that opens the stream
 * stays classic aptX's (a second endpoint would need the either-side path to follow two codecs). */
#define BTAPTX_HD_VENDOR_ID       0x000000D7u
#define BTAPTX_HD_CODEC_ID        0x0024u
#define BTAPTX_HD_LOSC            13u        /* media type, codec type, vendor (4), codec (2), the frequency and channel byte, four reserved bytes */
#define BTAPTX_HD_CONFIG_LEN      17u        /* media transport (2) + media codec (15)                                                              */

/* 1 if the codec element's body `d` (media type onward, BTAPTX_LOSC bytes) names classic aptX. */
static inline int btaptx_sep_is_aptx(const uint8_t *d)
{
	return (d[0] >> 4) == 0u && d[1] == BTAPTX_CODEC_TYPE_VENDOR &&
	       d[2] == (BTAPTX_VENDOR_ID & 0xFFu) && d[3] == ((BTAPTX_VENDOR_ID >> 8) & 0xFFu) && d[4] == ((BTAPTX_VENDOR_ID >> 16) & 0xFFu) && d[5] == (BTAPTX_VENDOR_ID >> 24) &&
	       d[6] == (BTAPTX_CODEC_ID & 0xFFu) && d[7] == (BTAPTX_CODEC_ID >> 8);
}

/* Walks a sink's capabilities (the reply after its two header bytes, `n` long) to its media codec element and says whether it is classic aptX at 48 kHz in
 * stereo. Fills `out` (seid not set here; object, vbr and max_bps are 0: aptX has none of them). Never reads past `n`. A malformed list (an element running past
 * the end) is no. The vendor element is one of several the sink may list: other codec elements are skipped over. */
static inline int btaptx_sep_scan(const uint8_t *caps, uint32_t n, BtAacSink *out)
{
	uint32_t i = 0;

	while (i + 2u <= n) {
		uint32_t cat = caps[i], losc = caps[i + 1u];

		if (i + 2u + losc > n)
			return 0;
		if (cat == BTAAC_CAT_CODEC && losc == BTAPTX_LOSC && btaptx_sep_is_aptx(caps + i + 2u)) {
			uint8_t info = caps[i + 2u + 8u];

			if (!(info & BTAPTX_INFO_48K) || !(info & BTAPTX_INFO_STEREO))
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

static inline int btaptx_sep_is_aptx_hd(const uint8_t *d)
{
	return (d[0] >> 4) == 0u && d[1] == BTAPTX_CODEC_TYPE_VENDOR &&
	       d[2] == (BTAPTX_HD_VENDOR_ID & 0xFFu) && d[3] == ((BTAPTX_HD_VENDOR_ID >> 8) & 0xFFu) && d[4] == ((BTAPTX_HD_VENDOR_ID >> 16) & 0xFFu) && d[5] == (BTAPTX_HD_VENDOR_ID >> 24) &&
	       d[6] == (BTAPTX_HD_CODEC_ID & 0xFFu) && d[7] == (BTAPTX_HD_CODEC_ID >> 8);
}

/* The same walk for aptX HD at 48 kHz in stereo (the sink's reserved bytes are not looked at). */
static inline int btaptx_sep_scan_hd(const uint8_t *caps, uint32_t n, BtAacSink *out)
{
	uint32_t i = 0;

	while (i + 2u <= n) {
		uint32_t cat = caps[i], losc = caps[i + 1u];

		if (i + 2u + losc > n)
			return 0;
		if (cat == BTAAC_CAT_CODEC && losc == BTAPTX_HD_LOSC && btaptx_sep_is_aptx_hd(caps + i + 2u)) {
			uint8_t info = caps[i + 2u + 8u];

			if (!(info & BTAPTX_INFO_48K) || !(info & BTAPTX_INFO_STEREO))
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

/* The HD configuration: 17 bytes into `out`, the reserved bytes zero. */
static inline void btaptx_sep_config_hd(uint8_t *out)
{
	out[0] = (uint8_t)BTAAC_CAT_TRANSPORT;
	out[1] = 0;
	out[2] = (uint8_t)BTAAC_CAT_CODEC;
	out[3] = (uint8_t)BTAPTX_HD_LOSC;
	out[4] = 0;                                   /* audio */
	out[5] = (uint8_t)BTAPTX_CODEC_TYPE_VENDOR;
	out[6] = (uint8_t)(BTAPTX_HD_VENDOR_ID & 0xFFu);
	out[7] = (uint8_t)((BTAPTX_HD_VENDOR_ID >> 8) & 0xFFu);
	out[8] = (uint8_t)((BTAPTX_HD_VENDOR_ID >> 16) & 0xFFu);
	out[9] = (uint8_t)(BTAPTX_HD_VENDOR_ID >> 24);
	out[10] = (uint8_t)(BTAPTX_HD_CODEC_ID & 0xFFu);
	out[11] = (uint8_t)(BTAPTX_HD_CODEC_ID >> 8);
	out[12] = (uint8_t)BTAPTX_INFO_CONFIG;
	out[13] = out[14] = out[15] = out[16] = 0;
}

/* The configuration SET_CONFIGURATION carries: media transport, then the media codec element narrowed to 48 kHz stereo: 13 bytes into `out`
 * (01 00 07 09 00 FF 4F 00 00 00 01 00 12). No content protection (optional for a source; the packets then carry no protection byte). */
static inline void btaptx_sep_config(uint8_t *out)
{
	out[0] = (uint8_t)BTAAC_CAT_TRANSPORT;
	out[1] = 0;
	out[2] = (uint8_t)BTAAC_CAT_CODEC;
	out[3] = (uint8_t)BTAPTX_LOSC;
	out[4] = 0;                                   /* audio */
	out[5] = (uint8_t)BTAPTX_CODEC_TYPE_VENDOR;
	out[6] = (uint8_t)(BTAPTX_VENDOR_ID & 0xFFu);
	out[7] = (uint8_t)((BTAPTX_VENDOR_ID >> 8) & 0xFFu);
	out[8] = (uint8_t)((BTAPTX_VENDOR_ID >> 16) & 0xFFu);
	out[9] = (uint8_t)(BTAPTX_VENDOR_ID >> 24);
	out[10] = (uint8_t)(BTAPTX_CODEC_ID & 0xFFu);
	out[11] = (uint8_t)(BTAPTX_CODEC_ID >> 8);
	out[12] = (uint8_t)BTAPTX_INFO_CONFIG;
}

/* Checks the capability bytes of a SET_CONFIGURATION for endpoint 4 (`n` of them, at most BTAAC_CONFIG_MAX): media transport, then one media codec element
 * that is classic aptX with exactly 48 kHz and stereo chosen (the one configuration we advertise). Nothing else is accepted. Returns 0, or the AVDTP error to
 * reject with and in `*cat` the category it concerns (0: none): the same rules and codes as btaac_sep_check_config. */
static inline uint32_t btaptx_sep_check_config(const uint8_t *caps, uint32_t n, BtAacSink *out, uint32_t *cat)
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
			if (losc < 2u || (d[0] >> 4) != 0u || d[1] != BTAPTX_CODEC_TYPE_VENDOR)
				return BTAAC_ERR_UNSUPPORTED;                  /* SBC, AAC, another media type: not what endpoint 4 is */
			if (losc != BTAPTX_LOSC)
				return BTAAC_ERR_BAD_LENGTH;
			if (!btaptx_sep_is_aptx(d) || d[8] != BTAPTX_INFO_CONFIG)
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

#endif /* BTAPTX_SEP_H */
