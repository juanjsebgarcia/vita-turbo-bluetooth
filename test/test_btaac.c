/*
 * test_btaac.c - host test of the AAC engine (kernel/btaac.c, btaac_leaf.c, btaac_core.h), compiled as they are, with the console's
 * kernel calls replaced by fakes that really run the engine's thread (a POSIX thread), its event flag, its memory blocks and the
 * crash record's file, and with the vendored encoder (one build) behind both of the engine's build entry points.
 *
 *   make test      (cc -Itest/host test/test_btaac.c + the vendored encoder, then run)
 *
 * What is proved:
 *   - the bit rate: every request is brought into the encoder's range (4,000 to 160,000 a channel), under the caller's maximum (the
 *     headphones'), and the encoder is never given a rate it would replace with its default;
 *   - the rings: the PCM ring takes whole blocks only (a full ring refuses and counts), keeps every frame in order across any
 *     number of wraps, stereo and mono; the access unit ring keeps units in order with their numbers, refuses when full, skips units of
 *     an earlier session, drops (and counts) a unit the caller's buffer is too small for; nothing is accepted while no session
 *     runs; a producer thread and a consumer thread running flat out lose and reorder nothing;
 *   - the crash record: written and read back, a damaged one is refused and starts clean; what a start does with a session (plain C
 *     or NEON) or a self-test (C or NEON) the last start left running, three in a row, a clean session clearing the count;
 *   - the self-test's verdict for every combination of the two builds' results (pass, NEON differs by one byte, by one length, by the
 *     count; NEON banned; C failed; could not run), and that NEON is allowed on a pass only;
 *   - the engine in a thread of its own: the thread is made at start on the system core's mask, and with the default placement (logged)
 *     when the kernel refuses the mask; the self-test marks the record 'c' then 'K' then 'n' then 'K' around its two runs; a session
 *     marks it 'S' (C) or 'Z' (NEON) while it runs and 'K' with the count at 0 when it ends, by a stop and by module stop; a start
 *     that finds a session left running holds AAC off, bans the NEON build when that was the one, and a start that cannot write the
 *     record runs no encoder at all; a request before the self-test, with a bad format, below the encoder's lowest rate, or while a
 *     session runs is refused with its own error; the encoder's block is freed whenever a session ends, however it ends (a failed
 *     encode, a failed consistency check, the stop, the module's stop); a block that cannot be had fails the session cleanly;
 *   - the real encoder through the engine: the noise signal of the console's timing runs (10 s, 256 and 320 kb/s) gives their figures
 *     (470 access units, a stream of 320,853 and 401,066 bytes, as measured), the units come out numbered
 *     from 0, the encoder takes 8 allocations, and the self-test with a NEON build that changes one byte (or a length, or only the
 *     last unit) is found out and keeps NEON off, while a NEON build that agrees is used when asked for and not when not.
 * Each scenario runs in its own process: the engine keeps its state in statics.
 */
#include <btt_host.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include "../kernel/btaac_leaf.c"
#include "../kernel/btaac.c"
#define BTT_HOST_TEST 1
#include "../kernel/btaac_main.c"

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

#include "host/btaac_fakes.h"

/* ---- the scenarios ------------------------------------------------------------------------------------------ */

static void t_clamp(void)
{
	/* stereo and mono, the sink's maximum, the encoder's own limits */
	CHECK(btaac_clamp_bps(256000, 48000, 2, 0) == 256000, "256 kb/s stays");
	CHECK(btaac_clamp_bps(320000, 48000, 2, 0) == 320000, "320 kb/s is the top at two channels");
	CHECK(btaac_clamp_bps(400000, 48000, 2, 0) == 320000, "400 kb/s comes down to 320");
	CHECK(btaac_clamp_bps(400000, 44100, 2, 0) == 320000, "the same at 44.1 kHz");
	CHECK(btaac_clamp_bps(200000, 48000, 1, 0) == 160000, "mono tops at 160 kb/s");
	CHECK(btaac_clamp_bps(1000, 48000, 2, 0) == 8000, "below the lowest comes up to 4 kb/s a channel");
	CHECK(btaac_clamp_bps(1000, 48000, 1, 0) == 4000, "... one channel");
	CHECK(btaac_clamp_bps(0, 48000, 2, 0) == 8000, "zero is the lowest, not the encoder's default");
	CHECK(btaac_clamp_bps(320000, 48000, 2, 264000) == 264000, "the sink's maximum (264 kb/s) holds it");
	CHECK(btaac_clamp_bps(256000, 48000, 2, 264000) == 256000, "under the maximum, unchanged");
	CHECK(btaac_clamp_bps(256000, 48000, 2, 192000) == 192000, "a lower maximum");
	CHECK(btaac_clamp_bps(256000, 48000, 2, 1000) == 8000, "a maximum below the encoder's lowest cannot go lower than it");
	CHECK(btaac_clamp_bps(0xFFFFFFFFu, 48000, 2, 0) == 320000, "the largest number");
	CHECK(btaac_clamp_bps(256000, 48000, 2, 0xFFFFFFFFu) == 256000, "an unlimited maximum");
	/* the encoder's own rule, so a clamped rate is never replaced by its default (128 kb/s): every clamped value is inside it */
	for (uint32_t rate = 44100; rate <= 48000; rate += 3900)
		for (uint32_t ch = 1; ch <= 2; ch++)
			for (uint32_t bps = 0; bps < 700000; bps += 997) {
				uint32_t r = btaac_clamp_bps(bps, rate, ch, 0);

				CHECK(r / ch >= 4000 && r / ch <= 160000 && r <= rate * 6 * ch, "%u of %u at %u/%u", (unsigned)r, (unsigned)bps, (unsigned)rate, (unsigned)ch);
			}
	CHECK(btaac_format_ok(48000, 2) && btaac_format_ok(44100, 2) && btaac_format_ok(48000, 1) && !btaac_format_ok(32000, 2) &&
	      !btaac_format_ok(48000, 0) && !btaac_format_ok(48000, 3), "the formats the engine encodes");
}

static void t_guard_codec(void)
{
	BtAacGuard g;
	uint8_t b[BTAAC_G_BYTES];

	btaac_guard_default(&g);
	g.session = 'Z'; g.test = 'n'; g.crashes = 2;
	btaac_guard_encode(&g, b);
	CHECK(!memcmp(b, "BAG1Zn2\n", 8), "the record's bytes: %.8s", b);
	memset(&g, 0, sizeof(g));
	CHECK(btaac_guard_decode(&g, b, 8) == 1 && g.session == 'Z' && g.test == 'n' && g.crashes == 2, "read back");
	/* damaged or short: refused, and the defaults */
	static const char *bad[] = { "BAG2KK0\n", "bAG1KK0\n", "BAG1XK0\n", "BAG1KX0\n", "BAG1KK:\n", "BAG1KK/\n", "\0\0\0\0\0\0\0\0" };
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		g.session = 'Z'; g.test = 'N'; g.crashes = 5;
		CHECK(btaac_guard_decode(&g, (const uint8_t *)bad[i], 8) == 0 && g.session == 'K' && g.test == 'K' && g.crashes == 0, "bad record %u", i);
	}
	CHECK(btaac_guard_decode(&g, b, 7) == 0 && g.session == 'K', "a short file starts clean");
	CHECK(btaac_guard_decode(&g, b, 0) == 0 && g.session == 'K', "no file starts clean");
	/* the count is held at the digit's top */
	g.crashes = 200;
	btaac_guard_encode(&g, b);
	CHECK(b[6] == '9', "the count's digit stops at 9");
}

