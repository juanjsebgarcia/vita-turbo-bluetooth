/*
 * test_btvol.c - host test of the Bluetooth volume curve and maximum: the generated tables
 * (kernel/btvol_table.h), the mapping (btvol_map.c), the two hook bodies (btvol_hook.c) and the
 * installer (btvol.c) are compiled into this file as they are, with the console's calls replaced by
 * fakes.
 *
 *   make test      (cc -Itest/host test/test_btvol.c, then run)
 *
 * What is proved:
 *   - the tables: the console's own numbers are (step * 127 + 15) / 30; every one of the 18 tables is
 *     strictly increasing from 0 at step 0 (so every step can be told apart), stays at or below its
 *     maximum (100, 90 ... 50 % of 127) and ends on it; Standard with 100 % is the console's numbers;
 *   - the round trip: for every curve, every maximum and every one of the 31 steps, what the console
 *     sends maps to the table's number, and that number read back is exactly the console's own number
 *     for the step, which the console's own formula turns into the same step; the map and its inverse
 *     never decrease, for every input from 0 to 127;
 *   - Standard with 100 % is the identity on every input (also numbers no step produces), and a
 *     number above 127 is never changed;
 *   - the hooks: the headphones' address and every other argument go through untouched, the number is
 *     mapped on the way out and the reported one on the way back, an error from the original is
 *     returned as it came, and with no reference set nothing is called;
 *   - the whole loop as the console runs it: for every setting and step, the number sent, echoed back
 *     by headphones that report what they were told, gives the same step;
 *   - the installer: the right library and function numbers, nothing for Standard with 100 % or with
 *     Bluetooth Turbo off, both hooks in for anything else, no second install on a change, the
 *     console's own numbers at once when switched off with the hooks left in, out at module stop,
 *     and the retry rules after a failure (no mapping with one hook alone).
 */
#define BTT_HOST_TAI 1
#include <btt_host.h>
#include <stdlib.h>

#include "../kernel/hpgains.c"
#include "../kernel/btvol_map.c"
#include "../kernel/btvol_hook.c"

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

/* ---- fakes ----------------------------------------------------------------------------- */

static struct { uint32_t a, b, c, d; int calls; } g_send_seen;
static int g_send_ret = 0x77, g_read_ret, g_read_calls;
static uint32_t g_echo;                 /* the number the "headphones" last accepted */

static int fake_send(uint32_t a, uint32_t b, uint32_t vol, uint32_t d)
{
	g_send_seen.a = a; g_send_seen.b = b; g_send_seen.c = vol; g_send_seen.d = d;
	g_send_seen.calls++;
	g_echo = vol;
	return g_send_ret;
}
static int fake_read(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	g_read_calls++;
	return g_read_ret;
}

static struct { int calls, releases, fail_nid; uint32_t nid[4], lib[4]; const void *hook[4]; const char *mod[4]; } g_tai;
SceUID taiHookFunctionImportForKernel(SceUID pid, tai_hook_ref_t *ref, const char *module,
                                      uint32_t lib_nid, uint32_t func_nid, const void *hook)
{
	int i = g_tai.calls++;
	if (i < 4) {
		g_tai.mod[i] = module; g_tai.lib[i] = lib_nid; g_tai.nid[i] = func_nid; g_tai.hook[i] = hook;
	}
	if (func_nid == (uint32_t)g_tai.fail_nid)
		return -1;
	*ref = (tai_hook_ref_t)(func_nid == 0x7689DA3D ? (void *)fake_send : (void *)fake_read);
	return 0x1000 + g_tai.calls;
}
int taiHookReleaseForKernel(SceUID uid, tai_hook_ref_t ref) { g_tai.releases++; return 0; }
int ksceKernelDelayThread(SceUInt delay) { return 0; }
static int g_log_lines;
void btt_klog_line(const char *line)
{
	g_log_lines++;
	if (getenv("BTT_TEST_VERBOSE"))
		printf("    %s\n", line);
}

#include "../kernel/btvol.c"

