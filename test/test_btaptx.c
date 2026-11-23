/*
 * test_btaptx.c - host test of the aptX add-on (kernel/btaptx.c, btaptx_leaf.c, btaptx_core.h) with the vendored encoders (third_party/aptxenc, Apache-2.0, as
 * the module builds them), the console's file calls replaced by a fake record file.
 *
 *   make test      (the encoders are built to build/aptxenc_host/ first; with -DBAD_HASH_BUILD the test is the failed self-test's)
 *
 * What is proved:
 *   - the crash record: every byte pattern decodes or is refused; what a start does with a session or a self-test the last start left running; three in
 *     a row hold aptX off; a clean session clears the count;
 *   - the encoder: the 512-frame block becomes exactly 512 bytes; a stream cut into blocks is byte for byte the stream encoded whole; the self-test's hash is the
 *     one FFmpeg's aptX encoder (a black box: its command-line tool only) gives for the same signal, and with ffmpeg on the machine the host checks that, and a
 *     longer stream, bit for bit against it and decodes ours with it for the signal-to-noise ratio and the delay (the comparisons use signals well below full scale:
 *     the vendored encoders clip a near-full-scale input differently from FFmpeg's, so a bit-exact check there would measure the clipping, not the codec);
 *   - the encoders' states fit the module's two buffers;
 *   - the engine: no session before the self-test passed, the record marked while a session runs and cleared when it ends (and by module stop), a session
 *     refused when held, banned, busy, or when the record cannot be written; a second session starts from a reset encoder (same bytes again); a failed
 *     self-test (the second build of this test) keeps aptX off.
 * Each scenario runs in its own process: the module keeps its state in statics.
 */
#define BTT_HOST_TEST 1
#include <btt_host.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <math.h>

#pragma GCC diagnostic ignored "-Wunused-function"   /* the failed-self-test build runs one scenario only */

#include "../kernel/btaptx_leaf.c"
#include "../kernel/btaptx.c"

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

/* ---- the fake kernel ---------------------------------------------------------------------------------------- */
static char g_log[8192];
void btt_klog_init(void) {}
void btt_klog_line(const char *line)
{
	if (strlen(g_log) + strlen(line) + 2 < sizeof(g_log)) {
		strcat(g_log, line);
		strcat(g_log, "\n");
	}
}
static int log_has(const char *needle) { return strstr(g_log, needle) != NULL; }

static uint8_t g_file[16];
static int g_file_len, g_file_exists, g_write_fails, g_pos, g_nwrites;
static uint8_t g_writes[64][BTAPTX_G_BYTES];
SceUID ksceIoOpen(const char *path, int flags, int mode)
{
	(void)mode;
	if (strcmp(path, BTT_DIR "/aptx.guard"))
		return -1;
	if (!g_file_exists) {
		if (!(flags & SCE_O_CREAT) || g_write_fails)
			return -1;
		g_file_exists = 1;
		g_file_len = 0;
	}
	CHECK(!(flags & SCE_O_TRUNC), "the crash record is written in place, never truncated");
	g_pos = 0;
	return 5;
}
int ksceIoRead(SceUID fd, void *buf, SceSize n) { (void)fd; if ((int)n > g_file_len) n = (SceSize)g_file_len; memcpy(buf, g_file, n); return (int)n; }
int ksceIoWrite(SceUID fd, const void *buf, SceSize n)
{
	(void)fd;
	if (g_write_fails)
		return -1;
	CHECK(n == BTAPTX_G_BYTES && g_nwrites < 64, "the record is written whole, in one write");
	memcpy(g_file + g_pos, buf, n);
	g_pos += (int)n;
	if (g_pos > g_file_len)
		g_file_len = g_pos;
	memcpy(g_writes[g_nwrites++], buf, n);
	return (int)n;
}
int ksceIoClose(SceUID fd) { (void)fd; return 0; }
int ksceIoMkdir(const char *path, int mode) { (void)path; (void)mode; return 0; }
uint32_t ksceKernelGetSystemTimeLow(void) { static uint32_t t; return t += 100; }

static void preload(const char *s)   /* the record as the last start left it */
{
	memcpy(g_file, s, BTAPTX_G_BYTES);
	g_file_len = BTAPTX_G_BYTES;
	g_file_exists = 1;
}
static int file_is(const char *s) { return g_file_len == BTAPTX_G_BYTES && !memcmp(g_file, s, BTAPTX_G_BYTES); }

/* a stream of `frames` frames of a music-like signal (the self-test's signal at half level, with noise: its peak stays under 0.5 of full scale, where the vendored
 * encoders and FFmpeg's give the same bytes; they clip a near-full-scale input differently) */
