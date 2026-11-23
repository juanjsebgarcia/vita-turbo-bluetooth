/*
 * btldac_core.h - the decisions of the LDAC add-on that need no system call: the sizes, the packet, the crash record and what a start does with it, the self-test's
 * signal and verdict, and the adaptive rate's bookkeeping. Header only, plain integer C, so the host test (test/test_btldac.c) compiles exactly what the kernel
 * module and the main module's hooks run.
 *
 * The encoder is Sony's open-source libldac (third_party/libldac, Apache-2.0, see THIRD_PARTY_NOTICES.md), built in its integer (fixed point) form; only the add-on module
 * (bluetoothturbo_ldac.skprx) holds it. What it makes: frames of 110 / 220 bytes (330 / 660 kb/s at 48 kHz), 6 / 3 of them to a packet, 660 bytes in all; one call
 * takes 128 stereo frames (2.667 ms of sound), so the audio sender's block of 512 frames is four calls.
 */
#ifndef BTLDAC_CORE_H
#define BTLDAC_CORE_H

#include <stdint.h>

#define BTLDAC_FRAME           128u      /* frames of sound (a stereo pair each) one encode call takes                                                     */
#define BTLDAC_RATE            48000u
#define BTLDAC_BLOCK_FRAMES    512u      /* what the audio sender's hook hands over at a time                                                            */
#define BTLDAC_MTU_REQUIRED    679u      /* libldac refuses to start for a media channel limit below this: it is built around one 2-DH5 packet            */
#define BTLDAC_RTP_HDR         12u
#define BTLDAC_PKT_HDR         13u       /* the RTP header and the one payload header byte (frame count, no fragmentation)                                */
#define BTLDAC_FRAMES_BYTES    661u      /* the most bytes of frames in a packet: libldac sizes its packets for 679 - 18, whatever the limit is           */
#define BTLDAC_PKT_MAX         (BTLDAC_PKT_HDR + BTLDAC_FRAMES_BYTES)   /* 674 bytes: the whole packet                                                      */
#define BTLDAC_NFRM_MAX        15u       /* the frame count is four bits                                                                                  */
#define BTLDAC_RTP_PT          96u
#define BTLDAC_PCM_RING_FRAMES 16384u    /* the PCM input ring (btldac_leaf.c): 341 ms, to ride out a stall of the engine's thread                         */
#define BTLDAC_PKT_RING_SLOTS  16u       /* the output ring: 16 packets (at 330 kb/s, 256 ms of sound)                                                    */
#define BTLDAC_SEND_MAX        2u        /* the most packets one media hook call sends (660 kb/s needs 4 of every 3 blocks' worth: 1 or 2 a call)           */

/* The bit rate setting (the values of BTT_KEY_LDAC_RATE; the Settings list's order): fixed 330, fixed 660, or adaptive between them. */
enum { BTLDAC_MODE_330 = 0, BTLDAC_MODE_660 = 1, BTLDAC_MODE_ABR = 2, BTLDAC_MODE_COUNT };
/* libldac's quality indexes for those: LDACBT_EQMID_MQ (330), _SQ (660). The adaptive mode starts on 330 and the adaptive code may move up to 660, never to 990 (HQ). */
#define BTLDAC_EQMID_HQ        0
#define BTLDAC_EQMID_SQ        1
#define BTLDAC_EQMID_MQ        2
#define BTLDAC_ABR_MS          20u       /* the adaptive code is called this often (its own documented interval)                                          */

/* The frame bytes of a CBR packet at each fixed mode (libldac's table; the host totals: 412,500 / 825,000 bytes for 10 s of sound). */
#define BTLDAC_PKT_BYTES_CBR   660u
static inline uint32_t btldac_frames_per_pkt(uint32_t mode) { return mode == BTLDAC_MODE_660 ? 3u : 6u; }
static inline uint32_t btldac_eqmid_of(uint32_t mode)       { return mode == BTLDAC_MODE_660 ? (uint32_t)BTLDAC_EQMID_SQ : (uint32_t)BTLDAC_EQMID_MQ; }
static inline uint32_t btldac_kbps_of(uint32_t mode)        { return mode == BTLDAC_MODE_660 ? 660u : 330u; }

