/*
 * btaptx_core.h - the decisions of the aptX add-on that need no system call: the packet's size, the crash record and what a start does with it, the
 * self-test's signal and verdict. Header only, plain integer C, so the host test (test/test_btaptx.c) compiles exactly what the kernel module runs.
 *
 * The encoders themselves are the vendored aptX and aptX HD ones (third_party/aptxenc, Apache-2.0), which only the add-on module (bluetoothturbo_aptx.skprx) holds.
 */
#ifndef BTAPTX_CORE_H
#define BTAPTX_CORE_H

#include <stdint.h>

#define BTAPTX_BLOCK_FRAMES  512u      /* the audio sender hands over 512 frames (s16 stereo, 48 kHz) at a time                               */
#define BTAPTX_BLOCK_BYTES   512u      /* ... which the encoder makes 4 bytes per 4 stereo frames: exactly one packet's payload               */
#define BTAPTX_RATE          48000u
#define BTAPTX_KBPS          384u      /* 48000 * 2 channels * 16 bits / 4 (the codec has no rate setting)                                    */

/* ---- the self-test --------------------------------------------------------------------------------------------------------------- */

#define BTAPTX_ST_FRAMES     1024u     /* two blocks: the second runs on the first's state                                                    */
#define BTAPTX_ST_BYTES      1024u
/* The FNV-1a hash (64 bit) of the 1,024 bytes a correct encoder makes of the signal below. The value was made on the host with ffmpeg's aptX encoder
 * used as a black box (`ffmpeg -f s16le -ar 48000 -ac 2 -i signal.raw -c:a aptx -f aptx out.aptx`; the first 1,024 bytes of its output) and is the
 * one the vendored encoder gives (test/test_btaptx.c checks both against ffmpeg when it is on the machine). */
#ifndef BTAPTX_ST_HASH   /* (the host test builds a second copy with another value, to see a failed self-test) */
#define BTAPTX_ST_HASH       0xDB97AFEC90714FAEull
#endif

enum BtAptxSelfTest { BTAPTX_ST_NOTRUN = 0, BTAPTX_ST_PASS, BTAPTX_ST_FAIL, BTAPTX_ST_NORUN };   /* NORUN: held, or the record cannot be written, or busy */

#define BTAPTX_FNV_BASIS 0xCBF29CE484222325ull
#define BTAPTX_FNV_PRIME 0x100000001B3ull

static inline uint64_t btaptx_fnv(uint64_t h, const uint8_t *p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= BTAPTX_FNV_PRIME;
	}
	return h;
}

