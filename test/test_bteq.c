/*
 * test_bteq.c - host test of the Bluetooth sound filter: the filter code (kernel/bteq_plan.c,
 * bteq_run.c, hpgains.c), the hook body (bteq_hook.c) and the installer (bteq.c) are compiled
 * into this file as they are, with the console's calls replaced by fakes.
 *
 *   make test      (cc -Itest/host test/test_bteq.c -lm -lpthread, then run)
 *
 * What is proved:
 *   - the sound presets as band gains (hpgains.c), Custom, and "off" = flat;
 *   - a flat set is passed through untouched (the original sees the very same pointer);
 *   - the filter, run on real blocks, has the response of an independently computed
 *     double-precision cascade of the same five bands (within +-0.5 dB at the band centres,
 *     for every preset and some extreme Custom sets), and the nominal gain of a lone band;
 *   - full-scale-ish sine sweeps through every preset and the extreme sets never clip, and
 *     the pre-gain keeps the true peak of the response at or below 0 dB;
 *   - only blocks of 0x800 bytes are touched (other lengths, null and unaligned pointers go
 *     to the original as they came);
 *   - the memory is cleared after a pause of more than 50 ms and not before;
 *   - a new set is taken over whole: a reader thread never sees half a set (two sets
 *     alternating at full speed against a reader), and a set is never written while the audio
 *     thread may be reading it;
 *   - the channel stage (mono, swap, balance): Stereo with the balance centred is untouched, down to
 *     the very pointer the original sees; Mono is the two sides averaged (rounded down) on both,
 *     Swap exchanges them exactly, each balance step lowers one side by 0, 3, 6, 12 dB or off, in
 *     Q15, and the combinations and the filter first, on extreme samples, never wrap or clip;
 *   - the installer: right library and function numbers, nothing installed for a flat preset with
 *     Stereo and the centre, installed for a channel or balance change alone, follows setting
 *     changes without reinstalling, leaves the hook in while blocks still go through and keeps it
 *     in until module stop, retry rules after a failure;
 *   - the cost: 25,600 multiply-adds per block with all five bands running.
 */
#define BTT_HOST_TAI 1
#include <btt_host.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#include "../kernel/hpgains.c"
#include "../kernel/bteq_plan.c"
#include "../kernel/bteq_run.c"
#include "../kernel/btaac_neg.c"     /* the AAC stream state the sender's hook reads */
#include "../kernel/bteq_hook.c"

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

#define PI 3.14159265358979323846
#define FS 48000.0

/* ---- fakes for the hook and the installer ------------------------------------------- */

static uint32_t g_clock_us;
uint32_t ksceKernelGetSystemTimeLow(void) { return g_clock_us; }

static const void *g_seen_buf;       /* what the "original routine" was given */
static uint32_t g_seen_len;
static int16_t g_seen_data[BQ_FRAMES * 2];
static int g_orig_calls;
static int g_orig_ret = 0x1234;

static int fake_original(uint32_t lo, uint32_t hi, const void *buf, uint32_t len)
{
	g_orig_calls++;
	g_seen_buf = buf;
	g_seen_len = len;
	if (len == BQ_BLOCK_BYTES && buf)
		memcpy(g_seen_data, buf, sizeof(g_seen_data));
	return g_orig_ret;
}

static struct { int calls, releases; const char *mod; uint32_t lib, nid; const void *hook; int fail; } g_tai;
SceUID taiHookFunctionImportForKernel(SceUID pid, tai_hook_ref_t *ref, const char *module,
                                      uint32_t lib_nid, uint32_t func_nid, const void *hook)
{
	g_tai.calls++;
	g_tai.mod = module; g_tai.lib = lib_nid; g_tai.nid = func_nid; g_tai.hook = hook;
	if (g_tai.fail)
		return -1;
	*ref = (tai_hook_ref_t)fake_original;
	return 0x1000 + g_tai.calls;
}
int taiHookReleaseForKernel(SceUID uid, tai_hook_ref_t ref) { g_tai.releases++; return 0; }
int ksceKernelDelayThread(SceUInt delay) { return 0; }
static int g_log_lines;
static char g_all_log[8192];   /* every line logged, one after the other */
void btt_klog_line(const char *line)
{
	size_t n = strlen(g_all_log);

	g_log_lines++;
	snprintf(g_all_log + n, sizeof(g_all_log) - n, "%s\n", line);
	if (getenv("BTT_TEST_VERBOSE"))
		printf("    %s\n", line);
}

/* The AAC stream, fakes (btaac_stream.c and the engine's leaf, tested in test_btaac_stream.c): the sender's hook hands a block to the engine only while the
 * stream is AAC and running and started; whether the hook must be installed for it is the stream's say. */
static int g_aac_wants;
int btt_aac_stream_wants_pcm_hook(void) { return g_aac_wants; }
static struct { int calls; uint32_t frames; int16_t first[2]; const int16_t *ptr; } g_push;
uint32_t btt_aac_push_pcm(const int16_t *pcm, uint32_t frames)
{
	g_push.calls++;
	g_push.frames = frames;
	g_push.ptr = pcm;
	memcpy(g_push.first, pcm, sizeof(g_push.first));
	return frames;
}

uint32_t btt_ldac_push_pcm(const int16_t *pcm, uint32_t frames) { (void)pcm; return frames; }   /* an LDAC stream's block: tested in test_btaac_stream.c */

#include "../kernel/bteq.c"

/* ---- helpers ------------------------------------------------------------------------ */

static int16_t g_in[BQ_FRAMES * 2];

static void fill_sine(int16_t *b, double f, double amp, long n0, double phase)
{
	for (int n = 0; n < BQ_FRAMES; n++) {
		double v = amp * sin(2 * PI * f * (n0 + n) / FS + phase);
		b[2 * n] = (int16_t)lrint(v);
		b[2 * n + 1] = (int16_t)lrint(-v * 0.5);   /* the other channel differs: nothing may leak */
	}
}

/* Independent reference: the cookbook biquads in double precision (written here again, not read
 * from the generated table), magnitude in dB at f. */
static const double CENTRE[5] = { 100, 300, 875, 2400, 6900 };
static void ref_coef(int band, double gdb, double c[5])
{
	double A = pow(10, gdb / 40), w0 = 2 * PI * CENTRE[band] / FS, cs = cos(w0), sn = sin(w0), b0, b1, b2, a0, a1, a2;
	if (band >= 1 && band <= 3) {
		double al = sn / (2 * 0.9);
		b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A;
	} else {
		double al = sn / 2 * sqrt(2.0), t = 2 * sqrt(A) * al;
		if (band == 0) {
			b0 = A * ((A + 1) - (A - 1) * cs + t); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - t);
			a0 = (A + 1) + (A - 1) * cs + t; a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - t;
		} else {
			b0 = A * ((A + 1) + (A - 1) * cs + t); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - t);
			a0 = (A + 1) - (A - 1) * cs + t; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - t;
		}
	}
	c[0] = b0 / a0; c[1] = b1 / a0; c[2] = b2 / a0; c[3] = a1 / a0; c[4] = a2 / a0;
}

