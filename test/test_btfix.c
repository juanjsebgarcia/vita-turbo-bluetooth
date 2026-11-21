/*
 * test_btfix.c - host test of the byte-checked changes to the Bluetooth code (kernel/btfix.c): the
 * timing fix, the latency levels and the wireless controller response. Compiled into this file as it
 * is, with taiHEN's calls replaced by fakes that really patch and unpatch a stand-in for the code.
 *
 *   make test      (cc -Itest/host test/test_btfix.c, then run)
 *
 * What is proved, on the code's bytes themselves:
 *   - nothing is written until the bytes are the stock bytes the change was checked against
 *     (or, for the timing fix, already the fixed bytes from another plugin); on a mismatch, a
 *     missing module or a failed lookup nothing is ever written, however often it is asked;
 *   - the timing fix writes 512 in place of 8000 (4 bytes); the latency writes 11 (Low) or 7
 *     (Lowest) in place of 23 (1 byte), and a change of level goes straight from one to the other;
 *     the controller response writes 0 in place of 18 at two places (2 bytes each), both or neither;
 *     each once, however many passes follow;
 *   - Standard latency and Standard controller response write nothing at all;
 *   - switching any of them off, and module stop, put the stock bytes back exactly (compared byte
 *     for byte against the original); a stop with nothing in place releases nothing;
 *   - when another plugin has already fixed the timing, that change is left as it is, never
 *     written and never removed;
 *   - controller code that is not the stock code turns only the controller response off: the
 *     timing fix and the latency still work;
 *   - a failed write is not retried on every pass, but is after the setting changes.
 */
#define BTT_HOST_INJECT 1
#include <btt_host.h>
#include <stdlib.h>

#include "../kernel/btfix.c"

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

/* ---- the stand-in for the sender's code ------------------------------------------------- */

#define TEXT_SIZE 0x14000
static uint8_t g_text_mem[TEXT_SIZE];
static uint8_t g_pristine[TEXT_SIZE];

/* "stock", "fixed" (the timing fixed by someone else), "wrong" (a different build); ctrl 0 stock,
 * 1 the first controller site differs, 2 the second */
static void make_text3(int ts, int queue, int ctrl)
{
	memset(g_text_mem, 0x5A, sizeof(g_text_mem));
	memcpy(g_text_mem + OFF_TS, ts == 0 ? k_ts_stock : ts == 1 ? k_ts_fixed : (const uint8_t[4]){ 1, 2, 3, 4 }, 4);
	memcpy(g_text_mem + OFF_QUEUE, queue == 0 ? k_q_stock : (const uint8_t[2]){ 0x19, 0x2B }, 2);
	memcpy(g_text_mem + OFF_CTRL_A, ctrl == 1 ? (const uint8_t[2]){ 0x13, 0x24 } : k_c_stock, 2);
	memcpy(g_text_mem + OFF_CTRL_B, ctrl == 2 ? (const uint8_t[2]){ 0x13, 0x24 } : k_c_stock, 2);
	memcpy(g_pristine, g_text_mem, sizeof(g_text_mem));
}
static void make_text(int ts, int queue) { make_text3(ts, queue, 0); }

/* ---- fake taiHEN -------------------------------------------------------------------------- */

static int g_have_module = 1, g_offset_ok = 1, g_lookups;
static int g_fail_ts, g_fail_q, g_fail_ca, g_fail_cb;   /* make the next injects at those offsets fail */
static struct { int used; uint32_t off; size_t n; uint8_t orig[8]; } g_inj[8];
static int g_injects, g_releases;