static inline uint32_t btaptx_hash32(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

/* Frame `n` of the test signal: a triangle and noise on the left, a sawtooth and noise on the right, a loud click at 700 and silence from 900 (so the
 * adaptive steps see a loud and a quiet stretch). Integer only; no division. */
static inline void btaptx_sig(uint32_t n, int16_t *l, int16_t *r)
{
	uint32_t p = n & 63u, q = (n * 7u) & 127u;
	int32_t a = (p < 32u) ? (int32_t)p * 750 - 12000 : 12000 - (int32_t)(p - 32u) * 750;
	int32_t b = (int32_t)q * 120 - 7600;

	a += (int32_t)(btaptx_hash32(n) >> 20) - 2048;
	b += (int32_t)(btaptx_hash32(n + 99991u) >> 21) - 1024;
	if (n >= 700u && n < 703u) {
		a = 30000;
		b = -30000;
	}
	if (n >= 900u && n < 1000u) {
		a = 0;
		b = 0;
	}
	*l = (int16_t)a;
	*r = (int16_t)b;
}

/* ---- aptX HD (experimental) ---------------------------------------------------------------------------------------------------- */
/* The HD encoder: 24-bit input (the 16-bit sample shifted up by 8), 6 bytes for every 4 stereo frames, so a 512-frame block is 128 frames of 6 bytes, 768 bytes. Unlike
 * classic aptX the media packet carries an RTP header (12 bytes: version 2, payload type 96, the sequence number and the timestamp big endian, the source
 * identifier 0, no extension, no payload header, no trailer) and a packet holds as many whole 6-byte frames as the media channel takes. Public sources for the
 * framing: PipeWire's spa/plugins/bluez5/a2dp-codec-aptx.c (MIT) and BlueALSA's src/a2dp-aptx-hd.c, src/rtp.c (MIT), which both fill a packet with
 * (MTU - 12) / 6 frames and number the packets in order, the timestamp counting the PCM frames sent. */
#define BTAPTX_HD_FRAME_BYTES   6u
#define BTAPTX_HD_FRAME_PCM     4u                                   /* stereo frames of sound one 6-byte frame holds                                      */
#define BTAPTX_HD_BLOCK_FRAMES  (BTAPTX_BLOCK_FRAMES / BTAPTX_HD_FRAME_PCM)   /* 128 encoded frames to a block                                              */
#define BTAPTX_HD_BLOCK_BYTES   (BTAPTX_HD_BLOCK_FRAMES * BTAPTX_HD_FRAME_BYTES)   /* 768                                                                  */
#define BTAPTX_HD_KBPS          576u                                 /* 48000 * 2 * 24 / 4 / 1000: the codec has no rate setting                            */
#define BTAPTX_HD_RTP_HDR       12u
#define BTAPTX_HD_PIECES_MAX    8u                                   /* the most packets one block may be split into                                      */
#define BTAPTX_HD_PACKET_MAX    (BTAPTX_HD_RTP_HDR + BTAPTX_HD_BLOCK_BYTES)   /* 780: the whole block in one packet                                              */

#define BTAPTX_ST_HD_BYTES      1536u    /* the HD self-test's output: the same 1,024 frames, 6 bytes for every 4 */
#ifndef BTAPTX_ST_HASH_HD   /* (the host test builds a copy with another value) */
/* FNV-1a (64 bit) of the 1,536 bytes of the HD stream of the same signal, made on the host with ffmpeg's aptX HD encoder as a black box (`ffmpeg ... -c:a aptx_hd
 * -f aptx_hd`), the one the vendored HD encoder gives (test/test_btaptx.c checks both against ffmpeg when it is on the machine). */
#define BTAPTX_ST_HASH_HD    0x48401DD4901DF60Dull
#endif

/* How many packets a block of HD frames goes out in for a media channel limit of `mtu` bytes: 1 when the whole 780 bytes fit; 0 when the limit is so small that a
 * packet would carry fewer than 16 frames (more than BTAPTX_HD_PIECES_MAX packets a block: not sent at all). */
static inline uint32_t btaptx_hd_pieces(uint32_t mtu)
{
	uint32_t room, per, n;

	if (mtu <= BTAPTX_HD_RTP_HDR)
		return 0;
	room = mtu - BTAPTX_HD_RTP_HDR;
	per = 0;
	while (per + 1u <= BTAPTX_HD_BLOCK_FRAMES && (per + 1u) * BTAPTX_HD_FRAME_BYTES <= room)   /* (no division: the module has none) */
		per++;
	if (!per)
		return 0;
	for (n = 1; n <= BTAPTX_HD_PIECES_MAX; n++)
		if (n * per >= BTAPTX_HD_BLOCK_FRAMES)
			return n;
	return 0;
}

/* The frames in packet `k` of `pieces` for a block of 128: as even as can be (the first ones take the extra frame). */
static inline uint32_t btaptx_hd_piece_frames(uint32_t k, uint32_t pieces)
{
	uint32_t base = 0, rem, left = BTAPTX_HD_BLOCK_FRAMES;

	while (left >= pieces) {
		left -= pieces;
		base++;
	}
	rem = left;
	return base + (k < rem ? 1u : 0u);
}

/* The 12-byte RTP header of one HD packet. */
static inline void btaptx_hd_rtp(uint8_t *out, uint16_t seq, uint32_t ts)
{
	out[0] = 0x80;                      /* version 2, no padding, no extension, no contributing sources */
	out[1] = 96;                        /* payload type 96, marker 0                                    */
	out[2] = (uint8_t)(seq >> 8);
	out[3] = (uint8_t)seq;
	out[4] = (uint8_t)(ts >> 24);
	out[5] = (uint8_t)(ts >> 16);
	out[6] = (uint8_t)(ts >> 8);
	out[7] = (uint8_t)ts;
	out[8] = out[9] = out[10] = out[11] = 0;   /* the source identifier: both public implementations leave it 0 */
}

/* ---- the crash record ---------------------------------------------------------------------------------------------------------- */
/* ur0:data/bluetoothturbo/aptx.guard, eight bytes "BPG1", session, test, count digit, newline; overwritten in place like aac.guard.
 *   session: K none running; S an aptX session is running (the encoder runs inline in the audio sender's thread: a crash there is a crash of the console)
 *   test:    K rest; t the self-test is running; B banned (the self-test was running when the console went down: never run again until the file is deleted)
 *   count:   sessions in a row that did not end cleanly, 0..9
 */
#define BTAPTX_G_BYTES       8
#define BTAPTX_CRASH_LIMIT   3
#define BTAPTX_CRASH_MAX     9

typedef struct BtAptxGuard {
	char    session, test;
	uint8_t count;
} BtAptxGuard;

typedef struct BtAptxBoot {
	int hold;       /* aptX is held off for this start (and the next ones while the cause stands)                       */
	int hold_now;   /* an unclean session was found at this start                                                        */
	int banned;     /* the self-test killed the console: the encoder is never run                                       */
} BtAptxBoot;

static inline void btaptx_guard_default(BtAptxGuard *g)
{
	g->session = 'K';
	g->test = 'K';
	g->count = 0;
}

static inline int btaptx_guard_decode(BtAptxGuard *g, const uint8_t *b, int n)
{
	btaptx_guard_default(g);
	if (n < BTAPTX_G_BYTES - 1 || b[0] != 'B' || b[1] != 'P' || b[2] != 'G' || b[3] != '1')
		return 0;
	if ((b[4] != 'K' && b[4] != 'S') || (b[5] != 'K' && b[5] != 't' && b[5] != 'B') || b[6] < '0' || b[6] > '0' + BTAPTX_CRASH_MAX)
		return 0;
	g->session = (char)b[4];
	g->test = (char)b[5];
	g->count = (uint8_t)(b[6] - '0');
	return 1;
}

static inline void btaptx_guard_encode(const BtAptxGuard *g, uint8_t *b)
{
	b[0] = 'B'; b[1] = 'P'; b[2] = 'G'; b[3] = '1';
	b[4] = (uint8_t)g->session;
	b[5] = (uint8_t)g->test;
	b[6] = (uint8_t)('0' + (g->count > BTAPTX_CRASH_MAX ? BTAPTX_CRASH_MAX : g->count));
	b[7] = '\n';
}

/* What a start does with what it found: a session left running means the console went down in it (the count goes up and aptX is held off for this start);
 * a self-test marker left bans the encoder for good; three unclean sessions in a row hold it off until the file is deleted. The record is changed to match. */
static inline BtAptxBoot btaptx_guard_boot(BtAptxGuard *g)
{
	BtAptxBoot r = { 0, 0, 0 };

	if (g->session == 'S') {
		if (g->count < BTAPTX_CRASH_MAX)
			g->count++;
		g->session = 'K';
		r.hold_now = 1;
	}
	if (g->test == 't')
		g->test = 'B';
	r.banned = g->test == 'B';
	r.hold = r.hold_now || r.banned || g->count >= BTAPTX_CRASH_LIMIT;
	return r;
}

static inline void btaptx_guard_session_begin(BtAptxGuard *g) { g->session = 'S'; }
static inline void btaptx_guard_session_end(BtAptxGuard *g) { g->session = 'K'; g->count = 0; }

/* ---- the packet --------------------------------------------------------------------------------------------------------------- */
/* An aptX packet is the encoder's bytes and nothing else: no RTP header, no payload header (the configuration carries no content protection, so no
 * protection byte either). One 512-frame block is one 512-byte packet; the media channel's packet limit (672 until the sink says otherwise) must take it. */
#define BTAPTX_PACKET_BYTES  BTAPTX_BLOCK_BYTES

static inline int btaptx_packet_fits(uint32_t mtu)
{
	return mtu >= BTAPTX_PACKET_BYTES;
}

#endif /* BTAPTX_CORE_H */