static void t_guard_boot(void)
{
	BtAacGuard g;
	BtAacBoot b;

	/* a clean record: nothing held */
	btaac_guard_default(&g);
	b = btaac_guard_boot(&g);
	CHECK(!b.unclean && !b.hold && !b.neon_banned && !b.aac_banned && g.crashes == 0, "clean");
	/* a session with the C build left running: one unclean, held this start, NEON not blamed */
	btaac_guard_default(&g); g.session = 'S';
	b = btaac_guard_boot(&g);
	CHECK(b.unclean && b.hold && !b.neon_banned && !b.aac_banned && g.crashes == 1 && g.session == 'K' && g.test == 'K', "an S session");
	b = btaac_guard_boot(&g);
	CHECK(!b.unclean && !b.hold && g.crashes == 1, "the start after it may use AAC again");
	/* a session with the NEON build: the NEON build is the one to stop using */
	btaac_guard_default(&g); g.session = 'Z';
	b = btaac_guard_boot(&g);
	CHECK(b.unclean && b.hold && b.neon_banned && !b.aac_banned && g.test == 'N' && g.crashes == 1, "a Z session");
	b = btaac_guard_boot(&g);
	CHECK(!b.hold && b.neon_banned && !b.aac_banned, "the ban stays, AAC with C may run again");
	/* a self-test that was running: the build it was running is banned for good, a C crash takes AAC with it */
	btaac_guard_default(&g); g.test = 'n';
	b = btaac_guard_boot(&g);
	CHECK(!b.unclean && !b.hold && b.neon_banned && !b.aac_banned && g.test == 'N', "NEON test crashed");
	btaac_guard_default(&g); g.test = 'c';
	b = btaac_guard_boot(&g);
	CHECK(b.hold && b.aac_banned && b.neon_banned && g.test == 'A', "C test crashed");
	b = btaac_guard_boot(&g);
	CHECK(b.hold && b.aac_banned && g.test == 'A', "and every start after it");
	/* three sessions in a row that did not end cleanly: AAC off from the third on, however many starts follow */
	btaac_guard_default(&g);
	for (int i = 1; i <= 3; i++) {
		g.session = 'S';
		b = btaac_guard_boot(&g);
		CHECK(b.unclean && b.hold && g.crashes == (uint8_t)i, "crash %d", i);
		if (i < 3) {
			b = btaac_guard_boot(&g);
			CHECK(!b.hold, "a start between two crashes may run AAC");
		}
	}
	for (int i = 0; i < 3; i++) {
		b = btaac_guard_boot(&g);
		CHECK(b.hold && !b.unclean && g.crashes == 3, "held for good once three in a row");
	}
	/* a session that ends cleanly clears the count */
	btaac_guard_default(&g); g.crashes = 2; g.session = 'S';
	btaac_guard_session_end(&g);
	CHECK(g.crashes == 0 && g.session == 'K', "a clean end clears the count");
	btaac_guard_session_begin(&g, 0);
	CHECK(g.session == 'S', "C session byte");
	btaac_guard_session_begin(&g, 1);
	CHECK(g.session == 'Z', "NEON session byte");
	/* the count never runs past its digit */
	btaac_guard_default(&g); g.crashes = BTAAC_CRASH_MAX; g.session = 'S';
	btaac_guard_boot(&g);
	CHECK(g.crashes == BTAAC_CRASH_MAX, "the count stops at its top");
}

static void run_of(BtAacRun *r, int ok, uint32_t aus, uint32_t bytes, uint64_t hash)
{
	memset(r, 0, sizeof(*r));
	r->ok = (uint8_t)ok;
	r->aus = aus;
	r->bytes = bytes;
	r->hash = hash;
	for (uint32_t i = 0; i < BTAAC_ST_FRAMES; i++)
		r->len[i] = (uint16_t)(600 + i);
}

static void t_selftest_decide(void)
{
	BtAacBoot clean = { 0, 0, 0, 0 }, nb = { 0, 0, 1, 0 }, ab = { 0, 1, 1, 1 };
	BtAacRun c, n;

	run_of(&c, 1, BTAAC_ST_FRAMES, 10000, 0x1234);
	run_of(&n, 1, BTAAC_ST_FRAMES, 10000, 0x1234);
	CHECK(btaac_selftest_decide(&clean, &c, &n) == BTAAC_ST_PASS, "identical runs pass");
	n.hash ^= 1;
	CHECK(btaac_selftest_decide(&clean, &c, &n) == BTAAC_ST_NEON_DIFF, "one differing bit of the hash");
	n.hash = c.hash; n.len[7]++;
	CHECK(btaac_selftest_decide(&clean, &c, &n) == BTAAC_ST_NEON_DIFF, "one unit one byte longer");
	n.len[7] = c.len[7]; n.bytes++;
	CHECK(btaac_selftest_decide(&clean, &c, &n) == BTAAC_ST_NEON_DIFF, "one byte more in all");
	n.bytes = c.bytes; n.aus--;
	CHECK(btaac_selftest_decide(&clean, &c, &n) == BTAAC_ST_NEON_DIFF, "one unit fewer");
	n.aus = c.aus; n.ok = 0;
	CHECK(btaac_selftest_decide(&clean, &c, &n) == BTAAC_ST_NEON_DIFF, "the NEON build reported a fault");
	CHECK(btaac_selftest_decide(&clean, &c, NULL) == BTAAC_ST_NEON_DIFF, "no NEON encoder could be made: C only");
	n.ok = 1;
	CHECK(btaac_selftest_decide(&nb, &c, NULL) == BTAAC_ST_NEON_BANNED, "NEON banned by the record");
	CHECK(btaac_selftest_decide(&nb, &c, &n) == BTAAC_ST_NEON_BANNED, "banned even if a run is given");
	CHECK(btaac_selftest_decide(&clean, NULL, NULL) == BTAAC_ST_ERROR, "the test could not run");
	c.ok = 0;
	CHECK(btaac_selftest_decide(&clean, &c, &n) == BTAAC_ST_C_FAIL, "the C build faulted");
	c.ok = 1; c.aus = BTAAC_ST_FRAMES - 1;
	CHECK(btaac_selftest_decide(&clean, &c, &n) == BTAAC_ST_C_FAIL, "the C build gave too few units");
	c.aus = BTAAC_ST_FRAMES;
	CHECK(btaac_selftest_decide(&ab, &c, &n) == BTAAC_ST_C_FAIL, "AAC banned by the record");
	CHECK(btaac_neon_allowed(BTAAC_ST_PASS) && !btaac_neon_allowed(BTAAC_ST_NEON_DIFF) && !btaac_neon_allowed(BTAAC_ST_NEON_BANNED) &&
	      !btaac_neon_allowed(BTAAC_ST_NOT_RUN) && !btaac_neon_allowed(BTAAC_ST_C_FAIL) && !btaac_neon_allowed(BTAAC_ST_ERROR), "NEON only on a pass");
	CHECK(btaac_c_allowed(BTAAC_ST_PASS) && btaac_c_allowed(BTAAC_ST_NEON_DIFF) && btaac_c_allowed(BTAAC_ST_NEON_BANNED) &&
	      !btaac_c_allowed(BTAAC_ST_NOT_RUN) && !btaac_c_allowed(BTAAC_ST_C_FAIL) && !btaac_c_allowed(BTAAC_ST_ERROR), "C once its own test passed");
	/* the stream hash is sensitive to a single byte anywhere and to the order */
	uint8_t a[16], bb[16];
	for (int i = 0; i < 16; i++) a[i] = bb[i] = (uint8_t)(i * 37);
	uint64_t h0 = btaac_fnv(BTAAC_FNV_BASIS, a, 16);
	for (int i = 0; i < 16; i++) {
		bb[i] ^= 1;
		CHECK(btaac_fnv(BTAAC_FNV_BASIS, bb, 16) != h0, "byte %d", i);
		bb[i] ^= 1;
	}
	bb[3] = a[4]; bb[4] = a[3];
	CHECK(btaac_fnv(BTAAC_FNV_BASIS, bb, 16) != h0, "swapped bytes");
}

