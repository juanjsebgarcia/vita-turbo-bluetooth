/*
 * btaac_latm.h - the AAC media packet: an access unit of the encoder wrapped as an A2DP source sends it. Header only, plain integer C,
 * no system call and no library call, so the send hook (kernel/btaac_hook.c) runs exactly this and the host test compiles it as it is.
 *
 * A2DP 1.3 carries MPEG-2/4 AAC as LATM (ISO/IEC 14496-3 4.4.1) in an RTP packet (RFC 3016): an RTP header (12 bytes, payload type 96,
 * the timestamp in sample units, +1024 a frame, the marker set on the packet that ends an AudioMuxElement) and the AudioMuxElement. There
 * is no one-byte media header as SBC has.
 *
 * The AudioMuxElement is written with its StreamMuxConfig IN BAND in every frame (muxConfigPresent 1, useSameStreamMux 0), which is
 * what common sources send and what sinks accept: a sink that wants the configuration out of band (AVDTP gives it AAC LC, 48 kHz,
 * two channels already) still decodes a frame that carries it, while a sink that expects it in band would be lost without it. It costs
 * 6 bytes a frame. The config is fixed (the only one we negotiate): AAC LC, 48 kHz, two channels, one frame of 1024 samples a unit,
 * constant length coding. Bit by bit:
 *   useSameStreamMux 0 | audioMuxVersion 0 | allStreamsSameTimeFraming 1 | numSubFrames 0 (6) | numProgram 0 (4) | numLayer 0 (3)
 *   | AudioSpecificConfig 0x11 0x90 (object 2, rate index 3 = 48 kHz, channels 2, no extension) | frameLengthType 0 (3)
 *   | latmBufferFullness 0xFF (8) | otherDataPresent 0 | crcCheckPresent 0                                      = 45 bits
 *   then PayloadLengthInfo (0xFF for every 255 bytes, then the rest: one byte, or more) and the access unit, then zero bits to the byte.
 *
 * A packet may not exceed the link's L2CAP MTU: an access unit whose element does not fit is cut into pieces over several RTP packets
 * (same timestamp, marker only on the last, RFC 3016), `btaac_latm_pieces` / `btaac_rtp_piece`.
 */
#ifndef BTAAC_LATM_H
#define BTAAC_LATM_H

#include <stdint.h>

#define BTAAC_RTP_HDR       12u
#define BTAAC_RTP_PT        96u
#define BTAAC_LATM_MAX      (1536u + 16u)       /* the largest element: an access unit at its ceiling and the header in front of it */
#define BTAAC_MTU_DEFAULT   672u                /* L2CAP's minimum: what every sink accepts when it did not name a larger one */
#define BTAAC_MTU_MIN       256u                /* below this the value is not believed (the pieces would be absurdly many)  */
#define BTAAC_MTU_MAX       4096u               /* above this either: the encoder's largest element is smaller than that anyway */

/* `n` bits of `v` (the low bits, most significant first) at bit position *pos of `buf`, which must be zero there. */
static inline void btaac_put_bits(uint8_t *buf, uint32_t *pos, uint32_t v, uint32_t n)
{
	while (n) {
		uint32_t room = 8u - (*pos & 7u), take = n < room ? n : room;
		uint32_t bits = (v >> (n - take)) & ((1u << take) - 1u);

		buf[*pos >> 3] |= (uint8_t)(bits << (room - take));
		*pos += take;
		n -= take;
	}
}

/* The AudioMuxElement of one access unit of `len` bytes into `out` (capacity `cap`). Returns its length, 0 if it would not fit or `len`
 * is 0. */
static inline uint32_t btaac_latm_wrap(uint8_t *out, uint32_t cap, const uint8_t *au, uint32_t len)
{
	uint32_t pos = 0, nlen = len / 255u + 1u, total, i, rest;

	if (!len || len > 0xFFFFu || (45u + 8u * (nlen + len) + 7u) / 8u > cap)
		return 0;
	total = (45u + 8u * (nlen + len) + 7u) / 8u;
	for (i = 0; i < total; i++)
		out[i] = 0;
	btaac_put_bits(out, &pos, 0, 1);          /* useSameStreamMux */
	btaac_put_bits(out, &pos, 0, 1);          /* audioMuxVersion */
	btaac_put_bits(out, &pos, 1, 1);          /* allStreamsSameTimeFraming */
	btaac_put_bits(out, &pos, 0, 6);          /* numSubFrames */
	btaac_put_bits(out, &pos, 0, 4);          /* numProgram */
	btaac_put_bits(out, &pos, 0, 3);          /* numLayer */
	btaac_put_bits(out, &pos, 0x1190, 16);    /* AudioSpecificConfig: AAC LC, 48 kHz, 2 channels */
	btaac_put_bits(out, &pos, 0, 3);          /* frameLengthType */
	btaac_put_bits(out, &pos, 0xFF, 8);       /* latmBufferFullness */
	btaac_put_bits(out, &pos, 0, 1);          /* otherDataPresent */
	btaac_put_bits(out, &pos, 0, 1);          /* crcCheckPresent */
	for (rest = len; rest >= 255u; rest -= 255u)
		btaac_put_bits(out, &pos, 0xFF, 8);
	btaac_put_bits(out, &pos, rest, 8);       /* PayloadLengthInfo */
	for (i = 0; i < len; i++)
		btaac_put_bits(out, &pos, au[i], 8);
	return total;
}