static void make_pcm(int16_t *pcm, uint32_t frames)
{
	for (uint32_t n = 0; n < frames; n++) {
		int16_t l, r;

		btaptx_sig(n & 1023u, &l, &r);
		pcm[2 * n] = (int16_t)(l / 2 + (int16_t)(btaptx_hash32(n) >> 25));
		pcm[2 * n + 1] = (int16_t)(r / 2 - (int16_t)(btaptx_hash32(n ^ 0xABCDu) >> 25));
	}
}

static void check_headroom(const int16_t *pcm, uint32_t frames)   /* a bit-exact comparison with FFmpeg is only valid away from clipping */
{
	int32_t peak = 0;

	for (uint32_t n = 0; n < frames * 2u; n++) {
		int32_t v = pcm[n] < 0 ? -pcm[n] : pcm[n];

		if (v > peak)
			peak = v;
	}
	CHECK(peak < 16384, "the comparison signal's peak %d is under half of full scale", (int)peak);
}

/* A whole stream through an encoder of the library directly (its own state, the whole stream in one pass, the bytes packed by this test's own code), the reference for
 * the leaf's block by block output. */
static void reference_stream(int hd, const int16_t *pcm, uint32_t frames, uint8_t *out)
{
	static uint8_t mem[BTAPTX_STATE_MAX] __attribute__((aligned(16)));
	int32_t l[4], r[4];
	uint32_t w[2];

	CHECK((hd ? aptxhdbtenc_init(mem, 0) : aptxbtenc_init(mem, 0)) == 0, "the reference encoder");
	for (uint32_t f = 0; f < frames; f += 4) {
		for (uint32_t k = 0; k < 4; k++) {
			l[k] = (int32_t)pcm[(f + k) * 2] * (hd ? 256 : 1);
			r[k] = (int32_t)pcm[(f + k) * 2 + 1] * (hd ? 256 : 1);
		}
		if (hd) {
			CHECK(aptxhdbtenc_encodestereo(mem, l, r, w) == 0, "the HD reference");
			for (uint32_t c = 0; c < 2; c++)
				for (uint32_t b = 0; b < 3; b++)
					out[(f / 4) * 6 + c * 3 + b] = (uint8_t)(w[c] >> (16 - 8 * b));
		} else {
			uint16_t h[2];

			CHECK(aptxbtenc_encodestereo(mem, l, r, h) == 0, "the reference");
			for (uint32_t c = 0; c < 2; c++) {
				out[f + c * 2] = (uint8_t)(h[c] >> 8);
				out[f + c * 2 + 1] = (uint8_t)h[c];
			}
		}
	}
}

static uint8_t *encode_stream(const int16_t *pcm, uint32_t blocks)
{
	uint8_t *out = malloc((size_t)blocks * BTAPTX_BLOCK_BYTES);

	for (uint32_t b = 0; b < blocks; b++)
		CHECK(btaptx_encode(pcm + (size_t)b * BTAPTX_BLOCK_FRAMES * 2u, out + (size_t)b * BTAPTX_BLOCK_BYTES) == BTAPTX_BLOCK_BYTES, "block %u", (unsigned)b);
	return out;
}

static void up(void)   /* the module as the console loads it, the self-test run, a session started */
{
	btaptx_init();
	btaptx_sync(1);
}

/* ---- the scenarios ----------------------------------------------------------------------------------------- */