static void settings(int *st, int enable, int curve, int max)
{
	memset(st, 0, sizeof(int) * BTT_KEY_COUNT);
	st[BTT_KEY_ENABLE] = enable;
	st[BTT_KEY_VOLCURVE] = curve;
	st[BTT_KEY_VOLMAX] = max;
}

/* The console's own maths for a step and back (what SceAVConfig does). */
static uint32_t console_abs(int v) { return (uint32_t)((v * 127 + 15) / 30); }
static int console_step(uint32_t abs) { return (int)((abs * 30 + 63) / 127); }

/* ---- tests ----------------------------------------------------------------------------- */

static void test_tables(void)
{
	CHECK(BV_STEPS == 31 && BV_CURVES == BTT_VC_COUNT && BV_MAXES == BTT_VOLMAX_COUNT, "table dimensions");
	for (int v = 0; v < BV_STEPS; v++)
		CHECK(BV_STOCK[v] == console_abs(v), "stock number of step %d: %d", v, BV_STOCK[v]);
	CHECK(!memcmp(BV_TAB[0], BV_STOCK, BV_STEPS), "Standard with 100 %% is not the console's numbers");
	for (int c = 0; c < BV_CURVES; c++)
		for (int m = 0; m < BV_MAXES; m++) {
			const uint8_t *t = BV_TAB[c * BV_MAXES + m];
			int cap = (127 * BTT_VOLMAX_PCT(m) + 50) / 100;
			CHECK(t[0] == 0, "curve %d max %d: step 0 is %d", c, m, t[0]);
			for (int v = 1; v < BV_STEPS; v++)
				CHECK(t[v] > t[v - 1], "curve %d max %d: step %d (%d) does not rise above step %d (%d)",
				      c, m, v, t[v], v - 1, t[v - 1]);
			CHECK(t[BV_STEPS - 1] == cap, "curve %d max %d: top step is %d, expected %d", c, m, t[BV_STEPS - 1], cap);
			CHECK(cap <= 127, "cap");
		}
	/* the shape: Finer is quieter than Standard at every low step, Louder is louder */
	for (int m = 0; m < BV_MAXES; m++)
		for (int v = 1; v < 15; v++) {
			CHECK(BV_TAB[BV_MAXES + m][v] <= BV_TAB[m][v], "Finer is louder than Standard at step %d max %d", v, m);
			CHECK(BV_TAB[2 * BV_MAXES + m][v] >= BV_TAB[m][v], "Louder is quieter than Standard at step %d max %d", v, m);
		}
	/* a lower maximum never sends more than a higher one at any step of the same curve */
	for (int c = 0; c < BV_CURVES; c++)
		for (int m = 1; m < BV_MAXES; m++)
			for (int v = 0; v < BV_STEPS; v++)
				CHECK(BV_TAB[c * BV_MAXES + m][v] <= BV_TAB[c * BV_MAXES + m - 1][v], "maximum %d step %d", m, v);
}

static void test_round_trip(void)
{
	int n = 0;
	for (uint32_t sel = 0; sel < BV_TABLES; sel++)
		for (int v = 0; v < BV_STEPS; v++) {
			uint32_t asked = console_abs(v);
			uint32_t sent = bv_send_map(sel, asked);
			CHECK(sent == BV_TAB[sel][v], "sel %u step %d: sent %u, table says %u", sel, v, sent, BV_TAB[sel][v]);
			uint32_t back = bv_read_map(sel, sent);
			CHECK(back == asked, "sel %u step %d: asked %u, sent %u, read back as %u", sel, v, asked, sent, back);
			CHECK(console_step(back) == v, "sel %u: step %d comes back as step %d", sel, v, console_step(back));
			n++;
		}
	/* both directions never decrease and always land on a step, for every input */
	for (uint32_t sel = 0; sel < BV_TABLES; sel++) {
		uint32_t ps = 0, pr = 0;
		for (uint32_t x = 0; x <= 127; x++) {
			uint32_t s = bv_send_map(sel, x), r = bv_read_map(sel, x);
			CHECK(s >= ps && r >= pr, "sel %u: the map decreases at %u", sel, x);
			ps = s; pr = r;
			int on_table = 0, on_stock = 0;
			for (int v = 0; v < BV_STEPS; v++) {
				on_table |= s == BV_TAB[sel][v];
				on_stock |= r == BV_STOCK[v];
			}
			CHECK(!sel || (on_table && on_stock), "sel %u input %u: send %u / read %u is not a step's number", sel, x, s, r);
		}
		CHECK(bv_send_map(sel, 0) == 0 && bv_read_map(sel, 0) == 0, "step 0 (mute) moved");
	}
	printf("  round trip: %d settings x 31 steps, all exact\n", n / BV_STEPS);
}