/* ---- the packet -------------------------------------------------------------------------------------------------------------------- */

/* The RTP header (version 2, payload type 96, marker 0, the sequence number, the timestamp and the source identifier big endian), the payload header byte (the
 * fragmentation, start and last bits clear: libldac never splits a frame, and the public LDAC packetisers leave them clear; the low four bits are the number of frames),
 * then the frames as the encoder made them. Returns the packet's length, or 0 if the frame count or the room is wrong. */
static inline uint32_t btldac_packet(uint8_t *pkt, uint32_t cap, const uint8_t *frames, uint32_t len, uint32_t nframes, uint16_t seq, uint32_t ts, uint32_t ssrc)
{
	uint32_t i;

	if (!nframes || nframes > BTLDAC_NFRM_MAX || !len || len > BTLDAC_FRAMES_BYTES || BTLDAC_PKT_HDR + len > cap)
		return 0;
	pkt[0] = 0x80;
	pkt[1] = (uint8_t)BTLDAC_RTP_PT;
	pkt[2] = (uint8_t)(seq >> 8);
	pkt[3] = (uint8_t)seq;
	pkt[4] = (uint8_t)(ts >> 24);
	pkt[5] = (uint8_t)(ts >> 16);
	pkt[6] = (uint8_t)(ts >> 8);
	pkt[7] = (uint8_t)ts;
	pkt[8] = (uint8_t)(ssrc >> 24);
	pkt[9] = (uint8_t)(ssrc >> 16);
	pkt[10] = (uint8_t)(ssrc >> 8);
	pkt[11] = (uint8_t)ssrc;
	pkt[12] = (uint8_t)(nframes & 0x0Fu);
	for (i = 0; i < len; i++)
		pkt[BTLDAC_PKT_HDR + i] = frames[i];
	return BTLDAC_PKT_HDR + len;
}

/* The timestamp advance of a packet of `nframes` frames: 128 samples a frame (what the reference encoder does). */
static inline uint32_t btldac_ts_step(uint32_t nframes) { return nframes * BTLDAC_FRAME; }

/* Whether a media channel limit takes LDAC at all. */
static inline int btldac_mtu_ok(uint32_t mtu) { return mtu >= BTLDAC_MTU_REQUIRED; }

/* The most packets the output ring may hold before the oldest are dropped, for the Bluetooth Latency setting (0 Standard, 1 Low, 2 Lowest): the same sound time the SBC
 * limits mean, about 128 / 64 / 48 ms, in packets of the current mode's size (a 330 packet is 16 ms of sound, a 660 packet 8 ms; adaptive modes use the smaller). */
static inline uint32_t btldac_keep(uint32_t latency, uint32_t mode)
{
	uint32_t ms = latency == 2u ? 48u : latency == 1u ? 64u : 128u;

	return mode == BTLDAC_MODE_330 ? ms / 16u : ms / 8u;
}

/* ---- the adaptive rate ---------------------------------------------------------------------------------------------------------- */

/* The adaptive code (abr/ldacBT_abr.c) is Sony's and may step as far as the highest quality; the product never sends it (about 70 % of the radio's time). Whatever
 * the adaptive code asks for, the quality index is held at 660 kb/s or below. Returns the index to set, or -1 to leave the encoder as it is. */
static inline int btldac_abr_clamp(int eqmid)
{
	return eqmid >= 0 && eqmid < BTLDAC_EQMID_SQ ? BTLDAC_EQMID_SQ : -1;
}