int taiGetModuleInfoForKernel(SceUID pid, const char *name, tai_module_info_t *info)
{
	g_lookups++;
	CHECK(pid == KERNEL_PID && !strcmp(name, "SceBt"), "wrong module looked up: %s", name);
	if (!g_have_module)
		return -1;
	info->modid = 0x777;
	return 0;
}
int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr)
{
	CHECK(modid == 0x777 && segidx == 0 && offset == 0, "wrong segment asked for");
	if (!g_offset_ok)
		return -1;
	*addr = (uintptr_t)g_text_mem;
	return 0;
}
SceUID taiInjectDataForKernel(SceUID pid, SceUID modid, int segidx, uint32_t offset, const void *data, size_t size)
{
	CHECK(pid == KERNEL_PID && modid == 0x777 && segidx == 0, "wrong inject target");
	CHECK(size <= 8 && offset + size <= TEXT_SIZE, "inject out of range");
	if ((offset == OFF_TS && g_fail_ts) || (offset == OFF_QUEUE && g_fail_q) ||
	    (offset == OFF_CTRL_A && g_fail_ca) || (offset == OFF_CTRL_B && g_fail_cb))
		return -1;
	int i;
	for (i = 0; i < 8 && g_inj[i].used; i++) ;
	CHECK(i < 8, "too many injections");
	g_inj[i].used = 1;
	g_inj[i].off = offset;
	g_inj[i].n = size;
	memcpy(g_inj[i].orig, g_text_mem + offset, size);
	memcpy(g_text_mem + offset, data, size);
	g_injects++;
	return 0x100 + i;
}
int taiInjectReleaseForKernel(SceUID uid)
{
	int i = uid - 0x100;
	CHECK(i >= 0 && i < 8 && g_inj[i].used, "released something not injected (uid 0x%x)", uid);
	memcpy(g_text_mem + g_inj[i].off, g_inj[i].orig, g_inj[i].n);
	g_inj[i].used = 0;
	g_releases++;
	return 0;
}

void btt_klog_line(const char *line)
{
	if (getenv("BTT_TEST_VERBOSE"))
		printf("    %s\n", line);
}

static int stock(void) { return !memcmp(g_text_mem, g_pristine, sizeof(g_text_mem)); }
static int ts_is_fixed(void) { return !memcmp(g_text_mem + OFF_TS, k_ts_fixed, 4); }
static int q_is(int v) { return g_text_mem[OFF_QUEUE] == v && g_text_mem[OFF_QUEUE + 1] == 0x2B; }
static int q_is_low(void) { return q_is(11); }
static int ctrl_fast(void)
{
	return !memcmp(g_text_mem + OFF_CTRL_A, k_c_fast, 2) && !memcmp(g_text_mem + OFF_CTRL_B, k_c_fast, 2);
}
/* the old two-argument form of the calls: the timing fix and Low latency */
#define SYNC(ts, ll) btt_bt_sync((ts), (ll) ? BTT_LAT_LOW : BTT_LAT_STANDARD, 0)

/* ---- tests -------------------------------------------------------------------------------- */

static void test_stock_cycle(void)
{
	make_text(0, 0);
	SYNC(0, 0);
	CHECK(g_injects == 0 && stock(), "wrote with both changes off");

	SYNC(1, 0);
	CHECK(g_injects == 1 && ts_is_fixed() && !q_is_low(), "timing fix not in place alone");
	CHECK(!memcmp(g_text_mem + OFF_TS, (const uint8_t[]){ 0x18, 0xF5, 0x00, 0x78 }, 4), "the timing bytes are not 512");
	SYNC(1, 0);
	SYNC(1, 0);
	CHECK(g_injects == 1, "the timing fix was written again (%d)", g_injects);

	SYNC(1, 1);
	CHECK(g_injects == 2 && ts_is_fixed() && q_is_low(), "low latency not in place");
	CHECK(g_text_mem[OFF_QUEUE] == 11, "the queue limit is not 11");
	SYNC(1, 1);
	CHECK(g_injects == 2, "written again (%d)", g_injects);

	SYNC(1, 0);   /* low latency off alone: the queue is back, the timing stays */
	CHECK(g_releases == 1 && ts_is_fixed() && !q_is_low() && g_text_mem[OFF_QUEUE] == 23, "low latency not taken out alone");

	SYNC(1, 1);
	SYNC(0, 0);   /* the master switch off: both out, byte for byte stock */
	CHECK(g_releases == 3 && stock(), "not back to stock after switching off");

	SYNC(0, 1);   /* low latency alone, without the timing fix */
	CHECK(q_is_low() && !ts_is_fixed(), "low latency alone");
	SYNC(1, 1);
	CHECK(q_is_low() && ts_is_fixed(), "both on again");
	btt_bt_restore();    /* module stop */
	CHECK(stock() && g_releases == g_injects, "module stop did not restore the stock bytes (%d/%d)", g_releases, g_injects);
	int r = g_releases;
	btt_bt_restore();
	CHECK(g_releases == r, "a second stop released something");
}