static double ref_db(const int gain[5], double f)
{
	double db = 0;
	for (int b = 0; b < 5; b++) {
		if (!gain[b])
			continue;
		double c[5], w = 2 * PI * f / FS;
		ref_coef(b, gain[b], c);
		double complex_re_n = c[0] + c[1] * cos(w) + c[2] * cos(2 * w), complex_im_n = -c[1] * sin(w) - c[2] * sin(2 * w);
		double re_d = 1 + c[3] * cos(w) + c[4] * cos(2 * w), im_d = -c[3] * sin(w) - c[4] * sin(2 * w);
		db += 10 * log10((complex_re_n * complex_re_n + complex_im_n * complex_im_n) / (re_d * re_d + im_d * im_d));
	}
	return db;
}

/* Run a sine through the filter (left channel measured) and return the gain in dB. Also reports
 * the largest |sample| seen in the settled blocks and whether any sample hit a limit. */
static double measure(BqEq *e, double f, double amp, int *clipped, int *peak)
{
	enum { WARM = 30, KEEP = 16 };
	static double yl[KEEP * BQ_FRAMES];
	long n0 = 0;
	int16_t blk[BQ_FRAMES * 2];
	*clipped = 0; *peak = 0;
	for (int k = 0; k < WARM + KEEP; k++, n0 += BQ_FRAMES) {
		fill_sine(blk, f, amp, n0, 0.3);
		g_clock_us += 10667;
		const int16_t *o = bq_process(e, blk, g_clock_us);
		if (!o)
			o = blk;
		if (k >= WARM)
			for (int n = 0; n < BQ_FRAMES; n++) {
				int v = o[2 * n];
				yl[(k - WARM) * BQ_FRAMES + n] = v;
				if (v == 32767 || v == -32768) *clipped = 1;
				if (abs(v) > *peak) *peak = abs(v);
			}
	}
	/* lock-in with a Hann window over the kept blocks */
	double sr = 0, si = 0, sw = 0;
	int N = KEEP * BQ_FRAMES;
	for (int n = 0; n < N; n++) {
		double w = 0.5 - 0.5 * cos(2 * PI * n / (N - 1)), t = 2 * PI * f * (n0 - N + n) / FS;
		sr += w * yl[n] * cos(t); si += w * yl[n] * sin(t); sw += w;
	}
	double a = 2 * sqrt(sr * sr + si * si) / sw;
	return 20 * log10(a / amp);
}

static void new_eq(BqEq *e, const int gain[5])
{
	BqSet s;
	bq_init(e);
	bq_plan(gain, &s);
	CHECK(bq_publish(e, &s) == 0, "publish");
	g_clock_us = 1000000;
}

/* ---- tests -------------------------------------------------------------------------- */

static const int PRESETS[][5] = {
	{ 3, 1, 0, -1, -1 }, { 0, 0, 0, 2, 3 }, { 6, 3, 0, 0, 0 }, { 4, 1, -2, 1, 4 },       /* the four presets    */
	{ 12, 12, 12, 12, 12 }, { -12, -12, -12, -12, -12 }, { 12, -12, 12, -12, 12 },       /* extreme Custom sets  */
	{ -12, 12, -12, 12, -12 }, { 0, 0, 12, 0, 0 }, { 2, 4, -6, 8, -10 }, { -6, 0, 0, 0, 0 },
};
#define NPRESETS ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

static void test_hpgains(void)
{
	int st[BTT_KEY_COUNT] = { 0 }, g[5], i, p;

	st[BTT_KEY_ENABLE] = 1;
	st[BTT_KEY_SOUND] = BTT_SOUND_ORIGINAL;
	CHECK(!btt_hp_gains(st, g), "Original must be flat");
	for (p = BTT_SOUND_WARM; p <= BTT_SOUND_LIVELY; p++) {
		st[BTT_KEY_SOUND] = p;
		CHECK(btt_hp_gains(st, g) == 1, "preset %d is flat", p);
		for (i = 0; i < 5; i++)
			CHECK(g[i] == PRESETS[p - 1][i], "preset %d band %d: %d", p, i, g[i]);
	}
	st[BTT_KEY_SOUND] = BTT_SOUND_CUSTOM;
	for (i = 0; i < 5; i++)
		st[BTT_KEY_BAND1 + i] = BTT_BAND_FLAT;
	CHECK(!btt_hp_gains(st, g), "Custom with flat bands must be flat");
	for (i = 0; i < 5; i++)
		st[BTT_KEY_BAND1 + i] = i * 3;   /* 0,3,6,9,12 -> -12,-6,0,+6,+12 dB */
	CHECK(btt_hp_gains(st, g) == 1 && g[0] == -12 && g[1] == -6 && g[2] == 0 && g[3] == 6 && g[4] == 12,
	      "Custom: %d %d %d %d %d", g[0], g[1], g[2], g[3], g[4]);
	st[BTT_KEY_ENABLE] = 0;
	CHECK(!btt_hp_gains(st, g) && !g[0] && !g[4], "Bluetooth Turbo off must be flat");
	st[BTT_KEY_ENABLE] = 1;
	st[BTT_KEY_SOUND] = BTT_SOUND_COUNT + 3;
	CHECK(!btt_hp_gains(st, g), "out of range must be flat");
	/* the row function behind the presets */
	CHECK(!btt_hp_preset_row(BTT_SOUND_ORIGINAL) && !btt_hp_preset_row(BTT_SOUND_CUSTOM) && !btt_hp_preset_row(-1) &&
	      btt_hp_preset_row(BTT_SOUND_BASS)[0] == 6, "btt_hp_preset_row");
}

static void test_table(void)
{
	/* The shipped integer coefficients match the cookbook in double precision. */
	double worst = 0;
	for (int b = 0; b < 5; b++)
		for (int g = BQ_GAIN_MIN; g <= BQ_GAIN_MAX; g++) {
			double c[5];
			ref_coef(b, g, c);
			for (int k = 0; k < 5; k++) {
				double d = fabs(BQ_COEF[b][g - BQ_GAIN_MIN][k] / (double)(1 << BQ_QBITS) - c[k]);
				if (d > worst) worst = d;
			}
		}
	CHECK(worst < 1e-8, "table differs from the cookbook by %g", worst);
	printf("  coefficient table vs cookbook: worst difference %.2g\n", worst);
}

static void test_flat_passthrough(void)
{
	BqEq e;
	static const int flat[5] = { 0 };
	new_eq(&e, flat);
	for (int n = 0; n < BQ_FRAMES * 2; n++)
		g_in[n] = (int16_t)(rand() * 7);
	CHECK(bq_process(&e, g_in, g_clock_us) == NULL, "a flat set must pass the block on");
	CHECK(e.n_filtered == 0 && e.n_bypassed == 1, "counters");

	/* through the hook: the original gets the very same pointer and length, and its result is returned */
	bq_init(&bteq_eq);
	{ BqSet s; bq_plan(flat, &s); CHECK(!bq_publish(&bteq_eq, &s), "publish"); }
	bteq_ref = (tai_hook_ref_t)fake_original;
	g_orig_calls = 0;
	CHECK(bteq_hook(1, 2, g_in, BQ_BLOCK_BYTES) == g_orig_ret, "return value");
	CHECK(g_orig_calls == 1 && g_seen_buf == g_in && g_seen_len == BQ_BLOCK_BYTES, "pointer/length changed");
	CHECK(!memcmp(g_seen_data, g_in, sizeof(g_seen_data)), "data changed");
}