/* ---- the crash record ---------------------------------------------------------------------------------------------------------- */
/* ur0:data/bluetoothturbo/ldac.guard, eight bytes "BLG1", session, test, count digit, newline; overwritten in place like aac.guard.
 *   session: K none running; S an LDAC session is running (the encoder runs in the engine's thread: a crash there takes the console down)
 *   test:    K rest; t the self-test is running; B banned (the self-test was running when the console went down: never run again until the file is deleted)
 *   count:   sessions in a row that did not end cleanly, 0..9
 */
#define BTLDAC_G_BYTES       8
#define BTLDAC_CRASH_LIMIT   3
#define BTLDAC_CRASH_MAX     9

typedef struct BtLdacGuard {
	char    session, test;
	uint8_t count;
} BtLdacGuard;

typedef struct BtLdacBoot {
	int hold;       /* LDAC is held off for this start (and the next ones while the cause stands) */
	int hold_now;   /* an unclean session was found at this start                                  */
	int banned;     /* the self-test killed the console: the encoder is never run                  */
} BtLdacBoot;

static inline void btldac_guard_default(BtLdacGuard *g)
{
	g->session = 'K';
	g->test = 'K';
	g->count = 0;
}

static inline int btldac_guard_decode(BtLdacGuard *g, const uint8_t *b, int n)
{
	btldac_guard_default(g);
	if (n < BTLDAC_G_BYTES - 1 || b[0] != 'B' || b[1] != 'L' || b[2] != 'G' || b[3] != '1')
		return 0;
	if ((b[4] != 'K' && b[4] != 'S') || (b[5] != 'K' && b[5] != 't' && b[5] != 'B') || b[6] < '0' || b[6] > '0' + BTLDAC_CRASH_MAX)
		return 0;
	g->session = (char)b[4];
	g->test = (char)b[5];
	g->count = (uint8_t)(b[6] - '0');
	return 1;
}

static inline void btldac_guard_encode(const BtLdacGuard *g, uint8_t *b)
{
	b[0] = 'B'; b[1] = 'L'; b[2] = 'G'; b[3] = '1';
	b[4] = (uint8_t)g->session;
	b[5] = (uint8_t)g->test;
	b[6] = (uint8_t)('0' + (g->count > BTLDAC_CRASH_MAX ? BTLDAC_CRASH_MAX : g->count));
	b[7] = '\n';
}

/* What a start does with what it found: a session left running means the console went down in it (the count goes up and LDAC is held off for this start); a
 * self-test marker left bans the encoder for good; three unclean sessions in a row hold it off until the file is deleted. The record is changed to match. */
static inline BtLdacBoot btldac_guard_boot(BtLdacGuard *g)
{
	BtLdacBoot r = { 0, 0, 0 };

	if (g->session == 'S') {
		if (g->count < BTLDAC_CRASH_MAX)
			g->count++;
		g->session = 'K';
		r.hold_now = 1;
	}
	if (g->test == 't')
		g->test = 'B';
	r.banned = g->test == 'B';
	r.hold = r.hold_now || r.banned || g->count >= BTLDAC_CRASH_LIMIT;
	return r;
}

static inline void btldac_guard_session_begin(BtLdacGuard *g) { g->session = 'S'; }
static inline void btldac_guard_session_end(BtLdacGuard *g) { g->session = 'K'; g->count = 0; }

/* ---- the self-test ------------------------------------------------------------------------------------------------------------- */

#define BTLDAC_ST_CALLS      192u      /* encode calls of each of the two runs (24,576 frames, 0.51 s of sound)                                              */
enum BtLdacSelfTest { BTLDAC_ST_NOTRUN = 0, BTLDAC_ST_PASS, BTLDAC_ST_FAIL, BTLDAC_ST_NORUN };   /* NORUN: held, or the record cannot be written, or busy */

#define BTLDAC_FNV_BASIS 0xCBF29CE484222325ull
#define BTLDAC_FNV_PRIME 0x100000001B3ull

static inline uint64_t btldac_fnv(uint64_t h, const uint8_t *p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= BTLDAC_FNV_PRIME;
	}
	return h;
}