static void t_guard_table(void)
{
	BtAptxGuard g;
	BtAptxBoot b;
	uint8_t buf[BTAPTX_G_BYTES];
	static const char *bad[] = { "XPG1KK0\n", "BPG2KK0\n", "BPG1XK0\n", "BPG1KX0\n", "BPG1KK:\n", "BPG1KKa\n", "bpg1KK0\n" };

	CHECK(btaptx_guard_decode(&g, (const uint8_t *)"BPG1KK0\n", 8) && g.session == 'K' && g.test == 'K' && g.count == 0, "a clean record");
	CHECK(btaptx_guard_decode(&g, (const uint8_t *)"BPG1St9\n", 8) && g.session == 'S' && g.test == 't' && g.count == 9, "every field");
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		CHECK(!btaptx_guard_decode(&g, (const uint8_t *)bad[i], 8) && g.session == 'K' && g.test == 'K' && g.count == 0, "damaged record %u starts afresh", i);
	CHECK(!btaptx_guard_decode(&g, (const uint8_t *)"BPG1KK0", 3) && !btaptx_guard_decode(&g, (const uint8_t *)"", 0), "short");
	btaptx_guard_default(&g);
	g.session = 'S'; g.test = 'B'; g.count = 12;
	btaptx_guard_encode(&g, buf);
	CHECK(!memcmp(buf, "BPG1SB9\n", 8), "the count never passes its digit: %.8s", buf);
	/* the boot decision */
	btaptx_guard_default(&g);
	b = btaptx_guard_boot(&g);
	CHECK(!b.hold && !b.hold_now && !b.banned && g.count == 0, "clean");
	btaptx_guard_default(&g); g.session = 'S';
	b = btaptx_guard_boot(&g);
	CHECK(b.hold && b.hold_now && !b.banned && g.count == 1 && g.session == 'K', "a session left running holds this start and counts");
	btaptx_guard_default(&g); g.count = 1;
	b = btaptx_guard_boot(&g);
	CHECK(!b.hold && g.count == 1, "one unclean session earlier, none now: not held");
	btaptx_guard_default(&g); g.count = 2; g.session = 'S';
	b = btaptx_guard_boot(&g);
	CHECK(b.hold && g.count == 3, "the third in a row");
	btaptx_guard_default(&g); g.count = 3;
	b = btaptx_guard_boot(&g);
	CHECK(b.hold && !b.hold_now, "three in a row hold every start");
	btaptx_guard_default(&g); g.test = 't';
	b = btaptx_guard_boot(&g);
	CHECK(b.banned && b.hold && g.test == 'B', "a self-test left running bans the encoder");
	btaptx_guard_default(&g); g.test = 'B';
	b = btaptx_guard_boot(&g);
	CHECK(b.banned && b.hold, "a ban stays");
	btaptx_guard_default(&g); g.count = 5;
	btaptx_guard_session_begin(&g);
	CHECK(g.session == 'S' && g.count == 5, "a session begins");
	btaptx_guard_session_end(&g);
	CHECK(g.session == 'K' && g.count == 0, "a clean end clears the count");
	CHECK(btaptx_packet_fits(512) && btaptx_packet_fits(672) && !btaptx_packet_fits(511) && !btaptx_packet_fits(0), "the packet's size against the link's limit");
	printf("  ok   the crash record\n");
}

static void t_state_size(void)
{
	/* the encoders' records against the module's two buffers (the console's are 4,588 and 4,840 bytes, a 64-bit host's 5,008 and 5,256) */
	CHECK(SizeofAptxbtenc() > 0 && SizeofAptxbtenc() <= (int)BTAPTX_STATE_MAX, "the classic record is %d bytes, room %u", SizeofAptxbtenc(), (unsigned)BTAPTX_STATE_MAX);
	CHECK(SizeofAptxhdbtenc() > 0 && SizeofAptxhdbtenc() <= (int)BTAPTX_STATE_MAX, "the HD record is %d bytes, room %u", SizeofAptxhdbtenc(), (unsigned)BTAPTX_STATE_MAX);
	up();
	CHECK(btaptx_ctx && btaptx_ctx_hd && btaptx_ctx != btaptx_ctx_hd, "two states, one for each mode");
	CHECK(log_has("engine ready"), "logged");
	printf("  ok   both encoders' states (%d and %d bytes) fit the module's buffers of %u\n", SizeofAptxbtenc(), SizeofAptxhdbtenc(), (unsigned)BTAPTX_STATE_MAX);
}

static void t_selftest_signal(void)
{
	up();
	CHECK(btaptx_ctx, "the state is made");
	BtAptxInfo i;
	btaptx_get_info(&i);
	CHECK(i.selftest == BTAPTX_ST_PASS && i.st_bytes == 1024 && ((uint64_t)i.hash_hi << 32 | i.hash_lo) == BTAPTX_ST_HASH, "self-test %u, %u bytes, hash %08x%08x", i.selftest, i.st_bytes,
	      i.hash_hi, i.hash_lo);
	CHECK(log_has("self-test passed"), "logged");
	CHECK(file_is("BPG1KK0\n"), "the record is clean after the self-test: %.8s", g_file);
	CHECK(g_nwrites == 3 && !memcmp(g_writes[0], "BPG1KK0\n", 8) && !memcmp(g_writes[1], "BPG1Kt0\n", 8) && !memcmp(g_writes[2], "BPG1KK0\n", 8), "the record: start, test running, test over");
	btaptx_sync(1);
	CHECK(i.st_runs == 1 && g_nwrites == 3, "the self-test runs once");
	printf("  ok   the self-test passes with the hash of the black box's bytes\n");
}