static void test_response(void)
{
	static const double extra[] = { 50, 200, 500, 1500, 4000, 12000 };
	double worst = 0;
	for (int p = 0; p < NPRESETS; p++) {
		BqEq e;
		new_eq(&e, PRESETS[p]);
		double pre = e.set[e.idx].pre_half_db / 2.0;
		for (int k = 0; k < 11; k++) {
			double f = k < 5 ? CENTRE[k] : extra[k - 5];
			int clipped, peak;
			bq_init(&e);
			{ BqSet s; bq_plan(PRESETS[p], &s); CHECK(!bq_publish(&e, &s), "publish"); }
			double got = measure(&e, f, 8000, &clipped, &peak);
			double want = ref_db(PRESETS[p], f) - pre;
			double err = fabs(got - want);
			if (err > worst) worst = err;
			CHECK(err <= 0.5, "preset %d at %.0f Hz: %.2f dB, expected %.2f (pre-gain %.1f)", p, f, got, want, pre);
		}
	}
	printf("  response vs the double-precision cascade: worst error %.3f dB at the centres and 6 more points, %d sets\n",
	       worst, NPRESETS);

	/* a lone peak band: the nominal gain at its centre (minus the pre-gain) */
	for (int b = 1; b <= 3; b++)
		for (int g = -12; g <= 12; g += 6) {
			if (!g) continue;
			int gain[5] = { 0 }, clipped, peak;
			gain[b] = g;
			BqEq e;
			new_eq(&e, gain);
			double got = measure(&e, CENTRE[b], 8000, &clipped, &peak);
			double want = g - e.set[e.idx].pre_half_db / 2.0;
			CHECK(fabs(got - want) <= 0.5, "lone band %d %+d dB: %.2f vs %.2f", b, g, got, want);
		}
	/* a lone shelf: its plateau */
	for (int g = -12; g <= 12; g += 12) {
		int gain[5] = { 0 }, clipped, peak;
		BqEq e;
		gain[0] = g;
		new_eq(&e, gain);
		double got = measure(&e, 30, 8000, &clipped, &peak), want = g - e.set[e.idx].pre_half_db / 2.0;
		CHECK(fabs(got - want) <= 0.5, "low shelf %+d dB plateau: %.2f vs %.2f", g, got, want);
		gain[0] = 0; gain[4] = g;
		new_eq(&e, gain);
		got = measure(&e, 16000, 8000, &clipped, &peak); want = g - e.set[e.idx].pre_half_db / 2.0;
		CHECK(fabs(got - want) <= 0.5, "high shelf %+d dB plateau: %.2f vs %.2f", g, got, want);
	}
}

static void test_noclip(void)
{
	double worst_peak = -99;
	for (int p = 0; p < NPRESETS; p++) {
		BqEq e;
		new_eq(&e, PRESETS[p]);
		double pre = e.set[e.idx].pre_half_db / 2.0;
		/* the true peak of the response, fine scan: at or below 0 dB with the pre-gain */
		double peak = -99;
		for (double f = 15; f < 21000; f *= pow(2, 1 / 48.0)) {
			double d = ref_db(PRESETS[p], f) - pre;
			if (d > peak) peak = d;
		}
		if (peak > worst_peak) worst_peak = peak;
		CHECK(peak <= 0.02, "preset %d: peak %.3f dB above full scale", p, peak);
		/* and on real blocks: a sine at -0.2 dBFS at 1/6-octave steps never touches a limit */
		for (double f = 20; f < 20000; f *= pow(2, 1 / 6.0)) {
			int clipped, pk;
			bq_init(&e);
			{ BqSet s; bq_plan(PRESETS[p], &s); CHECK(!bq_publish(&e, &s), "publish"); }
			measure(&e, f, 32000, &clipped, &pk);
			CHECK(!clipped, "preset %d: clipped at %.0f Hz (peak %d)", p, f, pk);
		}
	}
	printf("  no clipping: %d sets, sines at -0.2 dBFS from 20 Hz to 20 kHz; worst true response peak %+.3f dB\n",
	       NPRESETS, worst_peak);
}

static void test_other_lengths(void)
{
	static int16_t blk[BQ_FRAMES * 2 + 4] __attribute__((aligned(8)));
	static const int bass[5] = { 6, 3, 0, 0, 0 };
	static const uint32_t lens[] = { 0, 0xF0, 0x400, 0x7FF, 0x801, 0x1000 };

	bq_init(&bteq_eq);
	{ BqSet s; bq_plan(bass, &s); CHECK(!bq_publish(&bteq_eq, &s), "publish"); }
	bteq_ref = (tai_hook_ref_t)fake_original;
	fill_sine(blk, 1000, 20000, 0, 0);
	for (unsigned i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
		uint32_t before = bteq_eq.n_filtered + bteq_eq.n_bypassed;
		g_seen_buf = 0; g_seen_len = 99;
		CHECK(bteq_hook(1, 2, blk, lens[i]) == g_orig_ret, "return");
		CHECK(g_seen_buf == blk && g_seen_len == lens[i], "length 0x%x: pointer or length changed", lens[i]);
		CHECK(before == bteq_eq.n_filtered + bteq_eq.n_bypassed, "length 0x%x reached the filter", lens[i]);
	}
	CHECK(bteq_hook(1, 2, NULL, BQ_BLOCK_BYTES) == g_orig_ret && g_seen_buf == NULL, "null buffer");
	CHECK(bteq_hook(1, 2, (char *)blk + 2, BQ_BLOCK_BYTES) == g_orig_ret && g_seen_buf == (char *)blk + 2,
	      "unaligned buffer");
	CHECK(bteq_eq.n_filtered == 0, "something was filtered");

	/* the right length: filtered into the module's own buffer, the sender's buffer untouched */
	int16_t copy[BQ_FRAMES * 2];
	memcpy(copy, blk, sizeof(copy));
	CHECK(bteq_hook(1, 2, blk, BQ_BLOCK_BYTES) == g_orig_ret, "return");
	CHECK(g_seen_buf == bteq_eq.out && g_seen_buf != blk && bteq_eq.n_filtered == 1, "not filtered");
	CHECK(!memcmp(blk, copy, sizeof(copy)), "the sender's buffer was written");
	CHECK(memcmp(g_seen_data, copy, sizeof(copy)) != 0, "the filtered block equals the input");

	/* no reference yet (the hook is live but taiHEN has not stored it): no call, an error */
	bteq_ref = 0;
	g_orig_calls = 0;
	CHECK(bteq_hook(1, 2, blk, BQ_BLOCK_BYTES) == (int)0x802F0D07 && g_orig_calls == 0, "unset reference");
	bteq_ref = (tai_hook_ref_t)fake_original;
}

static void test_gap_reset(void)
{
	static const int bass[5] = { 6, 3, 0, 0, 0 };
	int16_t loud[BQ_FRAMES * 2], quiet[BQ_FRAMES * 2] = { 0 };
	fill_sine(loud, 150, 30000, 0, 0);
	int ringing[2] = { 0, 0 };
	static const uint32_t gaps[2] = { 10667, 60000 };
	for (int k = 0; k < 2; k++) {
		BqEq e;
		new_eq(&e, bass);
		bq_process(&e, loud, g_clock_us);
		g_clock_us += gaps[k];
		uint32_t resets = e.n_resets;
		const int16_t *o = bq_process(&e, quiet, g_clock_us);
		CHECK(o, "filter on");
		for (int n = 0; n < BQ_FRAMES * 2; n++)
			ringing[k] |= o[n] != 0;
		CHECK(e.n_resets == resets + (k ? 1 : 0), "reset count with a gap of %u us", gaps[k]);
	}
	CHECK(ringing[0], "the memory must carry on over a normal gap");
	CHECK(!ringing[1], "the memory must be cleared after a pause of 60 ms");

	/* the timer wrapping round must not look like a pause */
	BqEq e;
	new_eq(&e, bass);
	bq_process(&e, loud, 0xFFFFF000u);
	uint32_t r = e.n_resets;
	bq_process(&e, loud, 0xFFFFF000u + 10667u);
	CHECK(e.n_resets == r, "timer wrap taken for a pause");
}