static inline uint32_t btldac_hash32(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

/* Frame `n` of the test signal: a triangle and noise on the left, a sawtooth and noise on the right, a loud click at 10,000 and silence from 14,000 (so the encoder
 * meets a loud and a quiet stretch). Integer only; no division. */
static inline void btldac_sig(uint32_t n, int16_t *l, int16_t *r)
{
	uint32_t p = n & 63u, q = (n * 7u) & 127u;
	int32_t a = (p < 32u) ? (int32_t)p * 750 - 12000 : 12000 - (int32_t)(p - 32u) * 750;
	int32_t b = (int32_t)q * 120 - 7600;

	a += (int32_t)(btldac_hash32(n) >> 20) - 2048;
	b += (int32_t)(btldac_hash32(n + 99991u) >> 21) - 1024;
	if (n >= 10000u && n < 10003u) {
		a = 30000;
		b = -30000;
	}
	if (n >= 14000u && n < 15000u) {
		a = 0;
		b = 0;
	}
	*l = (int16_t)a;
	*r = (int16_t)b;
}

/* What a correct encoder makes of the signal at each fixed mode: the packets (every one 660 bytes of frames), the bytes and the FNV-1a hash (64 bit) of every output
 * byte in order. Made on the host with our own build of the same sources (test/test_btldac.c compiles the library from source and compares); the console's encoder
 * (ARM code, the compiler's 64-bit arithmetic) must give the same bytes. */
#ifndef BTLDAC_ST_HASH_330   /* (the host test builds a second copy with another value, to see a failed self-test) */
#define BTLDAC_ST_HASH_330   0x47EE778677FA09DDull
#endif
#ifndef BTLDAC_ST_HASH_660
#define BTLDAC_ST_HASH_660   0x779317A219BD9753ull
#endif
#define BTLDAC_ST_PKTS_330   (BTLDAC_ST_CALLS / 6u)
#define BTLDAC_ST_PKTS_660   (BTLDAC_ST_CALLS / 3u)
#define BTLDAC_ST_BYTES_330  (BTLDAC_ST_PKTS_330 * BTLDAC_PKT_BYTES_CBR)
#define BTLDAC_ST_BYTES_660  (BTLDAC_ST_PKTS_660 * BTLDAC_PKT_BYTES_CBR)

/* What one self-test run (one mode) gave. */
typedef struct BtLdacRun {
	uint8_t  ok;           /* every call succeeded and every packet was whole frames of the size the mode makes */
	uint32_t pkts, bytes;
	uint64_t hash;
} BtLdacRun;

static inline int btldac_run_pass(const BtLdacRun *r, uint32_t mode)
{
	return r->ok && r->pkts == (mode == BTLDAC_MODE_660 ? BTLDAC_ST_PKTS_660 : BTLDAC_ST_PKTS_330) &&
	       r->bytes == (mode == BTLDAC_MODE_660 ? BTLDAC_ST_BYTES_660 : BTLDAC_ST_BYTES_330) &&
	       r->hash == (mode == BTLDAC_MODE_660 ? BTLDAC_ST_HASH_660 : BTLDAC_ST_HASH_330);
}

/* The bytes a run of `calls` encode calls must make at 330, 660 or 990 kb/s: every packet 660 bytes of 6, 3 or 2 frames, the first one after the encoder's first 6, 3 or 2
 * calls. (990 kb/s is not offered as a setting; it is only measured.) */
static inline uint32_t btldac_bench_fpp(uint32_t kbps) { return kbps == 990u ? 2u : kbps == 660u ? 3u : 6u; }
static inline uint32_t btldac_bench_bytes(uint32_t kbps, uint32_t calls)
{
	uint32_t fpp = btldac_bench_fpp(kbps), n = 0;

	while ((n + 1u) * fpp <= calls)   /* (no division: the module has none) */
		n++;
	return n * BTLDAC_PKT_BYTES_CBR;
}

#endif /* BTLDAC_CORE_H */