/* The AudioMuxElement's length for an access unit of `len` bytes (what btaac_latm_wrap makes): the 45 header bits, the PayloadLengthInfo (one byte, and one more for
 * every 255 bytes) and the unit, to the byte. */
static inline uint32_t btaac_latm_len(uint32_t len)
{
	return (45u + 8u * (len / 255u + 1u + len) + 7u) / 8u;
}

/* The longest access unit whose element, with the RTP header, goes out in ONE packet of a link whose packet limit is `mtu` bytes (a believable one: btaac_mtu_sane).
 * The encoder is held to this (btaac_enc_limit, kernel/btaac_enclimit.c), so a unit is never cut into pieces; a cut unit is the safety net below, not the plan. */
static inline uint32_t btaac_au_cap(uint32_t mtu)
{
	uint32_t room, len;

	if (mtu <= BTAAC_RTP_HDR + 7u)
		return 0;
	room = mtu - BTAAC_RTP_HDR;
	len = room - 7u;                       /* the element is at least the unit and 7 bytes */
	while (len && btaac_latm_len(len) > room)
		len--;
	return len;
}

/* The pieces an element of `latm_len` bytes needs when a packet (header and payload) may be `mtu` bytes. By subtraction: a division would be a
 * library call, and the hooks that run this may call none. */
static inline uint32_t btaac_latm_pieces(uint32_t latm_len, uint32_t mtu)
{
	uint32_t room = mtu - BTAAC_RTP_HDR, pieces = 0;

	while (latm_len) {
		latm_len -= latm_len < room ? latm_len : room;
		pieces++;
	}
	return pieces;
}

/* The RTP header: version 2, no padding, extension or contributing sources; payload type 96; `marker` as RFC 3016 says. */
static inline void btaac_rtp_header(uint8_t *h, uint16_t seq, uint32_t ts, uint32_t ssrc, int marker)
{
	h[0] = 0x80;
	h[1] = (uint8_t)((marker ? 0x80u : 0u) | BTAAC_RTP_PT);
	h[2] = (uint8_t)(seq >> 8);
	h[3] = (uint8_t)seq;
	h[4] = (uint8_t)(ts >> 24);
	h[5] = (uint8_t)(ts >> 16);
	h[6] = (uint8_t)(ts >> 8);
	h[7] = (uint8_t)ts;
	h[8] = (uint8_t)(ssrc >> 24);
	h[9] = (uint8_t)(ssrc >> 16);
	h[10] = (uint8_t)(ssrc >> 8);
	h[11] = (uint8_t)ssrc;
}

/* Piece `k` (from 0) of an element of `latm_len` bytes: the whole RTP packet into `pkt` (header, then up to mtu - 12 bytes of the element).
 * Returns the packet's length. The marker is set on the last piece only. */
static inline uint32_t btaac_rtp_piece(uint8_t *pkt, const uint8_t *latm, uint32_t latm_len, uint32_t mtu, uint32_t k,
                                       uint16_t seq, uint32_t ts, uint32_t ssrc)
{
	uint32_t room = mtu - BTAAC_RTP_HDR, off = k * room, n = latm_len - off, i;

	if (n > room)
		n = room;
	btaac_rtp_header(pkt, seq, ts, ssrc, off + n >= latm_len);
	for (i = 0; i < n; i++)
		pkt[BTAAC_RTP_HDR + i] = latm[off + i];
	return BTAAC_RTP_HDR + n;
}

/* The link's MTU as the Bluetooth code stores it, made safe: anything outside what is believable is the L2CAP default. */
static inline uint32_t btaac_mtu_sane(uint32_t mtu)
{
	return mtu >= BTAAC_MTU_MIN && mtu <= BTAAC_MTU_MAX ? mtu : BTAAC_MTU_DEFAULT;
}

/* The RTP timestamp of access unit number `au_seq` of the session, advanced from the last one sent: 1024 samples a unit, so a unit the
 * encoder dropped leaves its gap, and a new session (the number went backwards) continues from where the stream was. */
static inline uint32_t btaac_ts_next(uint32_t last_ts, uint32_t last_au, uint32_t au_seq, int have_last)
{
	if (!have_last)
		return last_ts;
	return last_ts + (au_seq > last_au ? au_seq - last_au : 1u) * 1024u;
}

#endif /* BTAAC_LATM_H */