static void test_swap_logic(void)
{
	BqEq e;
	BqSet a, b;
	static const int ga[5] = { 6, 3, 0, 0, 0 }, gb[5] = { 0, 0, 0, 2, 3 };
	bq_init(&e);
	bq_plan(ga, &a);
	bq_plan(gb, &b);
	CHECK(bq_publish(&e, &a) == 0 && e.idx == 1, "first publish goes to set 1");
	uint32_t gen1 = e.set[1].gen;

	/* the audio thread is (apparently) reading the set that would be written: refuse, change nothing */
	BqSet before = e.set[0];
	e.in_use = 0 + 1;
	CHECK(bq_publish(&e, &b) == -1, "published over a set that is being read");
	CHECK(e.idx == 1 && !memcmp(&before, &e.set[0], sizeof(before)) && e.set[1].gen == gen1, "refused publish changed something");
	e.in_use = 1 + 1;   /* reading the current set: the other one is free */
	CHECK(bq_publish(&e, &b) == 0 && e.idx == 0 && e.set[0].gen != gen1, "publish while the current set is read");
	e.in_use = 0;
	CHECK(bq_publish(&e, &a) == 0 && e.idx == 1, "publish when idle");

	/* a gain change keeps the memory of bands that go on running; a band that stops loses it */
	new_eq(&e, ga);
	int16_t blk[BQ_FRAMES * 2];
	fill_sine(blk, 120, 20000, 0, 0);
	g_clock_us += 10667; bq_process(&e, blk, g_clock_us);
	BqState keep = e.st[0][1];
	CHECK(keep.z1 || keep.z2, "no filter memory");
	static const int gc[5] = { 0, 4, 0, 0, 0 };
	BqSet c; bq_plan(gc, &c);
	CHECK(bq_publish(&e, &c) == 0, "publish");
	uint32_t r = e.n_resets;
	g_clock_us += 10667; bq_process(&e, blk, g_clock_us);
	CHECK(e.n_resets == r, "a new set must not reset the memory");
	CHECK(!e.st[0][0].z1 && !e.st[0][0].z2 && !e.st[1][0].z1, "a band that stopped kept its memory");
}

typedef struct { BqEq *e; BqSet *sets; volatile int stop; } Race;

static void *race_writer(void *arg)
{
	Race *r = arg;
	unsigned k = 0, busy = 0;
	while (!r->stop) {
		if (bq_publish(r->e, &r->sets[k & 1]) == 0)
			k++;
		else
			busy++;
	}
	return (void *)(uintptr_t)(k * 0 + busy * 0 + 1);
}

static void test_swap_threads(void)
{
	static const int ga[5] = { 12, -6, 0, 0, 0 }, gb[5] = { 0, 0, 3, 6, -9 };
	BqSet sets[2];
	static BqEq e, ref;
	int16_t blk[BQ_FRAMES * 2], want[2][BQ_FRAMES * 2];
	enum { BLOCKS = 40000 };

	bq_plan(ga, &sets[0]);
	bq_plan(gb, &sets[1]);
	fill_sine(blk, 440, 15000, 0, 0);
	for (int k = 0; k < 2; k++) {
		bq_init(&ref);
		CHECK(bq_publish(&ref, &sets[k]) == 0, "publish");
		const int16_t *o = bq_process(&ref, blk, 1000000);
		memcpy(want[k], o, sizeof(want[k]));
	}
	CHECK(memcmp(want[0], want[1], sizeof(want[0])) != 0, "the two test sets sound the same");

	bq_init(&e);
	Race r = { &e, sets, 0 };
	CHECK(bq_publish(&e, &sets[0]) == 0, "publish");
	pthread_t th;
	pthread_create(&th, NULL, race_writer, &r);
	int seen[2] = { 0, 0 };
	for (int k = 0; k < BLOCKS; k++) {
		/* every block starts from silence (a 100 ms gap), so it depends on the set alone */
		const int16_t *o = bq_process(&e, blk, (uint32_t)k * 100000u);
		CHECK(o, "filter off");
		if (!memcmp(o, want[0], sizeof(want[0]))) seen[0]++;
		else if (!memcmp(o, want[1], sizeof(want[1]))) seen[1]++;
		else CHECK(0, "block %d is neither set's output (half a set was read)", k);
	}
	r.stop = 1;
	pthread_join(th, NULL);
	CHECK(seen[0] > 100 && seen[1] > 100, "the sets did not alternate (%d, %d)", seen[0], seen[1]);
	printf("  swap race: %d blocks against a writer swapping two sets, all whole (%d / %d)\n", BLOCKS, seen[0], seen[1]);
}

static void settings(int *st, int enable, int preset)
{
	memset(st, 0, sizeof(int) * BTT_KEY_COUNT);
	st[BTT_KEY_ENABLE] = enable;
	st[BTT_KEY_SOUND] = preset;
	st[BTT_KEY_CHANNELS] = BTT_CH_STEREO;
	st[BTT_KEY_BALANCE] = BTT_BAL_CENTRE;
	for (int i = 0; i < 5; i++)
		st[BTT_KEY_BAND1 + i] = BTT_BAND_FLAT;
}