static void test_identity(void)
{
	/* Standard with 100 %: every input, also the ones no step produces and those above 127 */
	for (uint32_t x = 0; x < 600; x++)
		CHECK(bv_send_map(0, x) == x && bv_read_map(0, x) == x, "identity broken at %u", x);
	/* a table that does not exist is the identity too */
	CHECK(bv_send_map(BV_TABLES, 50) == 50 && bv_read_map(999, 50) == 50, "out-of-range table");
	/* a number above 127 is never changed, whatever the table */
	for (uint32_t sel = 1; sel < BV_TABLES; sel++)
		CHECK(bv_send_map(sel, 128) == 128 && bv_read_map(sel, 255) == 255, "a number above 127 was changed");
}

static void test_hooks(void)
{
	memset(&g_send_seen, 0, sizeof(g_send_seen));
	btvol_send_ref = (tai_hook_ref_t)fake_send;
	btvol_read_ref = (tai_hook_ref_t)fake_read;

	/* Standard: everything through unchanged */
	btvol_sel = 0;
	CHECK(btvol_send_hook(0x11223344, 0x5566, 77, 0xDEAD) == g_send_ret, "send return");
	CHECK(g_send_seen.a == 0x11223344 && g_send_seen.b == 0x5566 && g_send_seen.c == 77 && g_send_seen.d == 0xDEAD &&
	      g_send_seen.calls == 1, "send arguments changed under Standard");
	g_read_ret = 77;
	CHECK(btvol_read_hook(1, 2, 3, 4) == 77, "read changed under Standard");

	/* Finer, 90 %: the number is mapped, the rest is not */
	uint32_t sel = 1 * BV_MAXES + 1;
	btvol_sel = sel;
	CHECK(btvol_send_hook(0x11223344, 0x5566, console_abs(10), 0xDEAD) == g_send_ret, "send return");
	CHECK(g_send_seen.c == BV_TAB[sel][10] && g_send_seen.c != console_abs(10), "volume not mapped (%u)", g_send_seen.c);
	CHECK(g_send_seen.a == 0x11223344 && g_send_seen.b == 0x5566 && g_send_seen.d == 0xDEAD, "address or fourth argument changed");
	g_read_ret = (int)BV_TAB[sel][10];
	CHECK(btvol_read_hook(1, 2, 3, 4) == (int)console_abs(10), "reported number not mapped back");

	/* an error from the original goes back as it came (a negative number is not a volume) */
	g_read_ret = (int)0x802F0608;
	CHECK(btvol_read_hook(1, 2, 3, 4) == (int)0x802F0608, "error changed");
	g_read_ret = 300;    /* positive, but not a volume number */
	CHECK(btvol_read_hook(1, 2, 3, 4) == 300, "a number above 127 was changed");
	g_read_ret = 0;
	CHECK(btvol_read_hook(1, 2, 3, 4) == 0, "zero changed");

	/* no reference yet: the hook is live but taiHEN has not stored it - no call, an error */
	btvol_send_ref = 0;
	btvol_read_ref = 0;
	int before = g_send_seen.calls, rb = g_read_calls;
	CHECK(btvol_send_hook(1, 2, 3, 4) == (int)0x802F1901 && g_send_seen.calls == before, "unset send reference");
	CHECK(btvol_read_hook(1, 2, 3, 4) == (int)0x802F1901 && g_read_calls == rb, "unset read reference");
	CHECK(!btvol_in_send && !btvol_in_read, "left marked as inside");
}