static void t_lifecycle(void)
{
	int16_t pcm[BTAPTX_BLOCK_FRAMES * 2u];
	uint8_t out[BTAPTX_BLOCK_BYTES];
	BtAptxInfo i;

	btaptx_init();
	make_pcm(pcm, BTAPTX_BLOCK_FRAMES);
	CHECK(btaptx_start(0) == BTAPTX_ERR_NOTEST, "no session before the self-test passed");
	CHECK(btaptx_encode(pcm, out) == 0, "no encoding without a session");
	btaptx_sync(0);
	CHECK(btaptx_start(0) == BTAPTX_ERR_NOTEST, "the switch off does not run it");
	btaptx_sync(1);
	CHECK(btaptx_start(0) == BTAPTX_OK && file_is("BPG1SK0\n"), "a session marks the record: %.8s", g_file);
	CHECK(btaptx_start(0) == BTAPTX_ERR_BUSY, "one session at a time");
	CHECK(btaptx_selftest_run() == BTAPTX_ST_NORUN, "no self-test while a session runs");
	CHECK(btaptx_encode(pcm, out) == BTAPTX_BLOCK_BYTES && btaptx_encode(NULL, out) == 0 && btaptx_encode(pcm, NULL) == 0, "a block is 512 bytes; no pointers, no work");
	btaptx_get_info(&i);
	CHECK(i.state == BTAPTX_S_RUNNING && i.blocks == 1 && i.sessions == 1, "running, 1 block");
	int w = g_nwrites;
	btaptx_stop();
	CHECK(file_is("BPG1KK0\n") && g_nwrites == w + 1, "a clean end clears the record: %.8s", g_file);
	btaptx_stop();
	btaptx_exit();
	CHECK(g_nwrites == w + 1, "a stop with no session writes nothing");
	CHECK(btaptx_encode(pcm, out) == 0, "no encoding after the session");
	CHECK(btaptx_start(0) == BTAPTX_OK, "a new session");
	btaptx_exit();   /* the module's stop ends it */
	CHECK(file_is("BPG1KK0\n"), "the module's stop ends the session cleanly");
	btaptx_get_info(&i);
	CHECK(i.state == BTAPTX_S_IDLE, "idle");
	printf("  ok   the engine's lifecycle\n");
}

static void t_held(void)
{
	BtAptxInfo i;

	preload("BPG1SK0\n");   /* the console went down in a session */
	up();
	btaptx_get_info(&i);
	CHECK(i.selftest == BTAPTX_ST_NORUN && i.hold && i.crashes == 1 && btaptx_start(0) == BTAPTX_ERR_HELD, "held for this start");
	CHECK(log_has("did not end cleanly") && file_is("BPG1KK1\n"), "logged, the count kept: %.8s", g_file);
	printf("  ok   a session left running holds aptX off\n");
}

static void t_held_three(void)
{
	BtAptxInfo i;

	preload("BPG1KK3\n");
	up();
	btaptx_get_info(&i);
	CHECK(i.hold && i.selftest == BTAPTX_ST_NORUN && btaptx_start(0) == BTAPTX_ERR_HELD && log_has("held off by the crash record"), "three in a row hold every start");
	printf("  ok   three unclean sessions in a row hold every start\n");
}

static void t_one_before(void)
{
	BtAptxInfo i;

	preload("BPG1KK1\n");
	up();
	btaptx_get_info(&i);
	CHECK(!i.hold && i.selftest == BTAPTX_ST_PASS && btaptx_start(0) == BTAPTX_OK, "one unclean session earlier does not hold");
	btaptx_stop();
	CHECK(file_is("BPG1KK0\n"), "a clean session clears the count");
	printf("  ok   a clean session clears the count\n");
}

static void t_banned(void)
{
	BtAptxInfo i;

	preload("BPG1Kt0\n");   /* the console went down inside the self-test */
	up();
	btaptx_get_info(&i);
	CHECK(i.banned && i.hold && i.selftest == BTAPTX_ST_NORUN && btaptx_start(0) == BTAPTX_ERR_HELD && file_is("BPG1KB0\n"), "banned: %.8s", g_file);
	printf("  ok   a self-test that took the console down bans the encoder\n");
}

static void t_unwritable(void)
{
	BtAptxInfo i;

	g_write_fails = 1;
	up();
	btaptx_get_info(&i);
	CHECK(!i.guard_ok && i.selftest == BTAPTX_ST_NORUN && btaptx_start(0) == BTAPTX_ERR_RECORD + 0 + (BTAPTX_ERR_NOTEST - BTAPTX_ERR_RECORD) && log_has("cannot be written"),
	      "no encoder without a record that can be written");
	printf("  ok   no encoder runs when the crash record cannot be written\n");
}

static void t_record_fails_later(void)
{
	up();
	g_write_fails = 1;
	CHECK(btaptx_start(0) == BTAPTX_ERR_RECORD, "a session needs its record");
	g_write_fails = 0;
	CHECK(btaptx_start(0) == BTAPTX_OK, "and starts once it can be written");
	btaptx_stop();
	printf("  ok   a session is refused when its record cannot be written\n");
}