static void t_signals(void)
{
	int16_t l, r;

	/* the timing runs' noise, frame 0 and 1: from its generator's definition */
	btaac_sig_noise(0, &l, &r);
	CHECK(l == (int16_t)((int16_t)(btaac_hash32(0) >> 16) >> 2) && r == (int16_t)((int16_t)(btaac_hash32(1) >> 16) >> 2), "noise");
	/* the self-test's signal is the same every time, loud enough to matter, and has its click and its silence */
	int32_t peak = 0;
	for (uint32_t n = 0; n < BTAAC_ST_FRAMES * BTAAC_FRAME; n++) {
		btaac_sig_selftest(n, &l, &r);
		if (abs(l) > peak) peak = abs(l);
		if (n == 2560 || n == 2561) CHECK(abs(l) == 20000 && abs(r) == 20000, "the click");
		if (n >= 2700 && n < 3000) CHECK(l == 0 && r == 0, "the silence after the click");
	}
	CHECK(peak >= 20000, "peak %d", peak);
}

/* ---- the rings (single thread) -------------------------------------------------------------------------- */

static int16_t g_blk[512 * 2];

static void fill_block(uint32_t first_frame, uint32_t frames, uint32_t ch, int16_t *dst)
{
	for (uint32_t f = 0; f < frames; f++)
		for (uint32_t c = 0; c < ch; c++)
			dst[f * ch + c] = (int16_t)((first_frame + f) * 7u + c * 13u + 1u);
}

static void t_rings(void)
{
	uint32_t ov, fin, pop, dr, out;

	/* nothing is accepted while no session runs */
	fill_block(0, 512, 2, g_blk);
	CHECK(btaac_push_pcm(g_blk, 512) == 0, "no session: refused");
	btaac_ring_info(&ov, &fin, &pop, &dr, &out);
	CHECK(ov == 0 && fin == 0, "and not counted as an overrun");
	CHECK(btaac_pop_au(NULL, 0, NULL) == 0, "no buffer");

	btaac_ring_open(2);
	CHECK(btaac_push_pcm(NULL, 512) == 0 && btaac_push_pcm(g_blk, 0) == 0 && btaac_push_pcm(g_blk, BTAAC_PCM_RING_FRAMES + 1) == 0, "null, empty and too big blocks refused");
	/* the ring's whole size in blocks fits, the next block is refused whole and counted */
	const uint32_t R = BTAAC_PCM_RING_FRAMES;
	for (uint32_t i = 0; i < R / 512; i++) {
		fill_block((uint32_t)i * 512, 512, 2, g_blk);
		CHECK(btaac_push_pcm(g_blk, 512) == 512, "block %d", i);
	}
	CHECK(btaac_pcm_avail() == R, "full");
	CHECK(btaac_push_pcm(g_blk, 512) == 0, "the one after the last");
	CHECK(btaac_push_pcm(g_blk, 1) == 0, "even one frame");
	btaac_ring_info(&ov, &fin, &pop, &dr, &out);
	CHECK(ov == 2 && fin == R, "overruns %u, frames in %u", ov, fin);
	/* take one frame's worth of 1024: the oldest, in order */
	static int16_t take[1024 * 2];
	btaac_pcm_take(take, 1024);
	for (uint32_t f = 0; f < 1024; f++)
		CHECK(take[2 * f] == (int16_t)(f * 7u + 1u) && take[2 * f + 1] == (int16_t)(f * 7u + 14u), "frame %u", f);
	CHECK(btaac_pcm_avail() == R - 1024, "one frame's worth less");
	/* room for two blocks again; a block that wraps the end of the ring keeps its order */
	fill_block(R, 512, 2, g_blk);
	CHECK(btaac_push_pcm(g_blk, 512) == 512, "refilled");
	fill_block(R + 512, 512, 2, g_blk);
	CHECK(btaac_push_pcm(g_blk, 512) == 512 && btaac_push_pcm(g_blk, 512) == 0, "refilled");
	for (uint32_t i = 0; i < R / 1024 - 1u; i++)
		btaac_pcm_take(take, 1024);
	btaac_pcm_take(take, 1024);
	for (uint32_t f = 0; f < 1024; f++)
		CHECK(take[2 * f] == (int16_t)((R + f) * 7u + 1u), "the wrapped block, frame %u", f);

	/* many wraps: 300 blocks of 512 pushed and taken in turn, every frame checked */
	btaac_ring_open(2);
	uint32_t next_in = 0, next_out = 0;
	for (int i = 0; i < 300; i++) {
		fill_block(next_in, 512, 2, g_blk);
		CHECK(btaac_push_pcm(g_blk, 512) == 512, "block %d", i);
		next_in += 512;
		if (btaac_pcm_avail() >= 1024) {
			btaac_pcm_take(take, 1024);
			for (uint32_t f = 0; f < 1024; f++)
				CHECK(take[2 * f] == (int16_t)((next_out + f) * 7u + 1u) && take[2 * f + 1] == (int16_t)((next_out + f) * 7u + 14u), "frame %u", next_out + f);
			next_out += 1024;
		}
	}
	/* a new session throws away what the last left in the ring */
	btaac_ring_open(2);
	CHECK(btaac_pcm_avail() == 0, "emptied");
	/* mono */
	btaac_ring_open(1);
	for (int i = 0; i < 20; i++) {
		fill_block((uint32_t)i * 512, 512, 1, g_blk);
		CHECK(btaac_push_pcm(g_blk, 512) == 512, "mono block %d", i);
		if (btaac_pcm_avail() >= 1024) {
			btaac_pcm_take(take, 1024);
			for (uint32_t f = 0; f < 1024; f++)
				CHECK(take[f] == (int16_t)((((uint32_t)(i - 1) * 512u) + f) * 7u + 1u), "mono frame %u", f);
		}
	}
	btaac_ring_close();
	CHECK(btaac_push_pcm(g_blk, 512) == 0, "after the session ends nothing is accepted");
}