static void test_stop_with_nothing(void)
{
	make_text(0, 0);
	btt_bt_restore();                       /* never looked at the sender */
	CHECK(g_releases == 0 && g_lookups == 0 && stock(), "stop with nothing in place touched something");
	SYNC(0, 0);
	btt_bt_restore();
	CHECK(g_releases == 0 && g_injects == 0 && stock(), "stop after idle passes touched something");
}

static void test_other_plugin_fixed_timing(void)
{
	make_text(1, 0);   /* the timing already fixed by someone else */
	SYNC(1, 1);
	CHECK(g_injects == 1 && q_is_low(), "timing written over another plugin's fix, or low latency missing (%d)", g_injects);
	CHECK(ts_is_fixed(), "the other plugin's fix is gone");
	SYNC(0, 0);
	btt_bt_restore();
	CHECK(g_releases == 1 && ts_is_fixed() && g_text_mem[OFF_QUEUE] == 23, "left the other plugin's fix as it was, or the queue not back");
}

static void refuses(const char *what)
{
	for (int i = 0; i < 5; i++) {
		SYNC(1, 1);
		SYNC(1, 0);
		SYNC(0, 1);
	}
	btt_bt_restore();
	CHECK(g_injects == 0 && g_releases == 0, "%s: something was written (%d)", what, g_injects);
	CHECK(g_lookups == 1, "%s: the module was looked up %d times", what, g_lookups);
	CHECK(!memcmp(g_text_mem, g_pristine, sizeof(g_text_mem)) || !g_have_module, "%s: the code changed", what);
}

static void test_wrong_timing_bytes(void) { make_text(2, 0); refuses("timing bytes of another build"); }
static void test_wrong_queue_bytes(void) { make_text(0, 1); refuses("queue bytes of another build"); }
static void test_wrong_both(void) { make_text(2, 1); refuses("both of another build"); }
static void test_module_missing(void) { make_text(0, 0); g_have_module = 0; refuses("module missing"); }
static void test_offset_fails(void) { make_text(0, 0); g_offset_ok = 0; refuses("no text address"); }

static void test_failed_write_not_retried(void)
{
	make_text(0, 0);
	g_fail_ts = g_fail_q = 1;
	for (int i = 0; i < 5; i++)
		SYNC(1, 1);
	CHECK(g_injects == 0 && stock(), "a failed write changed the code");
	g_fail_ts = g_fail_q = 0;
	int calls = 0;
	SYNC(1, 1);       /* still switched on: not retried */
	CHECK(g_injects == 0 && stock(), "retried without being switched off and on");
	SYNC(0, 0);
	SYNC(1, 1);       /* off and on again */
	calls = g_injects;
	CHECK(calls == 2 && ts_is_fixed() && q_is_low(), "not retried after off and on (%d)", calls);
	btt_bt_restore();
	CHECK(stock(), "not restored");
}


/* Latency: Standard writes nothing; Low and Lowest are 11 and 7 on the same byte; a change of level
 * goes straight from one to the other; every way back is byte for byte stock. */
static void test_latency_levels(void)
{
	make_text(0, 0);
	btt_bt_sync(0, BTT_LAT_STANDARD, 0);
	btt_bt_sync(1, BTT_LAT_STANDARD, 0);
	CHECK(g_injects == 1 && ts_is_fixed() && q_is(23), "Standard wrote the queue limit (%d)", g_injects);

	btt_bt_sync(1, BTT_LAT_LOW, 0);
	CHECK(q_is(11) && g_injects == 2, "Low is not 11");
	btt_bt_sync(1, BTT_LAT_LOW, 0);
	CHECK(g_injects == 2, "Low written again");
	btt_bt_sync(1, BTT_LAT_LOWEST, 0);
	CHECK(q_is(7) && g_injects == 3 && g_releases == 1, "Lowest is not 7 (%d %d)", g_injects, g_releases);
	btt_bt_sync(1, BTT_LAT_LOWEST, 0);
	CHECK(g_injects == 3, "Lowest written again");
	btt_bt_sync(1, BTT_LAT_LOW, 0);
	CHECK(q_is(11) && g_injects == 4, "Lowest to Low");
	btt_bt_sync(1, BTT_LAT_LOWEST, 0);
	btt_bt_sync(1, BTT_LAT_STANDARD, 0);
	CHECK(q_is(23) && ts_is_fixed(), "Lowest to Standard did not put 23 back");
	btt_bt_sync(1, BTT_LAT_LOWEST, 0);
	btt_bt_sync(0, BTT_LAT_STANDARD, 0);   /* the master switch off: the queue and the timing both out */
	CHECK(stock(), "not stock after switching off from Lowest");
	btt_bt_sync(0, 7, 0);                  /* a level out of range is Standard */
	btt_bt_sync(0, -1, 0);
	CHECK(stock(), "an out-of-range level wrote something");
	btt_bt_sync(1, BTT_LAT_LOWEST, 0);
	btt_bt_restore();
	CHECK(stock() && g_releases == g_injects, "module stop from Lowest (%d/%d)", g_releases, g_injects);
}