static void test_installer(void)
{
	int st[BTT_KEY_COUNT];
	int16_t blk[BQ_FRAMES * 2];
	fill_sine(blk, 500, 20000, 0, 0);

	memset(&g_tai, 0, sizeof(g_tai));
	bteq_ref = 0;
	settings(st, 1, BTT_SOUND_ORIGINAL);
	btt_bteq_sync(st);
	CHECK(g_tai.calls == 0, "installed for a flat preset");
	settings(st, 0, BTT_SOUND_BASS);
	btt_bteq_sync(st);
	CHECK(g_tai.calls == 0, "installed with Bluetooth Turbo off");

	settings(st, 1, BTT_SOUND_BASS);
	btt_bteq_sync(st);
	CHECK(g_tai.calls == 1, "not installed");
	CHECK(!strcmp(g_tai.mod, "SceAVConfig") && g_tai.lib == 0xD48CA62D && g_tai.nid == 0x47F19727 &&
	      g_tai.hook == (const void *)bteq_hook, "wrong import: %s %08x %08x", g_tai.mod, g_tai.lib, g_tai.nid);
	CHECK(bteq_ref == (tai_hook_ref_t)fake_original, "reference");
	g_clock_us += 10667;
	CHECK(bteq_hook(0, 0, blk, BQ_BLOCK_BYTES) == g_orig_ret && g_seen_buf == bteq_eq.out, "not filtering");
	CHECK(bteq_eq.set[bteq_eq.idx].active == 3, "Bass runs bands 0 and 1 only (mask %x)", bteq_eq.set[bteq_eq.idx].active);

	settings(st, 1, BTT_SOUND_LIVELY);   /* a change of preset: no second install */
	btt_bteq_sync(st);
	CHECK(g_tai.calls == 1 && bteq_eq.set[bteq_eq.idx].active == 31, "preset change (calls %d)", g_tai.calls);
	g_clock_us += 10667;
	bteq_hook(0, 0, blk, BQ_BLOCK_BYTES);
	CHECK(g_seen_buf == bteq_eq.out, "not filtering after the change");

	/* switched off: flat at once, and the hook is never released while the module runs */
	settings(st, 0, BTT_SOUND_LIVELY);
	btt_bteq_sync(st);
	CHECK(g_tai.releases == 0 && bteq_eq.set[bteq_eq.idx].active == 0, "released (or not made flat)");
	g_clock_us += 10667;
	bteq_hook(0, 0, blk, BQ_BLOCK_BYTES);
	CHECK(g_seen_buf == blk, "off, but the block was changed");
	btt_bteq_sync(st);
	btt_bteq_sync(st);   /* quiet passes: still in */
	CHECK(g_tai.releases == 0 && bteq_ref == (tai_hook_ref_t)fake_original, "released while the module runs");

	/* back on: the same hook, a new set */
	settings(st, 1, BTT_SOUND_WARM);
	btt_bteq_sync(st);
	CHECK(g_tai.calls == 1 && bteq_eq.set[bteq_eq.idx].active != 0, "back on (calls %d)", g_tai.calls);

	/* the sender's thread still on the set that would be overwritten: wait for the next pass */
	uint32_t w = bteq_eq.idx ^ 1, gen = bteq_eq.set[bteq_eq.idx].gen;
	bteq_eq.in_use = w + 1;
	settings(st, 1, BTT_SOUND_BRIGHT);
	btt_bteq_sync(st);
	CHECK(bteq_eq.set[bteq_eq.idx].gen == gen, "published over a set being read");
	bteq_eq.in_use = 0;
	btt_bteq_sync(st);
	CHECK(bteq_eq.set[bteq_eq.idx].gen != gen && bteq_eq.set[bteq_eq.idx].active == 24, "not applied on the next pass");

	/* module stop: out, once */
	btt_bteq_restore();
	CHECK(g_tai.releases == 1 && bteq_ref == 0, "restore");
	btt_bteq_restore();
	CHECK(g_tai.releases == 1, "restore twice");

	/* a failed install is not retried every pass, but is after the feature is switched off and on */
	g_tai.fail = 1;
	int calls = g_tai.calls;
	settings(st, 1, BTT_SOUND_BASS);
	btt_bteq_sync(st);
	btt_bteq_sync(st);
	btt_bteq_sync(st);
	CHECK(g_tai.calls == calls + 1, "retried a failed install (%d)", g_tai.calls - calls);
	g_tai.fail = 0;
	btt_bteq_sync(st);
	CHECK(g_tai.calls == calls + 1, "retried without being switched off and on");
	settings(st, 0, BTT_SOUND_BASS);
	btt_bteq_sync(st);
	settings(st, 1, BTT_SOUND_BASS);
	btt_bteq_sync(st);
	CHECK(g_tai.calls == calls + 2 && bteq_ref != 0, "not installed after off and on");
	btt_bteq_restore();
}


/* ---- channels and balance ------------------------------------------------------------ */

/* The expected Q15 gain of a balance step: 32768 * 10^(-dB / 20) for 0, 3, 6, 12 dB; off at the end. */
static const int BAL_DB[5] = { 0, 3, 6, 12, -1 };

static void test_mix_plan(void)
{
	BqSet o;
	for (int step = 0; step <= 4; step++) {
		int want = BAL_DB[step] < 0 ? 0 : (int)lrint(32768 * pow(10, -BAL_DB[step] / 20.0));
		/* right side lowered (the sound moves left): balance 4 - step */
		bq_plan_mix(&o, BQ_MIX_STEREO, 4 - step);
		CHECK(o.gain_l == 32768 && o.gain_r == want, "L%d: gains %d %d, expected 32768 %d", step, o.gain_l, o.gain_r, want);
		CHECK(o.mix_on == (step != 0) && o.mix_mode == BQ_MIX_STEREO, "L%d: on %u mode %u", step, o.mix_on, o.mix_mode);
		bq_plan_mix(&o, BQ_MIX_STEREO, 4 + step);
		CHECK(o.gain_r == 32768 && o.gain_l == want, "R%d: gains %d %d, expected %d 32768", step, o.gain_l, o.gain_r, want);
	}
	bq_plan_mix(&o, BQ_MIX_MONO, 4);
	CHECK(o.mix_on == 1 && o.mix_mode == BQ_MIX_MONO && o.gain_l == 32768 && o.gain_r == 32768, "mono at the centre");
	bq_plan_mix(&o, BQ_MIX_SWAP, 4);
	CHECK(o.mix_on == 1 && o.mix_mode == BQ_MIX_SWAP, "swap at the centre");
	bq_plan_mix(&o, 9, -3);   /* out of range: Stereo, and the balance clamped to the ends */
	CHECK(o.mix_mode == BQ_MIX_STEREO && o.gain_r == 0 && o.gain_l == 32768, "out of range: mode %u gains %d %d", o.mix_mode, o.gain_l, o.gain_r);
	bq_plan_mix(&o, BQ_MIX_STEREO, 99);
	CHECK(o.gain_l == 0 && o.gain_r == 32768, "balance above the range");
	/* a plan made after bq_plan keeps the filter and sets the stage */
	static const int bass[5] = { 6, 3, 0, 0, 0 };
	bq_plan(bass, &o);
	bq_plan_mix(&o, BQ_MIX_MONO, 2);
	CHECK(o.active == 3 && o.mix_on == 1 && o.gain_r < 32768, "plan then mix");
	/* and a plain bq_plan leaves the stage off */
	bq_plan(bass, &o);
	CHECK(o.mix_on == 0, "bq_plan alone turned the channel stage on");
}

static int ref_gain(int16_t x, int32_t g) { return (int)(((int32_t)x * g + 16384) >> 15); }

/* One frame through the channel stage, in 64-bit arithmetic so a wrap in the module would show. */
static void ref_frame(int mode, const BqSet *s, int l, int r, int *ol, int *or_)
{
	if (mode == BQ_MIX_MONO) l = r = (int)(((long long)l + r) >> 1);
	else if (mode == BQ_MIX_SWAP) { int t = l; l = r; r = t; }
	*ol = (int)(((long long)l * s->gain_l + 16384) >> 15);
	*or_ = (int)(((long long)r * s->gain_r + 16384) >> 15);
}

/* A block with extreme and ordinary samples, the two sides different. */
static void fill_mixed(int16_t *b)
{
	static const int16_t special[] = { -32768, 32767, 0, 1, -1, 12345, -12345, 32766, -32767 };
	for (int n = 0; n < BQ_FRAMES; n++) {
		b[2 * n] = (int16_t)(rand() * 3 - 16384);
		b[2 * n + 1] = (int16_t)(rand() * 5 - 8192);
	}
	for (int k = 0; k < 9; k++)
		for (int j = 0; j < 9; j++) {
			b[2 * (k * 9 + j)] = special[k];
			b[2 * (k * 9 + j) + 1] = special[j];
		}
}

static const int16_t *mix_with(BqEq *e, int mode, int balance, const int gain[5], const int16_t *in)
{
	BqSet s;
	bq_init(e);
	bq_plan(gain, &s);
	bq_plan_mix(&s, mode, balance);
	CHECK(bq_publish(e, &s) == 0, "publish");
	g_clock_us += 10667;
	return bq_process(e, in, g_clock_us);
}