static void t_au_ring(void)
{
	static uint8_t au[BTAAC_AU_MAX], got[BTAAC_AU_MAX];
	uint32_t seq, n, ov, fin, pop, dr, out;

	btaac_ring_open(2);
	CHECK(btaac_pop_au(got, sizeof(got), &seq) == 0, "empty");
	CHECK(btaac_au_room(), "room");
	CHECK(!btaac_au_put(au, BTAAC_AU_MAX + 1), "a unit larger than a slot is refused");
	/* fill the ring, the next unit is refused */
	const int NU = (int)BTAAC_AU_RING_SLOTS;
	for (int i = 0; i < NU; i++) {
		memset(au, 0x40 + i, 100 + (size_t)i);
		CHECK(btaac_au_put(au, 100 + (uint32_t)i) == 1, "unit %d", i);
	}
	CHECK(!btaac_au_room() && !btaac_au_put(au, 100), "full: refused");
	/* in order, with their numbers and lengths */
	for (int i = 0; i < NU; i++) {
		memset(got, 0, sizeof(got));
		n = btaac_pop_au(got, sizeof(got), &seq);
		CHECK(n == 100u + (uint32_t)i && seq == (uint32_t)i && got[0] == 0x40 + i && got[n - 1] == 0x40 + i && got[n] == 0, "unit %d: %u bytes, number %u", i, n, seq);
	}
	CHECK(btaac_pop_au(got, sizeof(got), &seq) == 0, "empty again");
	CHECK(btaac_pop_au(got, sizeof(got), NULL) == 0, "a null sequence number pointer is allowed");
	/* a buffer too small for the unit: the unit is dropped and counted, the next one is served */
	memset(au, 1, 700); CHECK(btaac_au_put(au, 700), "put");
	memset(au, 2, 300); CHECK(btaac_au_put(au, 300), "put");
	CHECK(btaac_pop_au(got, 400, &seq) == 0, "too small: nothing");
	n = btaac_pop_au(got, 400, &seq);
	CHECK(n == 300 && got[0] == 2 && seq == (uint32_t)NU + 1u, "the next unit, number %u", seq);
	btaac_ring_info(&ov, &fin, &pop, &dr, &out);
	CHECK(dr == 1, "one drop counted: %u", dr);
	/* units left by an earlier session are skipped; the new session's numbers start again at 0 */
	memset(au, 3, 50); CHECK(btaac_au_put(au, 50), "put");
	memset(au, 4, 60); CHECK(btaac_au_put(au, 60), "put");
	btaac_ring_open(2);
	CHECK(btaac_pop_au(got, sizeof(got), &seq) == 0, "the old session's units are dead");
	memset(au, 5, 70); CHECK(btaac_au_put(au, 70), "put");
	n = btaac_pop_au(got, sizeof(got), &seq);
	CHECK(n == 70 && got[0] == 5 && seq == 0, "the new session's first unit: %u bytes, number %u", n, seq);
	/* a ring left full by the old session is not full for the new one: the dead units are taken out by the first pop... */
	for (int i = 0; i < NU; i++) CHECK(btaac_au_put(au, 70), "fill %d", i);
	btaac_ring_open(2);
	CHECK(btaac_pop_au(got, sizeof(got), &seq) == 0 && btaac_au_room(), "dead units cleared by the pop, room again");
	btaac_ring_close();
}

/* a producer thread (the sender's role) and the consumer (the engine's) running flat out */
static volatile int g_stop_prod;
static void *producer(void *a)
{
	static int16_t blk[512 * 2];
	uint32_t next = 0;

	(void)a;
	while (next < 512u * 40000u) {
		fill_block(next, 512, 2, blk);
		if (btaac_push_pcm(blk, 512) == 512)
			next += 512;
		else if (g_stop_prod)
			break;
	}
	return NULL;
}
static void t_ring_threads(void)
{
	pthread_t p;
	static int16_t take[1024 * 2];
	uint32_t next_out = 0;

	btaac_ring_open(2);
	pthread_create(&p, NULL, producer, NULL);
	while (next_out < 512u * 40000u) {
		if (btaac_pcm_avail() >= 1024) {
			btaac_pcm_take(take, 1024);
			for (uint32_t f = 0; f < 1024; f++)
				CHECK(take[2 * f] == (int16_t)((next_out + f) * 7u + 1u) && take[2 * f + 1] == (int16_t)((next_out + f) * 7u + 14u),
				      "frame %u lost or out of order", next_out + f);
			next_out += 1024;
		}
	}
	g_stop_prod = 1;
	pthread_join(p, NULL);
	/* the same for the units: the engine's side puts, the sender's side pops */
	btaac_ring_open(2);
	static uint8_t au[BTAAC_AU_MAX], got[BTAAC_AU_MAX];
	uint32_t sent = 0, recv = 0, seq;
	while (recv < 200000) {
		uint32_t len = 100 + (sent % 700);
		if (sent < 200000) {
			memset(au, (int)(sent & 0xFF), len);
			if (btaac_au_put(au, len))
				sent++;
		}
		uint32_t n = btaac_pop_au(got, sizeof(got), &seq);
		if (n) {
			CHECK(seq == recv && n == 100 + (recv % 700) && got[0] == (uint8_t)(recv & 0xFF) && got[n - 1] == (uint8_t)(recv & 0xFF), "unit %u", recv);
			recv++;
		}
	}
}

/* ---- the engine ----------------------------------------------------------------------------------------- */

static void t_thread_start(void)
{
	engine_up();
	CHECK(g_thread_cpu == 0x80000 && g_thread_cpus_tried[0] == 0x80000 && g_thread_tries == 1, "made on the system core's mask first");
	CHECK(g_thread_prio == 0x70 && g_thread_stack == 0x4000, "priority and stack");
	BtAacInfo i = info();
	CHECK(i.cpu_mask == 0x80000 && i.cpu_mask_err == 0 && i.thread_err == 0 && i.state == BTAAC_S_IDLE && i.selftest == BTAAC_ST_NOT_RUN, "info");
	CHECK(!log_has("mask was refused") && log_has("engine ready (thread mask 0x80000)"), "logged:\n%s", g_log);
	engine_down();
	CHECK(g_thread_deleted == 1 && g_ev_deleted == 1, "thread and event flag deleted");
}

static void t_thread_mask_refused(void)
{
	g_refuse_mask = 1;
	engine_up();
	CHECK(g_thread_tries == 2 && g_thread_cpus_tried[0] == 0x80000 && g_thread_cpus_tried[1] == 0 && g_thread_cpu == 0, "then the default placement");
	BtAacInfo i = info();
	CHECK(i.cpu_mask == 0 && i.cpu_mask_err == 0x80020000u, "mask 0x%x err 0x%x", i.cpu_mask, i.cpu_mask_err);
	CHECK(log_has("system core's mask was refused") && log_has("default placement"), "logged:\n%s", g_log);
	run_selftest();
	CHECK(g_selftest == BTAAC_ST_PASS, "the engine works there too");
	engine_down();
}

static void t_selftest_pass(void)
{
	fs_set(NULL);
	engine_up();
	fs_expect('K', 'K', 0);                       /* the record is written at start, so a record that cannot be is known at once */
	CHECK(g_nwrites == 1, "one write at start");
	run_selftest();
	BtAacInfo i = info();
	CHECK(i.selftest == BTAAC_ST_PASS && i.neon_allowed && !i.hold, "passed");
	/* the markers around the two runs: C running, nothing, NEON running, nothing */
	CHECK(g_nwrites == 5, "writes: %d", g_nwrites);
	CHECK(g_writes[1][5] == 'c' && g_writes[2][5] == 'K' && g_writes[3][5] == 'n' && g_writes[4][5] == 'K', "markers %c %c %c %c", g_writes[1][5], g_writes[2][5], g_writes[3][5], g_writes[4][5]);
	fs_expect('K', 'K', 0);
	CHECK(g_st_aus == BTAAC_ST_FRAMES && g_st_bytes > 5000 && g_st_hash_c == g_st_hash_n && g_st_hash_c != 0 && g_st_c_us && g_st_n_us, "figures");
	CHECK(log_has("self-test passed") && log_has("NEON allowed"), "logged:\n%s", g_log);
	CHECK(g_blocks_made == 2, "two encoders were made, one at a time: %d", g_blocks_made);
	/* asked again it runs again, and asking for the test twice from the worker's pass asks once */
	btaac_sync(1);
	usleep(20000);
	CHECK(g_selftest_runs == 1, "btaac_sync asks only once");
	engine_down();
}