static void t_blocks_match_whole(void)
{
	enum { BLOCKS = 40 };
	static int16_t pcm[BLOCKS * BTAPTX_BLOCK_FRAMES * 2u];
	static uint8_t ref[BLOCKS * BTAPTX_BLOCK_BYTES];

	make_pcm(pcm, BLOCKS * BTAPTX_BLOCK_FRAMES);
	up();
	CHECK(btaptx_start(0) == BTAPTX_OK, "start");
	uint8_t *a = encode_stream(pcm, BLOCKS);
	btaptx_stop();
	/* the library used directly, the whole stream in one pass */
	reference_stream(0, pcm, BLOCKS * BTAPTX_BLOCK_FRAMES, ref);
	CHECK(!memcmp(a, ref, sizeof(ref)), "a stream cut into blocks is the stream encoded whole");
	/* the second session starts from a reset encoder: the same bytes again */
	CHECK(btaptx_start(0) == BTAPTX_OK, "second session");
	uint8_t *b = encode_stream(pcm, BLOCKS);
	btaptx_stop();
	CHECK(!memcmp(a, b, sizeof(ref)), "a new session starts from a reset encoder");
	free(a); free(b);
	printf("  ok   blocks of 512 frames are 512 bytes and make the whole stream's bytes; a session starts afresh\n");
}

static int write_file(const char *path, const void *d, size_t n)
{
	FILE *f = fopen(path, "wb");

	if (!f)
		return 0;
	fwrite(d, 1, n, f);
	fclose(f);
	return 1;
}

/* The self-test's signal through FFmpeg's encoder (the black box the hash constants were made with): the hash of the first `want` bytes of its output, so a change to the
 * signal, the constants or the encoders shows up here, not only in the module's own comparison. `codec` is "aptx" or "aptx_hd". */
static uint64_t ffmpeg_selftest_hash(const char *codec, uint32_t want)
{
	static int16_t sig[BTAPTX_ST_FRAMES * 2u];
	static uint8_t got[2048];
	char dir[] = "/tmp/btaptx_sigXXXXXX", raw[128], ff[128], cmd[512], rm[160];
	FILE *f;

	CHECK(mkdtemp(dir), "a scratch folder");
	for (uint32_t n = 0; n < BTAPTX_ST_FRAMES; n++)
		btaptx_sig(n, &sig[2u * n], &sig[2u * n + 1u]);
	snprintf(raw, sizeof raw, "%s/sig.raw", dir); snprintf(ff, sizeof ff, "%s/sig.out", dir);
	CHECK(write_file(raw, sig, sizeof(sig)), "file");
	snprintf(cmd, sizeof cmd, "ffmpeg -v error -y -f s16le -ar 48000 -ac 2 -i %s -c:a %s -f %s %s", raw, codec, codec, ff);
	CHECK(system(cmd) == 0, "ffmpeg encode of the self-test signal");
	f = fopen(ff, "rb");
	CHECK(f && want <= sizeof(got) && fread(got, 1, want, f) == want, "ffmpeg's output is long enough");
	fclose(f);
	snprintf(rm, sizeof rm, "rm -rf %s", dir);
	CHECK(system(rm) == 0, "cleanup");
	return btaptx_fnv(BTAPTX_FNV_BASIS, got, want);
}