static void test_mix_run(void)
{
	static const int flat[5] = { 0 };
	static int16_t in[BQ_FRAMES * 2], copy[BQ_FRAMES * 2];
	static BqEq e;
	const int16_t *o;

	fill_mixed(in);
	memcpy(copy, in, sizeof(in));
	g_clock_us = 1000000;

	/* Stereo with the balance centred: nothing to do, the block passes on as it is */
	o = mix_with(&e, BQ_MIX_STEREO, 4, flat, in);
	CHECK(o == NULL && e.n_bypassed == 1 && e.n_filtered == 0, "Stereo and centre must pass the block on");

	/* Mono: the two sides averaged, rounded down, on both; the sender's block untouched */
	o = mix_with(&e, BQ_MIX_MONO, 4, flat, in);
	CHECK(o == e.out && e.n_filtered == 1, "mono: no block");
	for (int n = 0; n < BQ_FRAMES; n++) {
		int m = ((int)in[2 * n] + (int)in[2 * n + 1]) >> 1;
		CHECK(o[2 * n] == m && o[2 * n + 1] == m, "mono frame %d: %d %d, expected %d (%d %d)", n, o[2 * n], o[2 * n + 1], m, in[2 * n], in[2 * n + 1]);
	}
	CHECK(!memcmp(in, copy, sizeof(in)), "the sender's block was written");

	/* Swap: exact exchange */
	o = mix_with(&e, BQ_MIX_SWAP, 4, flat, in);
	for (int n = 0; n < BQ_FRAMES; n++)
		CHECK(o[2 * n] == in[2 * n + 1] && o[2 * n + 1] == in[2 * n], "swap frame %d", n);

	/* every balance step, both directions: one side as the formula says, the other exact */
	for (int bal = 0; bal <= 8; bal++) {
		BqSet s;
		bq_plan_mix(&s, BQ_MIX_STEREO, bal);
		if (bal == 4)
			continue;
		o = mix_with(&e, BQ_MIX_STEREO, bal, flat, in);
		CHECK(o == e.out, "balance %d: no block", bal);
		for (int n = 0; n < BQ_FRAMES; n++)
			CHECK(o[2 * n] == ref_gain(in[2 * n], s.gain_l) && o[2 * n + 1] == ref_gain(in[2 * n + 1], s.gain_r),
			      "balance %d frame %d: %d %d", bal, n, o[2 * n], o[2 * n + 1]);
		if (bal < 4)
			for (int n = 0; n < BQ_FRAMES; n++)
				CHECK(o[2 * n] == in[2 * n], "balance %d: the unlowered left side changed", bal);
		else
			for (int n = 0; n < BQ_FRAMES; n++)
				CHECK(o[2 * n + 1] == in[2 * n + 1], "balance %d: the unlowered right side changed", bal);
	}
	/* the ends: the lowered side is silent */
	o = mix_with(&e, BQ_MIX_STEREO, 0, flat, in);
	for (int n = 0; n < BQ_FRAMES; n++)
		CHECK(o[2 * n + 1] == 0 && o[2 * n] == in[2 * n], "L4 is not left only");
	o = mix_with(&e, BQ_MIX_STEREO, 8, flat, in);
	for (int n = 0; n < BQ_FRAMES; n++)
		CHECK(o[2 * n] == 0 && o[2 * n + 1] == in[2 * n + 1], "R4 is not right only");

	/* the combinations: mono then balance, swap then balance */
	for (int mode = BQ_MIX_MONO; mode <= BQ_MIX_SWAP; mode++)
		for (int bal = 0; bal <= 8; bal++) {
			BqSet s;
			bq_plan_mix(&s, mode, bal);
			o = mix_with(&e, mode, bal, flat, in);
			for (int n = 0; n < BQ_FRAMES; n++) {
				int l = in[2 * n], r = in[2 * n + 1];
				if (mode == BQ_MIX_MONO) l = r = (l + r) >> 1; else { int t = l; l = r; r = t; }
				CHECK(o[2 * n] == ref_gain((int16_t)l, s.gain_l) && o[2 * n + 1] == ref_gain((int16_t)r, s.gain_r),
				      "mode %d balance %d frame %d", mode, bal, n);
			}
		}

	/* the filter first, then the stage: the same as the filter alone followed by the stage */
	static const int bass[5] = { 6, 3, 0, 0, 0 };
	static BqEq e2;
	static int16_t filtered[BQ_FRAMES * 2];
	for (int mode = BQ_MIX_STEREO; mode <= BQ_MIX_SWAP; mode++)
		for (int bal = 0; bal <= 8; bal += 2) {
			if (mode == BQ_MIX_STEREO && bal == 4)
				continue;
			const int16_t *f = mix_with(&e2, BQ_MIX_STEREO, 4, bass, in);   /* the filter alone */
			CHECK(f == e2.out, "filter alone");
			memcpy(filtered, f, sizeof(filtered));
			BqSet s;
			bq_plan_mix(&s, mode, bal);
			o = mix_with(&e, mode, bal, bass, in);
			CHECK(e.set[e.idx].active == 3 && e.set[e.idx].mix_on == 1, "filter and stage both on");
			for (int n = 0; n < BQ_FRAMES; n++) {
				int l = filtered[2 * n], r = filtered[2 * n + 1];
				if (mode == BQ_MIX_MONO) l = r = (l + r) >> 1; else if (mode == BQ_MIX_SWAP) { int t = l; l = r; r = t; }
				CHECK(o[2 * n] == ref_gain((int16_t)l, s.gain_l) && o[2 * n + 1] == ref_gain((int16_t)r, s.gain_r),
				      "filter then mode %d balance %d: frame %d", mode, bal, n);
			}
		}

	/* extreme samples never wrap: full-scale on one side, opposite on the other, every mode and balance */
	for (int k = 0; k < BQ_FRAMES * 2; k++)
		in[k] = (k & 2) ? 32767 : -32768;
	for (int mode = 0; mode <= 2; mode++)
		for (int bal = 0; bal <= 8; bal++) {
			if (mode == 0 && bal == 4)
				continue;
			BqSet s;
			bq_plan_mix(&s, mode, bal);
			o = mix_with(&e, mode, bal, flat, in);
			for (int n = 0; n < BQ_FRAMES; n++) {
				int l, r;
				ref_frame(mode, &s, in[2 * n], in[2 * n + 1], &l, &r);
				CHECK(l >= -32768 && l <= 32767 && r >= -32768 && r <= 32767, "the reference itself is out of range");
				CHECK(o[2 * n] == l && o[2 * n + 1] == r, "extreme samples mode %d balance %d frame %d: %d %d, expected %d %d",
				      mode, bal, n, o[2 * n], o[2 * n + 1], l, r);
			}
		}
}

static void test_mix_hook(void)
{
	static int16_t blk[BQ_FRAMES * 2];
	static const int flat[5] = { 0 };

	fill_mixed(blk);
	bteq_ref = (tai_hook_ref_t)fake_original;

	/* default: the original gets the very same pointer */
	bq_init(&bteq_eq);
	{ BqSet s; bq_plan(flat, &s); bq_plan_mix(&s, BQ_MIX_STEREO, BQ_BAL_CENTRE); CHECK(!bq_publish(&bteq_eq, &s), "publish"); }
	g_clock_us += 10667;
	CHECK(bteq_hook(0, 0, blk, BQ_BLOCK_BYTES) == g_orig_ret && g_seen_buf == blk, "Stereo and centre: the block was replaced");

	/* Mono: the module's own buffer, and the sender's block is untouched */
	int16_t copy[BQ_FRAMES * 2];
	memcpy(copy, blk, sizeof(copy));
	{ BqSet s; bq_plan(flat, &s); bq_plan_mix(&s, BQ_MIX_MONO, BQ_BAL_CENTRE); CHECK(!bq_publish(&bteq_eq, &s), "publish"); }
	g_clock_us += 10667;
	CHECK(bteq_hook(0, 0, blk, BQ_BLOCK_BYTES) == g_orig_ret && g_seen_buf == bteq_eq.out && !memcmp(blk, copy, sizeof(copy)), "mono through the hook");
	CHECK(g_seen_data[0] == g_seen_data[1], "the sides differ under Mono");
}