static void t_selftest_off(void)
{
	engine_up();
	btaac_sync(0);
	usleep(20000);
	CHECK(g_selftest == BTAAC_ST_NOT_RUN && g_selftest_runs == 0 && g_blocks_made == 0, "no self-test while Bluetooth Turbo is off");
	engine_down();
}

static void t_selftest_neon_diff(void)
{
	g_fault[1] = F_FLIP_BYTE;
	engine_up();
	run_selftest();
	CHECK(g_selftest == BTAAC_ST_NEON_DIFF && !info().neon_allowed && g_st_hash_c != g_st_hash_n, "a NEON build that changes one byte is found out");
	CHECK(log_has("NEON DIFFERS"), "logged:\n%s", g_log);
	/* sessions run on the C build whatever is asked */
	CHECK(btaac_start(256, 48000, 2, 0, 1, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	CHECK(!info().neon_in_use, "NEON asked for, not allowed: the C build");
	CHECK(g_file[4] == 'S', "and the record says so");
	btaac_stop();
	wait_for(c_idle, "the stop");
	engine_down();
}

static void t_selftest_neon_diff_length(void)
{
	g_fault[1] = F_FLIP_LENGTH;
	engine_up();
	run_selftest();
	CHECK(g_selftest == BTAAC_ST_NEON_DIFF, "a NEON build with one unit a byte short is found out (verdict %u)", g_selftest);
	engine_down();
}

static void t_selftest_neon_diff_last(void)
{
	g_fault[1] = F_FLIP_LAST_AU;
	engine_up();
	run_selftest();
	CHECK(g_selftest == BTAAC_ST_NEON_DIFF, "a difference in the last unit only is found out (verdict %u)", g_selftest);
	engine_down();
}

static void t_selftest_c_fail(void)
{
	g_fault[0] = F_ERROR;
	engine_up();
	run_selftest();
	CHECK(g_selftest == BTAAC_ST_C_FAIL && !btaac_c_allowed((int)g_selftest) && !info().neon_allowed, "the C build failed: no AAC");
	CHECK(g_nwrites == 3 && g_file[5] == 'K', "NEON not run after a C failure, the marker back to K: %d writes", g_nwrites);
	CHECK(btaac_start(256, 48000, 2, 0, 1, 0) == BTAAC_ERR_NOTEST, "and no session");
	engine_down();
}
static void t_selftest_c_assert(void)
{
	g_fault[0] = F_ASSERT;
	engine_up();
	run_selftest();
	CHECK(g_selftest == BTAAC_ST_C_FAIL, "a consistency check of the encoder failed: no AAC");
	CHECK(info().assert_fails >= 1, "counted");
	engine_down();
}
static void t_selftest_c_toobig(void)
{
	g_fault[0] = F_TOO_BIG;
	engine_up();
	run_selftest();
	CHECK(g_selftest == BTAAC_ST_C_FAIL, "a unit larger than the ring's slot: no AAC");
	engine_down();
}
static void t_selftest_no_memory(void)
{
	g_block_fail = 1;
	engine_up();
	run_selftest();
	CHECK(g_selftest == BTAAC_ST_ERROR, "no memory: could not run");
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_ERR_NOTEST, "no session");
	engine_down();
}

static void t_record_unwritable(void)
{
	g_file_write_fails = 1;
	engine_up();
	BtAacInfo i = info();
	CHECK(i.hold && g_hold && !g_guard_ok, "a record that cannot be written holds AAC off");
	CHECK(log_has("crash record cannot be written"), "logged:\n%s", g_log);
	run_selftest();
	CHECK(g_selftest == BTAAC_ST_ERROR && g_blocks_made == 0, "and no encoder is run: not even for the test");
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_ERR_HELD, "held");
	engine_down();
}

static void t_found_session_c(void)
{
	fs_set("SK1");
	engine_up();
	fs_expect('K', 'K', 2);                      /* the unclean session is counted and the record put right at once */
	BtAacInfo i = info();
	CHECK(i.hold && i.crashes == 2 && !g_neon_banned && !g_aac_banned, "held, the count 2, NEON not blamed");
	CHECK(log_has("did not end cleanly (2 in a row)"), "logged:\n%s", g_log);
	run_selftest();
	CHECK(g_selftest == BTAAC_ST_PASS, "the self-test still runs (the C build was not the suspect): %u", g_selftest);
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_ERR_HELD && g_state == BTAAC_S_IDLE, "but no session this start");
	engine_down();
}

static void t_found_session_neon(void)
{
	fs_set("ZK0");
	engine_up();
	fs_expect('K', 'N', 1);                       /* the NEON build is banned */
	BtAacInfo i = info();
	CHECK(i.hold && g_neon_banned && !g_aac_banned, "held, NEON banned");
	run_selftest();
	CHECK(g_selftest == BTAAC_ST_NEON_BANNED && g_st_n_us == 0 && g_st_hash_n == 0, "the NEON build is not run again: verdict %u", g_selftest);
	CHECK(g_nwrites == 3 && g_writes[1][5] == 'c' && g_writes[2][5] == 'N', "only the C run is marked, the ban kept");
	engine_down();
}

static void t_found_selftest_crash(void)
{
	fs_set("Kn0");
	engine_up();
	fs_expect('K', 'N', 0);
	CHECK(!g_hold && g_neon_banned, "a NEON test that did not finish: AAC allowed, NEON banned");
	run_selftest();
	CHECK(g_selftest == BTAAC_ST_NEON_BANNED, "verdict %u", g_selftest);
	CHECK(btaac_start(256, 48000, 2, 0, 1, 0) == BTAAC_OK, "a session with the C build is allowed");
	wait_for(c_running, "the session");
	CHECK(!info().neon_in_use, "on the C build");
	engine_down();
}

static void t_found_c_crash(void)
{
	fs_set("Kc0");
	engine_up();
	fs_expect('K', 'A', 0);
	CHECK(g_hold && g_aac_banned && g_neon_banned, "a C test that did not finish: no AAC at all");
	run_selftest();
	CHECK(g_selftest == BTAAC_ST_C_FAIL && g_blocks_made == 0, "no encoder is run again");
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_ERR_HELD, "held");
	engine_down();
}

static void t_found_third_crash(void)
{
	fs_set("SK2");
	engine_up();
	fs_expect('K', 'K', 3);
	CHECK(g_hold, "the third in a row");
	engine_down();
}

static void t_clear_write_fails_after_c(void)
{
	/* the writes: 0 at start, 1 the 'c' marker, then the clearing writes (both tries) fail: the marker stays on disk, so AAC is held off */
	g_write_fail_from = 2;
	engine_up();
	run_selftest();
	CHECK(g_file[5] == 'c', "the marker is still on disk");
	CHECK(g_hold && !g_guard_ok && info().hold, "held off for this start");
	CHECK(log_has("could not be cleared after the C test"), "logged:\n%s", g_log);
	CHECK(g_blocks_made == 1, "the NEON build was not run after it");
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_ERR_HELD, "no session");
	engine_down();
}

static void t_clear_write_fails_after_neon(void)
{
	g_write_fail_from = 4;                            /* start, 'c', clear, 'n' go through; the clear after the NEON run fails */
	engine_up();
	run_selftest();
	CHECK(g_file[5] == 'n' && g_hold && !g_guard_ok, "NEON marker left: held off");
	CHECK(log_has("could not be cleared after the NEON test"), "logged:\n%s", g_log);
	engine_down();
}