static void t_ffmpeg(void)
{
	enum { BLOCKS = 94 };   /* about one second */
	static int16_t pcm[BLOCKS * BTAPTX_BLOCK_FRAMES * 2u], dec[BLOCKS * BTAPTX_BLOCK_FRAMES * 2u + 8192];
	char cmd[512], dir[] = "/tmp/btaptx_testXXXXXX";
	const char *tag = "ffmpeg";

	if (system("ffmpeg -version >/dev/null 2>&1")) {
		printf("  skip the comparison with ffmpeg: it is not on this machine\n");
		return;
	}
	CHECK(mkdtemp(dir), "a scratch folder");
	CHECK(ffmpeg_selftest_hash("aptx", BTAPTX_ST_BYTES) == BTAPTX_ST_HASH, "the self-test's hash is the one ffmpeg's aptX encoder gives for its signal");
	make_pcm(pcm, BLOCKS * BTAPTX_BLOCK_FRAMES);
	check_headroom(pcm, BLOCKS * BTAPTX_BLOCK_FRAMES);
	up();
	CHECK(btaptx_start(0) == BTAPTX_OK, "start");
	uint8_t *ours = encode_stream(pcm, BLOCKS);
	btaptx_stop();
	char raw[128], ff[128], mine[128], back[128];
	snprintf(raw, sizeof raw, "%s/in.raw", dir); snprintf(ff, sizeof ff, "%s/ff.aptx", dir);
	snprintf(mine, sizeof mine, "%s/ours.aptx", dir); snprintf(back, sizeof back, "%s/back.raw", dir);
	CHECK(write_file(raw, pcm, sizeof(pcm)) && write_file(mine, ours, (size_t)BLOCKS * BTAPTX_BLOCK_BYTES), "files");
	/* the black box's encoder on the same samples: the first BLOCKS * 512 bytes are ours bit for bit */
	snprintf(cmd, sizeof cmd, "%s -v error -y -f s16le -ar 48000 -ac 2 -i %s -c:a aptx -f aptx %s", tag, raw, ff);
	CHECK(system(cmd) == 0, "ffmpeg encode");
	FILE *f = fopen(ff, "rb");
	CHECK(f, "ffmpeg's output");
	uint8_t *theirs = malloc((size_t)BLOCKS * BTAPTX_BLOCK_BYTES);
	CHECK(fread(theirs, 1, (size_t)BLOCKS * BTAPTX_BLOCK_BYTES, f) == (size_t)BLOCKS * BTAPTX_BLOCK_BYTES, "ffmpeg's output is long enough");
	fclose(f);
	CHECK(!memcmp(ours, theirs, (size_t)BLOCKS * BTAPTX_BLOCK_BYTES), "our stream is bit for bit the one ffmpeg's encoder makes (%u bytes)", BLOCKS * BTAPTX_BLOCK_BYTES);
	/* and its decoder takes ours: the signal comes back after the codec's delay with a high signal-to-noise ratio */
	snprintf(cmd, sizeof cmd, "%s -v error -y -f aptx -i %s -f s16le -ar 48000 -ac 2 %s", tag, mine, back);
	CHECK(system(cmd) == 0, "ffmpeg decode");
	f = fopen(back, "rb");
	CHECK(f, "decoded");
	size_t nd = fread(dec, 2, sizeof(dec) / 2, f) / 2;
	fclose(f);
	CHECK(nd >= BLOCKS * BTAPTX_BLOCK_FRAMES - 256, "decoded frames %zu", nd);
	double best = -1e9; int lag_best = -1;
	for (int lag = 0; lag < 200; lag++) {
		double sig = 0, err = 0;
		for (size_t n = 1000; n < nd - 400 && n + 0 < (size_t)BLOCKS * BTAPTX_BLOCK_FRAMES - 400; n++) {
			double x = pcm[2 * (n - (size_t)lag)], y = dec[2 * n];

			sig += x * x; err += (x - y) * (x - y);
		}
		double snr = err > 0 ? 10 * log10(sig / err) : 200;
		if (snr > best) { best = snr; lag_best = lag; }
	}
	CHECK(best > 15.0, "ffmpeg decodes our stream: best signal-to-noise %.1f dB at a delay of %d frames", best, lag_best);
	printf("  ok   bit for bit the stream of ffmpeg's encoder; ffmpeg decodes ours: %.1f dB at a delay of %d frames\n", best, lag_best);
	free(ours); free(theirs);
	char rm[160];
	snprintf(rm, sizeof rm, "rm -rf %s", dir);
	CHECK(system(rm) == 0, "cleanup");
}

/* ---- aptX HD (experimental) ---------------------------------------------------------------------------------- */

static uint8_t *encode_stream_hd(const int16_t *pcm, uint32_t blocks)
{
	uint8_t *out = malloc((size_t)blocks * BTAPTX_HD_BLOCK_BYTES);

	for (uint32_t b = 0; b < blocks; b++)
		CHECK(btaptx_encode_hd(pcm + (size_t)b * BTAPTX_BLOCK_FRAMES * 2u, out + (size_t)b * BTAPTX_HD_BLOCK_BYTES) == BTAPTX_HD_BLOCK_BYTES, "HD block %u", (unsigned)b);
	return out;
}

static void t_hd_selftest(void)
{
	BtAptxInfo i;

	up();
	CHECK(btaptx_ctx && btaptx_ctx_hd && btaptx_ctx != btaptx_ctx_hd, "two states, one for each mode");
	btaptx_get_info(&i);
	CHECK(i.selftest_hd == BTAPTX_ST_PASS && i.hd_ctx_ok && i.hd_bytes == 1536 && ((uint64_t)i.hd_hash_hi << 32 | i.hd_hash_lo) == BTAPTX_ST_HASH_HD, "HD self-test %u, %u bytes, hash %08x%08x",
	      i.selftest_hd, i.hd_bytes, i.hd_hash_hi, i.hd_hash_lo);
	CHECK(i.selftest == BTAPTX_ST_PASS && i.st_bytes == 1024 && ((uint64_t)i.hash_hi << 32 | i.hash_lo) == BTAPTX_ST_HASH, "the classic self-test is as before");
	CHECK(log_has("HD self-test passed") && file_is("BPG1KK0\n") && g_nwrites == 3, "logged; the record is written the same three times for both tests: %.8s", g_file);
	printf("  ok   the HD self-test passes with the hash of the black box's bytes\n");
}