/* Controller response: Standard writes nothing; Fast writes 0 at both places, both or neither. */
static void test_ctrl_fast(void)
{
	make_text(0, 0);
	btt_bt_sync(1, BTT_LAT_STANDARD, 0);
	CHECK(!ctrl_fast() && g_injects == 1, "controller written for Standard");
	btt_bt_sync(1, BTT_LAT_STANDARD, 1);
	CHECK(ctrl_fast() && g_injects == 3, "Fast not in at both places (%d)", g_injects);
	CHECK(g_text_mem[OFF_CTRL_A] == 0x00 && g_text_mem[OFF_CTRL_A + 1] == 0x24 &&
	      g_text_mem[OFF_CTRL_B] == 0x00 && g_text_mem[OFF_CTRL_B + 1] == 0x24, "the bytes are not 'movs r4,#0'");
	btt_bt_sync(1, BTT_LAT_STANDARD, 1);
	btt_bt_sync(1, BTT_LAT_LOW, 1);
	CHECK(g_injects == 4, "written again (%d)", g_injects);
	btt_bt_sync(1, BTT_LAT_LOW, 0);
	CHECK(!ctrl_fast() && q_is_low() && ts_is_fixed(), "Standard did not put the controller code back alone");
	CHECK(!memcmp(g_text_mem + OFF_CTRL_A, k_c_stock, 2) && !memcmp(g_text_mem + OFF_CTRL_B, k_c_stock, 2), "bytes differ");
	btt_bt_sync(1, BTT_LAT_LOW, 1);
	btt_bt_sync(0, BTT_LAT_STANDARD, 0);   /* the master switch off */
	CHECK(stock(), "not stock after switching off with Fast");
	btt_bt_sync(1, BTT_LAT_LOWEST, 1);
	btt_bt_restore();
	CHECK(stock() && g_releases == g_injects, "module stop with everything on (%d/%d)", g_releases, g_injects);
	int r = g_releases;
	btt_bt_restore();
	CHECK(g_releases == r, "a second stop released something");
}

/* Controller code that is not the stock code: only the controller response is refused. */
static void test_ctrl_unrecognised(void)
{
	for (int which = 1; which <= 2; which++) {
		g_injects = g_releases = 0;
		make_text3(0, 0, which);
		for (int i = 0; i < 4; i++)
			btt_bt_sync(1, BTT_LAT_LOWEST, 1);
		CHECK(g_text_mem[OFF_CTRL_A] == (which == 1 ? 0x13 : 0x12) && g_text_mem[OFF_CTRL_B] == (which == 2 ? 0x13 : 0x12),
		      "the controller code was written over (site %d)", which);
		CHECK(ts_is_fixed() && q_is(7) && g_injects == 2, "the timing fix and the latency were not applied (%d)", g_injects);
		btt_bt_restore();
		CHECK(g_releases == 2 && !memcmp(g_text_mem, g_pristine, sizeof(g_text_mem)), "not back to the original");
	}
}