static void t_end_write_fails(void)
{
	engine_up();
	run_selftest();
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	g_write_fail_from = g_nwrites;                    /* the write that ends the session, and its retry, fail */
	btaac_stop();
	wait_for(c_idle, "the stop");
	CHECK(g_file[4] == 'S' && g_hold && !g_guard_ok, "the record still says a session runs: held off rather than counted as a crash next time");
	CHECK(log_has("could not be cleared at the end of the session") && g_live_blocks == 0, "logged, block freed:\n%s", g_log);
	g_write_fail_from = -1;
	engine_down();
}

static void t_request_errors(void)
{
	engine_up();
	CHECK(btaac_start(256, 48000, 2, 0, 1, 0) == BTAAC_ERR_NOTEST, "before the self-test");
	run_selftest();
	CHECK(btaac_start(256, 32000, 2, 0, 1, 0) == BTAAC_ERR_ARG && btaac_start(256, 48000, 3, 0, 1, 0) == BTAAC_ERR_ARG && btaac_start(256, 48000, 0, 0, 1, 0) == BTAAC_ERR_ARG, "bad formats");
	CHECK(btaac_start(256, 48000, 2, 7, 1, 0) == BTAAC_ERR_RATE, "the headphones' maximum below the encoder's lowest (8 kb/s stereo)");
	CHECK(g_state == BTAAC_S_IDLE, "nothing was started by a refused request");
	CHECK(btaac_start(256, 48000, 2, 264, 1, 0) == BTAAC_OK, "a good one");
	CHECK(btaac_start(256, 48000, 2, 264, 1, 0) == BTAAC_ERR_BUSY, "a second at once");
	wait_for(c_running, "the session");
	BtAacInfo i = info();
	CHECK(i.bps == 256000 && i.rate == 48000 && i.channels == 2 && i.neon_in_use, "the session: %u b/s", i.bps);
	CHECK(btaac_start(256, 48000, 2, 264, 1, 0) == BTAAC_ERR_BUSY, "while it runs");
	btaac_stop();
	wait_for(c_idle, "the stop");
	/* the maximum and the clamps decide the rate */
	CHECK(btaac_start(320, 48000, 2, 264, 0, 0) == BTAAC_OK, "320 under 264");
	wait_for(c_running, "the session");
	CHECK(info().bps == 264000 && !info().neon_in_use, "held to the headphones' 264 kb/s, on the C build");
	btaac_stop();
	wait_for(c_idle, "the stop");
	CHECK(btaac_start(900000, 44100, 1, 0, 0, 0) == BTAAC_OK, "an absurd request");
	wait_for(c_running, "the session");
	CHECK(info().bps == 160000 && info().channels == 1 && info().rate == 44100, "brought to mono's top: %u", info().bps);
	btaac_stop();
	wait_for(c_idle, "the stop");
	engine_down();
}

static void t_session_record(void)
{
	engine_up();
	run_selftest();
	g_nwrites = 0;
	CHECK(btaac_start(256, 48000, 2, 0, 1, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	fs_expect('Z', 'K', 0);                        /* running on the NEON build: the record says so */
	CHECK(g_nwrites == 1, "one write");
	CHECK(g_live_blocks == 1, "the encoder's block is held while it runs");
	btaac_stop();
	wait_for(c_idle, "the stop");
	fs_expect('K', 'K', 0);
	CHECK(g_live_blocks == 0, "freed at the stop");
	/* a C session: 'S' */
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	fs_expect('S', 'K', 0);
	engine_down();
	fs_expect('K', 'K', 0);                        /* the module's stop ends it cleanly */
	CHECK(log_has("session started: 256 kb/s, 48000 Hz, 2 channel(s), C build"), "logged:\n%s", g_log);
}

/* The engine thread's priority: one below the audio sender's, read from the thread manager's list at the session's start, put back at its end; the module's own when the sender cannot be
 * found; a refused change leaves the thread where it is. */
static void t_session_priority(void)
{
	engine_up();
	run_selftest();
	CHECK(g_cur_prio == 0x70, "made at the module's own priority");
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	CHECK(g_cur_prio == 0x13 && g_prio_last == 0x13, "the sender is at 0x12: the engine runs at 0x13 (%d)", g_cur_prio);
	BtAacInfo i = info();
	CHECK(i.prio == 0x13 && i.prio_src == BTAAC_PRIO_SRC_COMPUTED, "readings: %u, source %u", (unsigned)i.prio, (unsigned)i.prio_src);
	CHECK(log_has("engine thread priority 0x13 (one below the sender's"), "logged:\n%s", g_log);
	CHECK(g_thread_cpu == 0x80000, "still on the system core");
	btaac_stop();
	wait_for(c_idle, "the stop");
	CHECK(g_cur_prio == 0x70 && g_prio_last == 0x70, "put back to the module's own at the end of the session (%d)", g_cur_prio);
	/* the sender is at another priority */
	g_threads[1].prio = 0x30;
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	CHECK(g_cur_prio == 0x31, "one below 0x30 is 0x31 (%d)", g_cur_prio);
	btaac_stop();
	wait_for(c_idle, "the stop");
	engine_down();
}

/* more threads than the engine reads: the sender among the first 512 is found, one beyond them is not (fallback, logged) */
static void t_session_priority_many_threads(void)
{
	engine_up();
	run_selftest();
	for (int i = 0; i < 700; i++) {
		g_threads[i].name = "SceSomething";
		g_threads[i].prio = 0x40;
	}
	g_nthreads = 700;
	g_threads[300].name = "SceAVConfigDataSend";
	g_threads[300].prio = 0x20;
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	CHECK(g_cur_prio == 0x21 && info().prio_src == BTAAC_PRIO_SRC_COMPUTED, "found among 700 threads: 0x%x", g_cur_prio);
	btaac_stop();
	wait_for(c_idle, "the stop");
	g_threads[300].name = "SceSomething";
	g_threads[650].name = "SceAVConfigDataSend";
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	CHECK(g_cur_prio == 0x70 && info().prio_src == BTAAC_PRIO_SRC_FALLBACK, "beyond the 512 read: the module's own");
	btaac_stop();
	wait_for(c_idle, "the stop");
	engine_down();
}

static void t_session_priority_fallback(void)
{
	engine_up();
	run_selftest();
	/* the sender is not in the list: the module's own priority, said in the log */
	g_threads[1].name = "SceSomethingElse";
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	CHECK(g_cur_prio == 0x70 && info().prio_src == BTAAC_PRIO_SRC_FALLBACK && log_has("the module's own: the sender's priority was not found"), "fallback logged:\n%s", g_log);
	btaac_stop();
	wait_for(c_idle, "the stop");
	/* the list cannot be read */
	g_threads[1].name = "SceAVConfigDataSend";
	g_list_fail = 1;
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	CHECK(g_cur_prio == 0x70 && info().prio_src == BTAAC_PRIO_SRC_FALLBACK, "an unreadable list: the module's own");
	btaac_stop();
	wait_for(c_idle, "the stop");
	/* the change is refused: the thread stays where it is, the session runs, the log says so */
	g_list_fail = 0;
	g_prio_refuse = 1;
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	CHECK(g_cur_prio == 0x70 && log_has("priority change to 0x13 was refused") && info().state == BTAAC_S_RUNNING, "a refused change leaves the thread alone:\n%s", g_log);
	btaac_stop();
	wait_for(c_idle, "the stop");
	engine_down();
}

static void t_session_clears_count(void)
{
	fs_set("KK2");
	engine_up();
	CHECK(!g_hold && g_crashes == 2, "the start after: AAC allowed, two in a row on record");
	run_selftest();
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	btaac_stop();
	wait_for(c_idle, "the stop");
	fs_expect('K', 'K', 0);
	CHECK(g_crashes == 0 && info().crashes == 0, "a session that ended cleanly clears the count");
	engine_down();
}

static void t_session_failures(void)
{
	/* a failed encode call ends the session cleanly: the block is freed, the record is put right, a new session may be asked for */
	g_fault[0] = F_NONE;
	engine_up();
	run_selftest();
	g_fault[0] = F_ERROR;
	g_calls[0] = 0;
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	static int16_t blk[512 * 2];
	for (int i = 0; i < 40 && !c_failed(); i++) {
		fill_block((uint32_t)i * 512, 512, 2, blk);
		btaac_push_pcm(blk, 512);
		usleep(2000);
	}
	wait_for(c_failed, "the failed session");
	CHECK(g_last_error == BTAAC_E_ENCODE && g_live_blocks == 0, "an encode error ends it, the block freed (error %d)", (int)g_last_error);
	fs_expect('K', 'K', 0);
	CHECK(btaac_push_pcm(blk, 512) == 0, "nothing is accepted after it");
	CHECK(log_has("encoder error 3"), "logged:\n%s", g_log);
	g_fault[0] = F_NONE;
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "a new session may be asked for after a failed one");
	wait_for(c_running, "the new session");
	btaac_stop();
	wait_for(c_idle, "the stop");
	engine_down();
}

static void t_session_assert(void)
{
	engine_up();
	run_selftest();
	g_fault[0] = F_ASSERT;
	g_calls[0] = 0;
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	static int16_t blk[512 * 2];
	for (int i = 0; i < 40 && !c_failed(); i++) {
		fill_block((uint32_t)i * 512, 512, 2, blk);
		btaac_push_pcm(blk, 512);
		usleep(2000);
	}
	wait_for(c_failed, "the failed session");
	CHECK(g_last_error == BTAAC_E_ASSERT && g_live_blocks == 0, "a failed consistency check ends the session (error %d)", (int)g_last_error);
	engine_down();
}

static void t_session_no_memory(void)
{
	engine_up();
	run_selftest();
	g_block_fail = 1;
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "asked");
	wait_for(c_failed, "the failed start");
	CHECK(g_last_error == BTAAC_E_MEMORY && g_live_blocks == 0, "no block: the session fails cleanly (error %d)", (int)g_last_error);
	fs_expect('K', 'K', 0);
	g_block_fail = 0;
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "and a later request works");
	wait_for(c_running, "the session");
	engine_down();
}