/* AAC streaming: the block as it goes on to the original (after the filter) is handed to the engine, once, only when every condition holds. */
static void test_aac_feed(void)
{
	static int16_t blk[BQ_FRAMES * 2];
	static const int flat[5] = { 0 };
	static uint8_t conn[0x4000];
	int16_t copy[BQ_FRAMES * 2];

	fill_mixed(blk);
	memcpy(copy, blk, sizeof(copy));
	bteq_ref = (tai_hook_ref_t)fake_original;
	bq_init(&bteq_eq);
	{ BqSet s; bq_plan(flat, &s); bq_plan_mix(&s, BQ_MIX_STEREO, BQ_BAL_CENTRE); CHECK(!bq_publish(&bteq_eq, &s), "publish"); }
	btaac_neg.conn = (uintptr_t)conn;
	*(uint32_t *)(conn + BTAAC_C_PROFILES) = BTAAC_PROF_STARTED;

	/* nothing set: the block goes on alone */
	g_clock_us += 10667;
	CHECK(bteq_hook(0, 0, blk, BQ_BLOCK_BYTES) == g_orig_ret && g_push.calls == 0, "no AAC stream: the engine was fed");
	/* each condition missing in turn */
	btaac_neg.pcm_gate = 1;
	CHECK(bteq_hook(0, 0, blk, BQ_BLOCK_BYTES) == g_orig_ret && g_push.calls == 0, "the stream is not AAC (state NONE): the engine was fed");
	btaac_neg.state = BTAAC_N_CONFIGURED;
	*(uint32_t *)(conn + BTAAC_C_PROFILES) = 0;
	CHECK(bteq_hook(0, 0, blk, BQ_BLOCK_BYTES) == g_orig_ret && g_push.calls == 0, "the stream is not started: the engine was fed");
	*(uint32_t *)(conn + BTAAC_C_PROFILES) = BTAAC_PROF_STARTED;
	btaac_neg.pcm_gate = 0;
	CHECK(bteq_hook(0, 0, blk, BQ_BLOCK_BYTES) == g_orig_ret && g_push.calls == 0, "the session is not running: the engine was fed");
	btaac_neg.pcm_gate = 1;
	btaac_neg.conn = 0;
	CHECK(bteq_hook(0, 0, blk, BQ_BLOCK_BYTES) == g_orig_ret && g_push.calls == 0, "no connection known: the engine was fed");
	btaac_neg.conn = (uintptr_t)conn;
	/* a block of another length or an unaligned one is not PCM we know: never given to the engine */
	CHECK(bteq_hook(0, 0, blk, 0xF0) == g_orig_ret && bteq_hook(0, 0, (char *)blk + 1, BQ_BLOCK_BYTES) == g_orig_ret && g_push.calls == 0, "another length or alignment was fed");
	/* all conditions: the block, 512 frames, exactly what the original was given */
	CHECK(bteq_hook(0, 0, blk, BQ_BLOCK_BYTES) == g_orig_ret && g_push.calls == 1 && g_push.frames == 512 && g_push.ptr == g_seen_buf,
	      "the engine got %d blocks of %u frames, the original's own buffer: %d", g_push.calls, (unsigned)g_push.frames, g_push.ptr == g_seen_buf);
	/* through a filter that changes the block (Mono): the filtered block is what the engine gets, the sender's is untouched */
	{ BqSet s; bq_plan(flat, &s); bq_plan_mix(&s, BQ_MIX_MONO, BQ_BAL_CENTRE); CHECK(!bq_publish(&bteq_eq, &s), "publish"); }
	g_clock_us += 10667;
	CHECK(bteq_hook(0, 0, blk, BQ_BLOCK_BYTES) == g_orig_ret && g_push.calls == 2 && g_push.ptr == bteq_eq.out && g_push.first[0] == g_push.first[1] &&
	      !memcmp(blk, copy, sizeof(copy)), "the engine did not get the filtered block");
	/* the installer: the hook goes in because AAC wants it, with a flat filter */
	{
		int st[BTT_KEY_COUNT];

		memset(&g_tai, 0, sizeof(g_tai));
		btt_bteq_restore();
		memset(&g_tai, 0, sizeof(g_tai));
		settings(st, 1, BTT_SOUND_ORIGINAL);
		btt_bteq_sync(st);
		CHECK(g_tai.calls == 0, "the sender's hook went in with nothing wanted");
		g_aac_wants = 1;
		btt_bteq_sync(st);
		CHECK(g_tai.calls == 1, "AAC streaming wants the sender's hook: %d installs", g_tai.calls);
		g_aac_wants = 0;
		btt_bteq_sync(st);
		CHECK(g_tai.calls == 1 && g_tai.releases == 0, "a hook in the sender's path stays in (passing through)");
	}
}

static void test_hp_mix(void)
{
	int st[BTT_KEY_COUNT], mode, bal;

	settings(st, 1, BTT_SOUND_ORIGINAL);
	CHECK(!btt_hp_mix(st, &mode, &bal) && mode == BTT_CH_STEREO && bal == BTT_BAL_CENTRE, "default is not stereo and centre");
	st[BTT_KEY_CHANNELS] = BTT_CH_MONO;
	CHECK(btt_hp_mix(st, &mode, &bal) == 1 && mode == BTT_CH_MONO && bal == 4, "mono");
	st[BTT_KEY_CHANNELS] = BTT_CH_SWAP;
	CHECK(btt_hp_mix(st, &mode, &bal) == 1 && mode == BTT_CH_SWAP, "swap");
	st[BTT_KEY_CHANNELS] = BTT_CH_STEREO;
	st[BTT_KEY_BALANCE] = 0;
	CHECK(btt_hp_mix(st, &mode, &bal) == 1 && bal == 0, "L4");
	st[BTT_KEY_BALANCE] = 8;
	CHECK(btt_hp_mix(st, &mode, &bal) == 1 && bal == 8, "R4");
	st[BTT_KEY_BALANCE] = 3;
	st[BTT_KEY_ENABLE] = 0;
	CHECK(!btt_hp_mix(st, &mode, &bal) && mode == BTT_CH_STEREO && bal == BTT_BAL_CENTRE, "Bluetooth Turbo off must be stereo and centre");
	st[BTT_KEY_ENABLE] = 1;
	st[BTT_KEY_CHANNELS] = 9;
	st[BTT_KEY_BALANCE] = 9;
	CHECK(!btt_hp_mix(st, &mode, &bal) && mode == BTT_CH_STEREO && bal == BTT_BAL_CENTRE, "out of range must be stereo and centre");
	st[BTT_KEY_BALANCE] = -1;
	CHECK(!btt_hp_mix(st, &mode, &bal) && bal == BTT_BAL_CENTRE, "negative balance");
}