/* Headphones that report what they were told, through both hooks, as the console runs the loop. */
static void test_whole_loop(void)
{
	btvol_send_ref = (tai_hook_ref_t)fake_send;
	btvol_read_ref = (tai_hook_ref_t)fake_read;
	for (uint32_t sel = 0; sel < BV_TABLES; sel++) {
		btvol_sel = sel;
		uint32_t prev = 0;
		for (int v = 0; v < BV_STEPS; v++) {
			btvol_send_hook(1, 2, console_abs(v), 0);       /* the console tells the headphones step v */
			g_read_ret = (int)g_echo;                       /* they report what they were told         */
			int got = btvol_read_hook(1, 2, 0, 0);          /* the console reads it back               */
			CHECK(console_step(got) == v, "sel %u: step %d read back as %d", sel, v, console_step(got));
			CHECK(g_echo >= prev, "sel %u: the volume sent falls at step %d", sel, v);
			prev = g_echo;
		}
	}
	/* headphones with their own rounding (16 steps of 8): the console ends on a step, never a jump back */
	btvol_sel = 1 * BV_MAXES;
	for (int v = 0; v < BV_STEPS; v++) {
		btvol_send_hook(1, 2, console_abs(v), 0);
		g_read_ret = (int)((g_echo + 4) / 8 * 8);
		if (g_read_ret > 127) g_read_ret = 127;
		int step = console_step((uint32_t)btvol_read_hook(1, 2, 0, 0));
		CHECK(step >= 0 && step <= 30 && abs(step - v) <= 3, "rounded headphones: step %d read back as %d", v, step);
	}
}

static void test_installer(void)
{
	int st[BTT_KEY_COUNT];

	memset(&g_tai, 0, sizeof(g_tai));
	btvol_send_ref = btvol_read_ref = 0;
	settings(st, 1, BTT_VC_STANDARD, 0);
	btt_btvol_sync(st);
	CHECK(g_tai.calls == 0 && btvol_sel == 0, "installed for Standard with 100 %%");
	settings(st, 0, BTT_VC_FINER, 3);
	btt_btvol_sync(st);
	CHECK(g_tai.calls == 0, "installed with Bluetooth Turbo off");
	settings(st, 1, 7, 0);
	btt_btvol_sync(st);
	settings(st, 1, 0, 9);
	btt_btvol_sync(st);
	CHECK(g_tai.calls == 0 && btvol_sel == 0, "installed for out-of-range values");

	settings(st, 1, BTT_VC_LOUDER, 2);
	btt_btvol_sync(st);
	CHECK(g_tai.calls == 2, "not installed (%d)", g_tai.calls);
	CHECK(!strcmp(g_tai.mod[0], "SceAVConfig") && g_tai.lib[0] == 0xD48CA62D && g_tai.nid[0] == 0x7689DA3D &&
	      g_tai.hook[0] == (const void *)btvol_send_hook, "wrong send import: %s %08x %08x", g_tai.mod[0], g_tai.lib[0], g_tai.nid[0]);
	CHECK(!strcmp(g_tai.mod[1], "SceAVConfig") && g_tai.lib[1] == 0xD48CA62D && g_tai.nid[1] == 0xC9C70056 &&
	      g_tai.hook[1] == (const void *)btvol_read_hook, "wrong read import: %s %08x %08x", g_tai.mod[1], g_tai.lib[1], g_tai.nid[1]);
	CHECK(btvol_send_ref == (tai_hook_ref_t)fake_send && btvol_read_ref == (tai_hook_ref_t)fake_read, "references");
	CHECK(btvol_sel == 2 * BV_MAXES + 2, "table %u", btvol_sel);

	/* a change: no second install, the table follows */
	settings(st, 1, BTT_VC_FINER, 5);
	btt_btvol_sync(st);
	CHECK(g_tai.calls == 2 && btvol_sel == 1 * BV_MAXES + 5, "change (calls %d, table %u)", g_tai.calls, btvol_sel);
	btt_btvol_sync(st);
	btt_btvol_sync(st);
	CHECK(g_tai.calls == 2, "reinstalled on quiet passes");

	/* switched off: the console's own numbers at once, the hooks never released while running */
	settings(st, 0, BTT_VC_FINER, 5);
	btt_btvol_sync(st);
	CHECK(btvol_sel == 0 && g_tai.releases == 0, "off: table %u, releases %d", btvol_sel, g_tai.releases);
	CHECK(btvol_send_hook(1, 2, 60, 0) == g_send_ret && g_send_seen.c == 60, "off, but the number was changed");
	btt_btvol_sync(st);
	CHECK(g_tai.releases == 0 && btvol_send_ref != 0, "released while the module runs");

	/* Standard with 100 % while on is the same: no mapping, hooks stay */
	settings(st, 1, BTT_VC_FINER, 0);
	btt_btvol_sync(st);
	CHECK(btvol_sel == BV_MAXES, "back on");
	settings(st, 1, BTT_VC_STANDARD, 0);
	btt_btvol_sync(st);
	CHECK(btvol_sel == 0 && g_tai.calls == 2 && g_tai.releases == 0, "Standard with 100 %% while on");

	/* module stop: both out, once */
	btt_btvol_restore();
	CHECK(g_tai.releases == 2 && btvol_send_ref == 0 && btvol_read_ref == 0 && btvol_sel == 0, "restore (%d)", g_tai.releases);
	btt_btvol_restore();
	CHECK(g_tai.releases == 2, "restore twice");
}