static void t_session_record_fails_midway(void)
{
	engine_up();
	run_selftest();
	g_file_write_fails = 1;
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "asked");
	wait_for(c_failed, "the failed start");
	CHECK(g_live_blocks == 0 && g_last_error == BTAAC_E_INIT, "a session that cannot be recorded does not run");
	engine_down();
}

static void t_start_stop_together(void)
{
	engine_up();
	run_selftest();
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	btaac_stop();                                    /* in the same batch: the session is made and ended */
	for (int i = 0; i < 400; i++) {
		if (g_state == BTAAC_S_IDLE && g_log[0] && strstr(g_log, "session started"))
			break;
		usleep(1000);
	}
	usleep(30000);
	CHECK(g_state == BTAAC_S_IDLE && g_live_blocks == 0, "stopped (state %u)", g_state);
	fs_expect('K', 'K', 0);
	engine_down();
}

static void t_stop_idle(void)
{
	engine_up();
	btaac_stop();
	usleep(20000);
	CHECK(g_state == BTAAC_S_IDLE, "a stop with no session does nothing");
	engine_down();
}

static void t_module_entry(void)
{
	const BtAacApi *a = btaacGetApi();

	/* the table the main module reads: its size and version, every function, and the same table every time */
	CHECK(a == btaacGetApi() && a->size == sizeof(BtAacApi) && a->version == BTAAC_API_VERSION, "the table");
	CHECK(a->start == btaac_start && a->stop == btaac_stop && a->sync == btaac_sync && a->get_info == btaac_get_info &&
	      a->push_pcm == btaac_push_pcm && a->pop_au == btaac_pop_au, "the table's functions are the engine's");
	CHECK(module_start(0, NULL) == SCE_KERNEL_START_SUCCESS && g_th >= 0, "module start makes the thread");
	CHECK(log_has("AAC module"), "logged:\n%s", g_log);
	run_selftest();
	CHECK(a->start(256, 48000, 2, 0, 1, 0) == BTAAC_OK, "a session through the table");
	wait_for(c_running, "the session");
	CHECK(module_stop(0, NULL) == SCE_KERNEL_STOP_CANCEL, "module stop asks not to be unloaded: the main module keeps the table");
	fs_expect('K', 'K', 0);
	CHECK(g_live_blocks == 0, "the engine is stopped and its memory freed");
	/* and the engine stays safe to call afterwards */
	static int16_t blk[512 * 2];
	static uint8_t au[BTAAC_AU_MAX];
	CHECK(a->push_pcm(blk, 512) == 0 && a->pop_au(au, sizeof(au), NULL) == 0 && a->start(256, 48000, 2, 0, 0, 0) == BTAAC_ERR_THREAD, "calls after the stop give and take nothing");
	a->stop();
	a->sync(1);
}

static void t_no_thread(void)
{
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_ERR_THREAD, "no thread before init");
	btaac_stop();
	btaac_sync(1);
	btaac_exit();                                    /* all harmless */
}

/* ---- the real encoder through the engine ------------------------------------------------------------------ */

/* Pushes `frames` frames of the timing runs' noise (then silence for `tail` frames) in 512-frame blocks, as the sender will, taking every
 * unit out as it comes; returns when all of it has been encoded. */
typedef struct Stream { uint32_t aus, bytes, min, max, first_seq_bad; } Stream;
static Stream stream_noise(uint32_t frames, uint32_t tail)
{
	static int16_t blk[512 * 2];
	static uint8_t got[BTAAC_AU_MAX];
	Stream s = { 0, 0, 0xFFFFFFFFu, 0, 0 };
	uint32_t pushed = 0, total = frames + tail, seq;

	while (pushed < total || s.aus < total / BTAAC_FRAME) {
		if (pushed < total) {
			for (uint32_t f = 0; f < 512; f++) {
				if (pushed + f < frames)
					btaac_sig_noise(pushed + f, &blk[2 * f], &blk[2 * f + 1]);
				else
					blk[2 * f] = blk[2 * f + 1] = 0;
			}
			if (btaac_push_pcm(blk, 512) == 512)
				pushed += 512;
		}
		uint32_t n = btaac_pop_au(got, sizeof(got), &seq);

		if (n) {
			if (seq != s.aus)
				s.first_seq_bad++;
			s.aus++;
			s.bytes += n;
			if (n < s.min) s.min = n;
			if (n > s.max) s.max = n;
		} else {
			usleep(500);
		}
	}
	return s;
}

/* A timing run: the same noise, 10 s, 470 units, and a stream of 320,853 bytes at
 * 256 kb/s and 401,066 at 320 (its ADTS files, whose 7-byte headers are paid for out of the bit budget: a raw unit is the same stream's
 * bytes with the header's share added back, so the totals equal). The smallest and largest unit are this build's own figures, kept
 * so any change of the encoder or of how the engine feeds it shows. */