static void test_installer_mix(void)
{
	int st[BTT_KEY_COUNT];
	int16_t blk[BQ_FRAMES * 2];
	fill_sine(blk, 500, 20000, 0, 0);

	memset(&g_tai, 0, sizeof(g_tai));
	btt_bteq_restore();
	bteq_ref = 0;
	settings(st, 1, BTT_SOUND_ORIGINAL);
	btt_bteq_sync(st);
	CHECK(g_tai.calls == 0, "installed for Stereo, centre and a flat preset");

	/* a channel mode alone installs the hook, with the filter flat */
	st[BTT_KEY_CHANNELS] = BTT_CH_MONO;
	size_t mark = strlen(g_all_log);
	btt_bteq_sync(st);
	CHECK(g_tai.calls == 1, "not installed for Mono");
	g_clock_us += 10667;
	CHECK(bteq_hook(0, 0, blk, BQ_BLOCK_BYTES) == g_orig_ret && g_seen_buf == bteq_eq.out, "not mixing");
	CHECK(bteq_eq.set[bteq_eq.idx].active == 0 && bteq_eq.set[bteq_eq.idx].mix_on == 1 && g_seen_data[10] == g_seen_data[11],
	      "Mono with a flat filter");
	/* a flat filter has no pre-gain: the log says "0 dB", never "-0.0 dB" */
	CHECK(strstr(g_all_log + mark, "pre-gain 0 dB, channels") && !strstr(g_all_log + mark, "-0."), "flat filter's log line: %s", g_all_log + mark);

	/* a balance change: no second install, the new gains in use */
	st[BTT_KEY_CHANNELS] = BTT_CH_STEREO;
	st[BTT_KEY_BALANCE] = 1;
	btt_bteq_sync(st);
	CHECK(g_tai.calls == 1 && bteq_eq.set[bteq_eq.idx].mix_mode == BQ_MIX_STEREO && bteq_eq.set[bteq_eq.idx].gain_l == 32768 &&
	      bteq_eq.set[bteq_eq.idx].gain_r == 8231, "balance change (calls %d, gains %d %d)", g_tai.calls,
	      bteq_eq.set[bteq_eq.idx].gain_l, bteq_eq.set[bteq_eq.idx].gain_r);

	/* the filter and the stage together, and each change republishes */
	st[BTT_KEY_SOUND] = BTT_SOUND_BASS;
	mark = strlen(g_all_log);
	btt_bteq_sync(st);
	CHECK(bteq_eq.set[bteq_eq.idx].active == 3 && bteq_eq.set[bteq_eq.idx].mix_on == 1, "both on");
	CHECK(strstr(g_all_log + mark, "pre-gain -") && strstr(g_all_log + mark, " dB, channels"), "an active filter's log line: %s", g_all_log + mark);
	uint32_t gen = bteq_eq.set[bteq_eq.idx].gen;
	btt_bteq_sync(st);
	CHECK(bteq_eq.set[bteq_eq.idx].gen == gen, "republished with nothing changed");

	/* back to Stereo and the centre: the stage is off at once, the filter still runs, the hook stays */
	st[BTT_KEY_BALANCE] = BTT_BAL_CENTRE;
	btt_bteq_sync(st);
	CHECK(bteq_eq.set[bteq_eq.idx].mix_on == 0 && bteq_eq.set[bteq_eq.idx].active == 3 && g_tai.releases == 0, "stage not off");

	/* everything default: flat, the block passes on as it is, the hook stays */
	st[BTT_KEY_SOUND] = BTT_SOUND_ORIGINAL;
	btt_bteq_sync(st);
	g_clock_us += 10667;
	bteq_hook(0, 0, blk, BQ_BLOCK_BYTES);
	CHECK(g_seen_buf == blk && g_tai.releases == 0, "defaults must pass every block on untouched");

	/* switched off with a mode and a balance set: stereo and the centre */
	st[BTT_KEY_CHANNELS] = BTT_CH_SWAP;
	st[BTT_KEY_BALANCE] = 7;
	btt_bteq_sync(st);
	CHECK(bteq_eq.set[bteq_eq.idx].mix_on == 1, "swap and R3");
	st[BTT_KEY_ENABLE] = 0;
	btt_bteq_sync(st);
	g_clock_us += 10667;
	bteq_hook(0, 0, blk, BQ_BLOCK_BYTES);
	CHECK(bteq_eq.set[bteq_eq.idx].mix_on == 0 && g_seen_buf == blk && g_tai.releases == 0, "off, but the block was changed");
	btt_bteq_restore();
}

static void test_cost(void)
{
	static const int all[5] = { 4, 1, -2, 1, 4 };
	BqEq e;
	new_eq(&e, all);
	int bands = __builtin_popcount(e.set[e.idx].active);
	/* each band and sample: 5 multiply-adds (b0 x, b1 x, b2 x, a1 y, a2 y) */
	long macs = (long)bands * 2 * BQ_FRAMES * 5;
	CHECK(bands == 5 && macs == 25600, "macs per block: %ld", macs);

	int16_t blk[BQ_FRAMES * 2];
	fill_sine(blk, 1000, 20000, 0, 0);
	struct timespec t0, t1;
	enum { N = 4000 };
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (int k = 0; k < N; k++)
		bq_process(&e, blk, (uint32_t)k * 10667u);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	double ns = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / N;
	printf("  cost: %ld multiply-adds per block (all five bands, both channels), %.1f/s;\n", macs, macs * 48000.0 / BQ_FRAMES);
	printf("        this computer: %.1f us per block. On the console (444 MHz, an assumed 3-5 cycles per\n", ns / 1000);
	printf("        multiply-add with the loads and stores): %.2f-%.2f ms per 10.67 ms block = %.1f-%.1f %% of one core\n",
	       macs * 3 / 444e3, macs * 5 / 444e3, macs * 3 / 444e3 / 10.667 * 100, macs * 5 / 444e3 / 10.667 * 100);
}

int main(int argc, char **argv)
{
	static const struct { const char *name; void (*fn)(void); } tests[] = {
		{ "presets as band gains", test_hpgains },
		{ "coefficient table", test_table },
		{ "flat preset passes through", test_flat_passthrough },
		{ "frequency response", test_response },
		{ "no clipping", test_noclip },
		{ "other lengths and pointers pass through", test_other_lengths },
		{ "memory cleared after a pause", test_gap_reset },
		{ "set hand-over logic", test_swap_logic },
		{ "set hand-over against a thread", test_swap_threads },
		{ "installer", test_installer },
		{ "channels and balance: the plan", test_mix_plan },
		{ "channels and balance: mono, swap, balance on real blocks", test_mix_run },
		{ "channels and balance: through the hook", test_mix_hook },
		{ "AAC streaming: the engine's feed from the hook", test_aac_feed },
		{ "channels and balance: from the settings", test_hp_mix },
		{ "installer: channels and balance", test_installer_mix },
		{ "cost", test_cost },
	};
	int n = (int)(sizeof(tests) / sizeof(tests[0])), fails = 0;

	if (argc > 1) {   /* a child runs one test, so a failed check ends only its own test */
		tests[atoi(argv[1])].fn();
		return 0;
	}
	for (int i = 0; i < n; i++) {
		char cmd[512];
		fflush(stdout);
		snprintf(cmd, sizeof(cmd), "'%s' %d", argv[0], i);
		int rc = system(cmd);
		printf("%s %s\n", rc == 0 ? "ok  " : "FAIL", tests[i].name);
		fails += rc != 0;
	}
	printf("test_bteq: %d tests, %d failed\n", n, fails);
	return fails ? 1 : 0;
}