static void t_hd_lifecycle(void)
{
	int16_t pcm[BTAPTX_BLOCK_FRAMES * 2u];
	uint8_t out[BTAPTX_HD_BLOCK_BYTES], cl[BTAPTX_BLOCK_BYTES];
	BtAptxInfo i;

	btaptx_init();
	make_pcm(pcm, BTAPTX_BLOCK_FRAMES);
	CHECK(btaptx_start(1) == BTAPTX_ERR_NOTEST && btaptx_encode_hd(pcm, out) == 0, "no HD session before the self-test passed");
	btaptx_sync(1);
	CHECK(btaptx_start(1) == BTAPTX_OK && file_is("BPG1SK0\n"), "an HD session marks the record: %.8s", g_file);
	CHECK(btaptx_start(0) == BTAPTX_ERR_BUSY, "one session at a time, either mode");
	CHECK(btaptx_encode_hd(pcm, out) == BTAPTX_HD_BLOCK_BYTES && btaptx_encode(pcm, cl) == 0, "an HD session encodes HD blocks (768 bytes) and the classic leaf gives nothing");
	CHECK(btaptx_encode_hd(NULL, out) == 0 && btaptx_encode_hd(pcm, NULL) == 0, "no pointers, no work");
	btaptx_get_info(&i);
	CHECK(i.state == BTAPTX_S_RUNNING && i.hd_session == 1 && i.blocks == 1, "running, HD");
	btaptx_stop();
	CHECK(file_is("BPG1KK0\n") && btaptx_encode_hd(pcm, out) == 0, "a clean end clears the record");
	CHECK(btaptx_start(0) == BTAPTX_OK && btaptx_encode(pcm, cl) == BTAPTX_BLOCK_BYTES && btaptx_encode_hd(pcm, out) == 0, "a classic session after it: the classic leaf only");
	btaptx_get_info(&i);
	CHECK(i.hd_session == 0, "not HD");
	btaptx_stop();
	printf("  ok   the HD session: its own leaf, one mode at a time, the record\n");
}

static void t_hd_blocks_match_whole(void)
{
	enum { BLOCKS = 40 };
	static int16_t pcm[BLOCKS * BTAPTX_BLOCK_FRAMES * 2u];
	static uint8_t ref[BLOCKS * BTAPTX_HD_BLOCK_BYTES];

	make_pcm(pcm, BLOCKS * BTAPTX_BLOCK_FRAMES);
	up();
	CHECK(btaptx_start(1) == BTAPTX_OK, "start");
	uint8_t *a = encode_stream_hd(pcm, BLOCKS);
	btaptx_stop();
	reference_stream(1, pcm, BLOCKS * BTAPTX_BLOCK_FRAMES, ref);
	CHECK(!memcmp(a, ref, sizeof(ref)), "an HD stream cut into blocks is the stream encoded whole");
	CHECK(btaptx_start(1) == BTAPTX_OK, "second session");
	uint8_t *b = encode_stream_hd(pcm, BLOCKS);
	btaptx_stop();
	CHECK(!memcmp(a, b, sizeof(ref)), "a new HD session starts from a reset encoder");
	free(a); free(b);
	printf("  ok   HD blocks of 512 frames are 768 bytes and make the whole stream's bytes; a session starts afresh\n");
}