static void reference_figures(uint32_t kbps, uint32_t bytes, uint32_t min, uint32_t max)
{
	engine_up();
	run_selftest();
	CHECK(btaac_start(kbps, 48000, 2, 0, 1, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	CHECK(g_enc.allocs == 8 && g_enc.used < BTAAC_HEAP, "the encoder takes 8 allocations (%u, %u bytes)", (unsigned)g_enc.allocs, (unsigned)g_enc.used);
	/* the timing run's 10 seconds: 468 frames of noise and two of silence, one unit for each */
	Stream s = stream_noise(468u * BTAAC_FRAME, 2u * BTAAC_FRAME);
	CHECK(s.aus == 470 && s.first_seq_bad == 0, "%u units, %u out of order", s.aus, s.first_seq_bad);
	CHECK(s.bytes == bytes && s.min == min && s.max == max, "%u kb/s: %u bytes, %u to %u (the console's: %u, %u to %u)", kbps, s.bytes, s.min, s.max, bytes, min, max);
	BtAacInfo i = info();
	CHECK(i.aus_out == 470 && i.aus_popped == 470 && i.au_drops == 0 && i.pcm_frames_in == 470u * BTAAC_FRAME, "counters: %u out, %u popped, %u in", i.aus_out, i.aus_popped, i.pcm_frames_in);
	CHECK(i.enc_max_us > 0 && i.enc_mean_us > 0, "timing counters");
	btaac_stop();
	wait_for(c_idle, "the stop");
	engine_down();
}
static void t_reference_256(void) { reference_figures(256, 320853, 647, 840); }
static void t_reference_320(void) { reference_figures(320, 401066, 778, 929); }

static void t_reference_c_build(void)
{
	/* the same on the build that is not NEON: the engine is the same code, only the entry point differs */
	engine_up();
	run_selftest();
	CHECK(btaac_start(256, 48000, 2, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	CHECK(!info().neon_in_use, "C");
	Stream s = stream_noise(468u * BTAAC_FRAME, 2u * BTAAC_FRAME);
	CHECK(s.aus == 470 && s.bytes == 320853 && s.min == 647 && s.max == 840, "C build: %u units %u bytes", s.aus, s.bytes);
	engine_down();
}

static void t_mono(void)
{
	engine_up();
	run_selftest();
	CHECK(btaac_start(128, 44100, 1, 0, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	static int16_t blk[512];
	static uint8_t got[BTAAC_AU_MAX];
	uint32_t pushed = 0, aus = 0, n, seq;

	while (aus < 20) {
		if (pushed < 512u * 80) {
			for (uint32_t f = 0; f < 512; f++) {
				int16_t l, r;
				btaac_sig_noise(pushed + f, &l, &r);
				blk[f] = l;
			}
			if (btaac_push_pcm(blk, 512) == 512)
				pushed += 512;
		}
		n = btaac_pop_au(got, sizeof(got), &seq);
		if (n)
			aus++;
		else
			usleep(500);
	}
	CHECK(info().channels == 1 && info().bps == 128000, "mono session");
	engine_down();
}

static void t_quit_with_session(void)
{
	engine_up();
	run_selftest();
	CHECK(btaac_start(256, 48000, 2, 0, 1, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	static int16_t blk[512 * 2];
	for (int i = 0; i < 6; i++) {
		fill_block((uint32_t)i * 512, 512, 2, blk);
		btaac_push_pcm(blk, 512);
	}
	engine_down();                                    /* the module's stop while encoding */
	fs_expect('K', 'K', 0);
	CHECK(g_live_blocks == 0, "block freed");
	CHECK(btaac_push_pcm(blk, 512) == 0, "no more input accepted");
}

/* ---- the harness ----------------------------------------------------------------------------------------- */

typedef struct { const char *name; void (*fn)(void); } Scn;
static const Scn k_scn[] = {
	{ "bit rate clamps", t_clamp },
	{ "crash record codec", t_guard_codec },
	{ "crash record at start", t_guard_boot },
	{ "self-test verdicts", t_selftest_decide },
	{ "test signals", t_signals },
	{ "PCM ring", t_rings },
	{ "unit ring", t_au_ring },
	{ "rings across two threads", t_ring_threads },
	{ "thread made on the system core", t_thread_start },
	{ "thread falls back when the mask is refused", t_thread_mask_refused },
	{ "no thread: calls are harmless", t_no_thread },
	{ "module entry: the table, start, stop", t_module_entry },
	{ "self-test passes, record marked around its runs", t_selftest_pass },
	{ "no self-test while off", t_selftest_off },
	{ "NEON output differs by a byte", t_selftest_neon_diff },
	{ "NEON output differs by a length", t_selftest_neon_diff_length },
	{ "NEON output differs in the last unit", t_selftest_neon_diff_last },
	{ "C build fails (error)", t_selftest_c_fail },
	{ "C build fails (consistency check)", t_selftest_c_assert },
	{ "C build fails (unit too big)", t_selftest_c_toobig },
	{ "self-test without memory", t_selftest_no_memory },
	{ "crash record cannot be written", t_record_unwritable },
	{ "start finds a C session left running", t_found_session_c },
	{ "start finds a NEON session left running", t_found_session_neon },
	{ "start finds a NEON self-test left running", t_found_selftest_crash },
	{ "start finds a C self-test left running", t_found_c_crash },
	{ "third unclean session", t_found_third_crash },
	{ "clearing write fails after the C test", t_clear_write_fails_after_c },
	{ "clearing write fails after the NEON test", t_clear_write_fails_after_neon },
	{ "write at the end of a session fails", t_end_write_fails },
	{ "request errors and bit rate", t_request_errors },
	{ "session marks the record", t_session_record },
	{ "session: the engine thread's priority, one below the sender's", t_session_priority },
	{ "session: the priority with more threads than are read", t_session_priority_many_threads },
	{ "session: the priority's fallbacks (no sender, no list, a refused change)", t_session_priority_fallback },
	{ "clean session clears the count", t_session_clears_count },
	{ "failed encode ends the session cleanly", t_session_failures },
	{ "failed consistency check ends the session", t_session_assert },
	{ "no memory for a session", t_session_no_memory },
	{ "record fails when a session starts", t_session_record_fails_midway },
	{ "start and stop in one batch", t_start_stop_together },
	{ "stop with no session", t_stop_idle },
	{ "module stop during a session", t_quit_with_session },
	{ "figures of the console's timing run, 256 kb/s", t_reference_256 },
	{ "figures of the console's timing run, 320 kb/s", t_reference_320 },
	{ "C build, same figures", t_reference_c_build },
	{ "mono session", t_mono },
};

int main(void)
{
	int failed = 0;

	for (unsigned i = 0; i < sizeof(k_scn) / sizeof(k_scn[0]); i++) {
		pid_t pid;
		int st;

		fflush(stdout);
		pid = fork();
		if (pid == 0) {
			k_scn[i].fn();
			_exit(0);
		}
		waitpid(pid, &st, 0);
		if (!WIFEXITED(st) || WEXITSTATUS(st)) {
			printf("  FAIL  %s\n", k_scn[i].name);
			failed++;
		} else {
			printf("  ok    %s\n", k_scn[i].name);
		}
	}
	if (failed) {
		printf("%d scenario(s) failed\n", failed);
		return 1;
	}
	printf("all %u scenarios passed\n", (unsigned)(sizeof(k_scn) / sizeof(k_scn[0])));
	return 0;
}