/* A refused controller write is never half done, and is tried again only after Standard. */
static void test_ctrl_failures(void)
{
	make_text(0, 0);
	g_fail_cb = 1;   /* the second place is refused after the first went in */
	for (int i = 0; i < 4; i++)
		btt_bt_sync(1, BTT_LAT_STANDARD, 1);
	CHECK(!ctrl_fast() && !memcmp(g_text_mem + OFF_CTRL_A, k_c_stock, 2), "half a change was left in");
	CHECK(g_injects == 2 && g_releases == 1, "the first place was not taken back (%d/%d)", g_injects, g_releases);
	g_fail_cb = 0;
	btt_bt_sync(1, BTT_LAT_STANDARD, 1);   /* still Fast: not tried again */
	CHECK(!ctrl_fast() && g_injects == 2, "retried without being switched off and on");
	btt_bt_sync(1, BTT_LAT_STANDARD, 0);
	btt_bt_sync(1, BTT_LAT_STANDARD, 1);
	CHECK(ctrl_fast(), "not retried after Standard");
	btt_bt_restore();
	CHECK(stock(), "not restored");

	/* the first place refused: the second is not even tried */
	make_text(0, 0);
	g_injects = g_releases = 0;
	g_fail_ca = 1;
	btt_bt_sync(1, BTT_LAT_STANDARD, 1);
	CHECK(g_injects == 1 && ts_is_fixed() && !memcmp(g_text_mem + OFF_CTRL_A, k_c_stock, 2) &&
	      !memcmp(g_text_mem + OFF_CTRL_B, k_c_stock, 2), "the first place refused: something else was written (%d)", g_injects);
	btt_bt_restore();
	CHECK(stock(), "not restored");

	/* a refused level: tried again only after the wanted level changes */
	make_text(0, 0);
	g_injects = g_releases = 0;
	g_fail_ca = 0;
	g_fail_q = 1;
	for (int i = 0; i < 4; i++)
		btt_bt_sync(0, BTT_LAT_LOWEST, 0);
	CHECK(g_injects == 0 && stock(), "a refused level changed the code");
	g_fail_q = 0;
	btt_bt_sync(0, BTT_LAT_LOWEST, 0);
	CHECK(g_injects == 0, "retried the same level");
	btt_bt_sync(0, BTT_LAT_LOW, 0);   /* another level is a new request */
	CHECK(q_is_low(), "Low not applied after Lowest was refused");
	btt_bt_sync(0, BTT_LAT_LOWEST, 0);
	CHECK(q_is(7), "Lowest not applied once it works again");
	btt_bt_restore();
	CHECK(stock(), "not restored");
}

/* The saved key of 0.1 (low latency 1 = on) is Low. */
static void test_old_key(void)
{
	CHECK(BTT_LAT_LOW == 1 && BTT_LAT_STANDARD == 0 && BTT_LAT_LOWEST == 2, "latency values");
	make_text(0, 0);
	btt_bt_sync(1, 1 /* what a 0.1 settings file holds for "on" */, 0);
	CHECK(q_is(11), "a saved 1 is not Low");
	btt_bt_restore();
}

int main(int argc, char **argv)
{
	static const struct { const char *name; void (*fn)(void); } tests[] = {
		{ "stock bytes: both changes in, out, in, stop", test_stock_cycle },
		{ "stop with nothing in place releases nothing", test_stop_with_nothing },
		{ "timing already fixed by another plugin", test_other_plugin_fixed_timing },
		{ "refuses on other timing bytes", test_wrong_timing_bytes },
		{ "refuses on other queue bytes", test_wrong_queue_bytes },
		{ "refuses on both other", test_wrong_both },
		{ "refuses when the sender is missing", test_module_missing },
		{ "refuses when its code address is missing", test_offset_fails },
		{ "a failed write is not retried every pass", test_failed_write_not_retried },
		{ "latency: Standard, Low, Lowest, every way back", test_latency_levels },
		{ "controller response: Fast in and out at both places", test_ctrl_fast },
		{ "controller code not recognised: only that setting is out", test_ctrl_unrecognised },
		{ "controller and latency refusals: never half, retried after a change", test_ctrl_failures },
		{ "a saved 1 from 0.1 is Low", test_old_key },
	};
	int n = (int)(sizeof(tests) / sizeof(tests[0])), fails = 0;

	if (argc > 1) {   /* a child runs one test: btfix.c keeps state in statics */
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
	printf("test_btfix: %d tests, %d failed\n", n, fails);
	return fails ? 1 : 0;
}