static void t_ffmpeg_hd(void)
{
	enum { BLOCKS = 94 };
	static int16_t pcm[BLOCKS * BTAPTX_BLOCK_FRAMES * 2u];
	char cmd[512], dir[] = "/tmp/btaptx_hdtestXXXXXX", raw[128], ff[128];
	const char *tag = "ffmpeg";

	if (system("ffmpeg -version >/dev/null 2>&1")) {
		printf("  skip the HD comparison with ffmpeg: it is not on this machine\n");
		return;
	}
	CHECK(mkdtemp(dir), "a scratch folder");
	CHECK(ffmpeg_selftest_hash("aptx_hd", BTAPTX_ST_HD_BYTES) == BTAPTX_ST_HASH_HD, "the HD self-test's hash is the one ffmpeg's aptX HD encoder gives for its signal");
	make_pcm(pcm, BLOCKS * BTAPTX_BLOCK_FRAMES);
	check_headroom(pcm, BLOCKS * BTAPTX_BLOCK_FRAMES);
	up();
	CHECK(btaptx_start(1) == BTAPTX_OK, "start");
	uint8_t *ours = encode_stream_hd(pcm, BLOCKS);
	btaptx_stop();
	snprintf(raw, sizeof raw, "%s/in.raw", dir); snprintf(ff, sizeof ff, "%s/ff.aptxhd", dir);
	CHECK(write_file(raw, pcm, sizeof(pcm)), "file");
	snprintf(cmd, sizeof cmd, "%s -v error -y -f s16le -ar 48000 -ac 2 -i %s -c:a aptx_hd -f aptx_hd %s", tag, raw, ff);
	CHECK(system(cmd) == 0, "ffmpeg encode");
	FILE *f = fopen(ff, "rb");
	CHECK(f, "ffmpeg's output");
	uint8_t *theirs = malloc((size_t)BLOCKS * BTAPTX_HD_BLOCK_BYTES);
	CHECK(fread(theirs, 1, (size_t)BLOCKS * BTAPTX_HD_BLOCK_BYTES, f) == (size_t)BLOCKS * BTAPTX_HD_BLOCK_BYTES, "ffmpeg's output is long enough");
	fclose(f);
	CHECK(!memcmp(ours, theirs, (size_t)BLOCKS * BTAPTX_HD_BLOCK_BYTES), "our HD stream is bit for bit the one ffmpeg's aptX HD encoder makes (%u bytes)", BLOCKS * BTAPTX_HD_BLOCK_BYTES);
	free(ours); free(theirs);
	char rm[160];
	snprintf(rm, sizeof rm, "rm -rf %s", dir);
	CHECK(system(rm) == 0, "cleanup");
	printf("  ok   bit for bit the stream of ffmpeg's aptX HD encoder\n");
}

#ifdef BAD_HD_HASH_BUILD
static void t_failed_hd(void)
{
	BtAptxInfo i;

	up();
	btaptx_get_info(&i);
	CHECK(i.selftest == BTAPTX_ST_PASS && i.selftest_hd == BTAPTX_ST_FAIL && log_has("HD self-test FAILED") && btaptx_start(1) == BTAPTX_ERR_NOTEST && file_is("BPG1KK0\n"),
	      "an HD self-test that fails keeps HD off and leaves classic aptX");
	CHECK(btaptx_start(0) == BTAPTX_OK, "classic aptX still starts");
	btaptx_stop();
	printf("  ok   a failed HD self-test keeps HD off, classic aptX stays\n");
}
#endif

#ifdef BAD_HASH_BUILD
static void t_failed(void)
{
	BtAptxInfo i;

	up();
	btaptx_get_info(&i);
	CHECK(i.selftest == BTAPTX_ST_FAIL && log_has("self-test FAILED") && btaptx_start(0) == BTAPTX_ERR_NOTEST && file_is("BPG1KK0\n"), "a self-test that fails keeps aptX off");
	CHECK(i.selftest_hd == BTAPTX_ST_NORUN && btaptx_start(1) == BTAPTX_ERR_NOTEST, "and HD with it: its test is not run");
	printf("  ok   a failed self-test keeps aptX off\n");
}
#endif

int main(void)
{
	struct { const char *name; void (*fn)(void); } t[] = {
#if defined(BAD_HASH_BUILD)
		{ "failed", t_failed },
#elif defined(BAD_HD_HASH_BUILD)
		{ "failed_hd", t_failed_hd },
#else
		{ "guard_table", t_guard_table }, { "state_size", t_state_size }, { "selftest_signal", t_selftest_signal }, { "lifecycle", t_lifecycle }, { "held", t_held }, { "held_three", t_held_three },
		{ "one_before", t_one_before }, { "banned", t_banned }, { "unwritable", t_unwritable }, { "record_fails_later", t_record_fails_later },
		{ "blocks_match_whole", t_blocks_match_whole }, { "ffmpeg", t_ffmpeg },
		{ "hd_selftest", t_hd_selftest }, { "hd_lifecycle", t_hd_lifecycle }, { "hd_blocks_match_whole", t_hd_blocks_match_whole }, { "ffmpeg_hd", t_ffmpeg_hd },
#endif
	};
	int fails = 0;

	for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		pid_t pid = fork();

		if (pid == 0) {
			t[i].fn();
			exit(0);
		}
		int st = 0;

		waitpid(pid, &st, 0);
		if (!WIFEXITED(st) || WEXITSTATUS(st)) {
			printf("FAIL %s\n", t[i].name);
			fails++;
		}
	}
	printf(fails ? "%d scenario(s) FAILED\n" : "all %u scenarios passed\n", fails ? fails : (unsigned)(sizeof(t) / sizeof(t[0])));
	return fails != 0;
}