static void test_install_failure(void)
{
	int st[BTT_KEY_COUNT];

	/* the second hook refused: the first stays in and passes through; no mapping with one hook alone */
	memset(&g_tai, 0, sizeof(g_tai));
	btvol_send_ref = btvol_read_ref = 0;
	g_tai.fail_nid = (int)0xC9C70056;
	settings(st, 1, BTT_VC_FINER, 2);
	btt_btvol_sync(st);
	btt_btvol_sync(st);
	btt_btvol_sync(st);
	CHECK(g_tai.calls == 2 && btvol_sel == 0, "retried a failed install, or mapped with one hook (%d, %u)", g_tai.calls, btvol_sel);
	g_tai.fail_nid = 0;
	btt_btvol_sync(st);
	CHECK(g_tai.calls == 2 && btvol_sel == 0, "retried without being switched off and on");
	settings(st, 0, BTT_VC_FINER, 2);
	btt_btvol_sync(st);
	settings(st, 1, BTT_VC_FINER, 2);
	btt_btvol_sync(st);
	CHECK(g_tai.calls == 3 && btvol_sel == 1 * BV_MAXES + 2, "not completed after off and on (%d, %u)", g_tai.calls, btvol_sel);
	btt_btvol_restore();
	CHECK(g_tai.releases == 2, "both released at stop (%d)", g_tai.releases);

	/* the first refused: nothing in at all */
	memset(&g_tai, 0, sizeof(g_tai));
	g_tai.fail_nid = 0x7689DA3D;
	settings(st, 1, BTT_VC_FINER, 2);
	btt_btvol_sync(st);
	CHECK(g_tai.calls == 1 && btvol_sel == 0, "the second was tried after the first failed");
	btt_btvol_restore();
	CHECK(g_tai.releases == 0, "released something that was never in");
}

int main(int argc, char **argv)
{
	static const struct { const char *name; void (*fn)(void); } tests[] = {
		{ "tables: strictly increasing, capped, Standard is the console's", test_tables },
		{ "round trip: every curve, maximum and step is exact", test_round_trip },
		{ "Standard with 100 % is the identity", test_identity },
		{ "hooks: only the volume number changes", test_hooks },
		{ "the whole loop, with headphones that echo", test_whole_loop },
		{ "installer", test_installer },
		{ "failed install: no mapping with one hook", test_install_failure },
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
	printf("test_btvol: %d tests, %d failed\n", n, fails);
	return fails ? 1 : 0;
}
