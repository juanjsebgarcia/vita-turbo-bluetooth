/*
 * test_btqual.c - host test of Bluetooth audio quality, Stable and Balanced (the bitpool cap): the hook body
 * (kernel/btqual_hook.c) and the installer (kernel/btqual.c) are compiled into this file as they are,
 * with taiHEN's calls replaced by fakes and the Bluetooth code's routine that applies an A2DP codec
 * configuration by a recorder that stores the headphones' bitpool range in a fake connection.
 *
 *   make test      (cc -Itest/host test/test_btqual.c, then run)
 *
 * What is proved:
 *   - the clamp: the highest bitpool becomes max(lowest, cap) but never more than it was, for every
 *     level (Standard = no cap, Balanced 45, Stable 37) and for every lowest/highest pair from 0 to 64, with
 *     the edge where the lowest bitpool is above the cap (the lowest wins) and a range that is already
 *     upside down (left as it came);
 *   - the hook: the original runs first with every argument as it came and its answer comes back; only when
 *     the call applies a configuration (flag bit 3), and only the connection's highest bitpool changes; a call
 *     that only reads capabilities, a missing connection and Standard write nothing; with no reference set nothing
 *     is called;
 *   - default = stock: Standard installs nothing (not even a look at the Bluetooth code), and a hook that is in
 *     writes nothing once the cap is back to none;
 *   - the installer: the hook goes in once, at text offset 0x732C as Thumb code, only for Balanced or
 *     Stable, only on the stock bytes (the two address loads may hold any immediates, nothing else may
 *     differ); a change of level changes the cap and installs nothing more; Standard sets the cap to none and
 *     leaves the hook in; module stop takes it out; a refused install is not tried again until the level has
 *     gone back to Standard.
 */
#define BTT_HOST_TAI    1
#define BTT_HOST_INJECT 1
#include <btt_host.h>
#include <taihen.h>
#include <stdlib.h>

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

/* ---- fakes ----------------------------------------------------------------------------- */

#define TEXT_SIZE 0x20000
static uint8_t g_text_mem[TEXT_SIZE];
static int g_have_module = 1, g_offset_ok = 1, g_lookups;

int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr);
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

/* a connection: the headphones' lowest and highest bitpool sit at +0x3BB8 and +0x3BBC */
#define CONN_SIZE 0x4000
static uint8_t g_conn[CONN_SIZE];
static uint32_t *bp(void) { return (uint32_t *)(g_conn + 0x3BB8); }

/* the routine that applies a configuration: records what it was given, and when it applies one stores
 * the headphones' range (g_range) in the connection, as the original does */
static struct { int calls; void *conn; uint32_t flags; const void *caps; uint32_t len; } g_orig;
static int g_orig_ret;
static uint32_t g_range[2] = { 2, 53 };
static int fake_cfg(void *conn, uint32_t flags, const void *caps, uint32_t len)
{
	g_orig.calls++; g_orig.conn = conn; g_orig.flags = flags; g_orig.caps = caps; g_orig.len = len;
	if (conn && (flags & 8)) {
		((uint32_t *)((uint8_t *)conn + 0x3BB8))[0] = g_range[0];
		((uint32_t *)((uint8_t *)conn + 0x3BB8))[1] = g_range[1];
	}
	return g_orig_ret;
}

static struct { int calls, releases, fail; uint32_t off; int thumb; const void *hook; SceUID modid; int segidx; } g_tai;
SceUID taiHookFunctionOffsetForKernel(SceUID pid, tai_hook_ref_t *ref, SceUID modid, int segidx,
                                      uint32_t offset, int thumb, const void *hook)
{
	g_tai.calls++;
	CHECK(pid == KERNEL_PID, "wrong pid");
	g_tai.off = offset; g_tai.thumb = thumb; g_tai.hook = hook; g_tai.modid = modid; g_tai.segidx = segidx;
	if (g_tai.fail)
		return -1;
	*ref = (tai_hook_ref_t)fake_cfg;
	return 0x1000;
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

#include "../kernel/btqual_hook.c"
#include "../kernel/btqual.c"

/* the configuration routine's entry as the loader leaves it: the two address loads (movw r7 / movt r7)
 * hold whatever immediates the loader wrote */
static void cfg_site(unsigned lo, unsigned hi)
{
	uint8_t *p = g_text_mem + OFF_CONFIG;

	memcpy(p, k_cfg_head, 12);
	memcpy(g_text_mem + OFF_CFG_TAIL, k_cfg_tail, 14);
	p[4] = (uint8_t)(0x40 | ((lo >> 12) & 0xF)); p[5] = (uint8_t)(0xF2 | (((lo >> 11) & 1) << 2));
	p[6] = (uint8_t)(lo & 0xFF);                 p[7] = (uint8_t)(0x07 | (((lo >> 8) & 7) << 4));
	p[8] = (uint8_t)(0xC0 | ((hi >> 12) & 0xF)); p[9] = (uint8_t)(0xF2 | (((hi >> 11) & 1) << 2));
	p[10] = (uint8_t)(hi & 0xFF);                p[11] = (uint8_t)(0x07 | (((hi >> 8) & 7) << 4));
}

static void reset(void)
{
	memset(g_text_mem, 0x5A, sizeof(g_text_mem));
	cfg_site(0x1234, 0x8101);
	memset(&g_tai, 0, sizeof(g_tai));
	memset(&g_orig, 0, sizeof(g_orig));
	memset(g_conn, 0xAA, sizeof(g_conn));
	g_range[0] = 2; g_range[1] = 53;
	g_have_module = g_offset_ok = 1;
	g_lookups = 0;
	g_orig_ret = 0;
	/* the module's own state, as at module load */
	g_qual_uid = -1; g_qual_failed = 0; btqual_cap = 0; btqual_ref = 0; btqual_in = 0;
}

/* ---- tests ----------------------------------------------------------------------------- */

static void test_clamp(void)
{
	static const uint32_t caps[] = { 0, BP_BALANCED, BP_STABLE };

	CHECK(BP_BALANCED == 45 && BP_STABLE == 37, "the caps");
	for (int c = 0; c < 3; c++)
		for (uint32_t lo = 0; lo <= 64; lo++)
			for (uint32_t hi = 0; hi <= 64; hi++) {
				uint32_t cap = caps[c], got = qual_clamp(cap, lo, hi), want;

				if (!cap)
					want = hi;                                  /* Standard: unchanged */
				else if (lo > hi)
					want = hi;                                  /* an upside-down range is left as it came */
				else {
					want = cap > lo ? cap : lo;                 /* max(lowest, cap) ... */
					if (want > hi)
						want = hi;                          /* ... never above the headphones' own highest */
				}
				CHECK(got == want, "cap %u range %u..%u: got %u, want %u", cap, lo, hi, got, want);
				CHECK(got <= hi, "cap %u range %u..%u: %u above the highest", cap, lo, hi, got);
				if (lo <= hi)
					CHECK(got >= lo, "cap %u range %u..%u: %u below the lowest", cap, lo, hi, got);
			}
	/* the common headphones, 2..53 */
	CHECK(qual_clamp(0, 2, 53) == 53 && qual_clamp(BP_BALANCED, 2, 53) == 45 && qual_clamp(BP_STABLE, 2, 53) == 37, "headphones 2..53");
	/* a fast link, 2..60 */
	CHECK(qual_clamp(0, 2, 60) == 60 && qual_clamp(BP_BALANCED, 2, 60) == 45 && qual_clamp(BP_STABLE, 2, 60) == 37, "a fast link, 2..60");
	/* headphones that cannot go as high as the cap: left alone */
	CHECK(qual_clamp(BP_BALANCED, 2, 40) == 40 && qual_clamp(BP_STABLE, 2, 37) == 37 && qual_clamp(BP_STABLE, 2, 32) == 32, "a highest below the cap");
	/* a lowest above the cap: the lowest wins */
	CHECK(qual_clamp(BP_STABLE, 40, 53) == 40 && qual_clamp(BP_BALANCED, 50, 53) == 50 && qual_clamp(BP_STABLE, 53, 53) == 53, "a lowest above the cap");
}

static void test_hook_behaviour(void)
{
	static const uint8_t caps[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

	reset();
	g_orig_ret = 0x1234;
	btqual_ref = (tai_hook_ref_t)fake_cfg;

	/* no cap (Standard): the original runs with every argument as it came, its answer comes back, nothing is written */
	btqual_cap = 0;
	CHECK(btqual_hook(g_conn, 8, caps, 8) == 0x1234, "answer");
	CHECK(g_orig.calls == 1 && g_orig.conn == g_conn && g_orig.flags == 8 && g_orig.caps == caps && g_orig.len == 8, "Standard changed an argument");
	CHECK(bp()[0] == 2 && bp()[1] == 53, "Standard: the connection holds %u..%u", bp()[0], bp()[1]);
	CHECK(btqual_in == 0, "left the in-hook flag set");

	/* Stable: the original ran first (it stored 2..53), then only the highest is lowered */
	btqual_cap = BP_STABLE;
	CHECK(btqual_hook(g_conn, 8, caps, 8) == 0x1234, "answer");
	CHECK(g_orig.calls == 2 && g_orig.flags == 8 && g_orig.caps == caps && g_orig.len == 8, "Stable changed an argument");
	CHECK(bp()[0] == 2 && bp()[1] == 37, "Stable: the connection holds %u..%u", bp()[0], bp()[1]);
	CHECK(*(uint32_t *)(g_conn + 0x3BB4) == 0xAAAAAAAAu && *(uint32_t *)(g_conn + 0x3BC0) == 0xAAAAAAAAu, "something else in the connection changed");

	/* Balanced, and headphones whose lowest is above the cap */
	btqual_cap = BP_BALANCED;
	btqual_hook(g_conn, 8, caps, 8);
	CHECK(bp()[1] == 45, "Balanced: %u", bp()[1]);
	btqual_cap = BP_STABLE;
	g_range[0] = 40;
	btqual_hook(g_conn, 8, caps, 8);
	CHECK(bp()[0] == 40 && bp()[1] == 40, "lowest above the cap: %u..%u", bp()[0], bp()[1]);
	g_range[0] = 2; g_range[1] = 30;                         /* the headphones' highest is below the cap */
	btqual_hook(g_conn, 8, caps, 8);
	CHECK(bp()[0] == 2 && bp()[1] == 30, "highest below the cap: %u..%u", bp()[0], bp()[1]);

	/* the call that only reads the headphones' capabilities (flag bit 3 clear): the original runs, nothing is written */
	g_range[1] = 53;
	bp()[0] = 7; bp()[1] = 9;
	for (uint32_t flags = 0; flags < 16; flags++) {
		if (flags & 8)
			continue;
		int before = g_orig.calls;
		bp()[0] = 7; bp()[1] = 53;
		btqual_hook(g_conn, flags, caps, 8);
		CHECK(g_orig.calls == before + 1 && g_orig.flags == flags && bp()[0] == 7 && bp()[1] == 53, "flags %u wrote %u..%u", flags, bp()[0], bp()[1]);
	}

	/* no connection: the original is still called, nothing is dereferenced */
	{
		int before = g_orig.calls;
		btqual_hook(NULL, 8, caps, 8);
		CHECK(g_orig.calls == before + 1 && g_orig.conn == NULL, "a call with no connection");
	}
	/* an error from the original comes back as it came */
	g_orig_ret = -5;
	CHECK(btqual_hook(g_conn, 4, caps, 8) == -5, "the original's answer");

	/* the reference not set yet (the hook is live a moment before taiHEN stores it): nothing is called */
	btqual_ref = 0;
	g_orig.calls = 0;
	CHECK(btqual_hook(g_conn, 8, caps, 8) == (int)0x802F1901 && g_orig.calls == 0,
	      "a hook with no reference called the original or returned success");
}

static void test_default_is_stock(void)
{
	/* Standard never looks at, or hooks, anything */
	reset();
	for (int i = 0; i < 6; i++)
		btt_btqual_sync(BTT_Q_STANDARD);
	CHECK(g_lookups == 0 && g_tai.calls == 0 && btqual_cap == 0 && g_qual_uid < 0, "Standard touched something");
	btt_btqual_sync(-1);
	btt_btqual_sync(BTT_Q_COUNT);
	CHECK(g_lookups == 0 && g_tai.calls == 0 && btqual_cap == 0, "an unknown level touched something");
	/* High and Highest are bthq.c's: this setting's cap stays none and nothing is hooked on their account */
	btt_btqual_sync(BTT_Q_HIGH);
	btt_btqual_sync(BTT_Q_HIGHEST);
	CHECK(g_lookups == 0 && g_tai.calls == 0 && btqual_cap == 0 && g_qual_uid < 0, "High or Highest installed the cap");
	btt_btqual_restore();
	CHECK(g_tai.releases == 0, "stop with nothing in released something");

	/* a hook that is in (the user went back to Standard): the connection is the console's own, for every range */
	reset();
	btt_btqual_sync(BTT_Q_STABLE);
	CHECK(g_tai.calls == 1 && btqual_cap == 37, "Stable not in");
	btt_btqual_sync(BTT_Q_STANDARD);
	CHECK(btqual_cap == 0 && g_tai.releases == 0, "Standard: cap %u, releases %d", (unsigned)btqual_cap, g_tai.releases);
	for (uint32_t lo = 0; lo < 64; lo += 7)
		for (uint32_t hi = 0; hi < 64; hi += 5) {
			g_range[0] = lo; g_range[1] = hi;
			btqual_hook(g_conn, 8, NULL, 0);
			CHECK(bp()[0] == lo && bp()[1] == hi, "Standard changed %u..%u to %u..%u", lo, hi, bp()[0], bp()[1]);
		}
}

static void test_installer(void)
{
	reset();
	btt_btqual_sync(BTT_Q_BALANCED);
	CHECK(g_lookups == 1 && g_tai.calls == 1, "lookups %d hooks %d", g_lookups, g_tai.calls);
	CHECK(g_tai.off == 0x732C && g_tai.thumb == 1 && g_tai.hook == (const void *)btqual_hook && g_tai.modid == 0x777 && g_tai.segidx == 0,
	      "hook at %x thumb %d", g_tai.off, g_tai.thumb);
	CHECK(btqual_cap == 45 && g_qual_uid >= 0, "Balanced: cap %u", (unsigned)btqual_cap);
	for (int i = 0; i < 4; i++)
		btt_btqual_sync(BTT_Q_BALANCED);
	CHECK(g_tai.calls == 1 && g_lookups == 1, "installed again on a quiet pass");

	btt_btqual_sync(BTT_Q_STABLE);
	CHECK(btqual_cap == 37 && g_tai.calls == 1 && g_tai.releases == 0, "a change of level: cap %u, hooks %d", (unsigned)btqual_cap, g_tai.calls);

	/* the headphones connect, as the hook sees it */
	btqual_hook(g_conn, 8, NULL, 0);
	CHECK(bp()[1] == 37, "Stable: %u", bp()[1]);
	btt_btqual_sync(BTT_Q_BALANCED);
	btqual_hook(g_conn, 8, NULL, 0);
	CHECK(bp()[1] == 45, "Balanced: %u", bp()[1]);

	/* back to Standard: no cap, the hook stays; then Stable again: no second install */
	btt_btqual_sync(BTT_Q_STANDARD);
	CHECK(btqual_cap == 0 && g_tai.releases == 0, "Standard");
	btqual_hook(g_conn, 8, NULL, 0);
	CHECK(bp()[1] == 53, "Standard: %u", bp()[1]);
	btt_btqual_sync(BTT_Q_STABLE);
	CHECK(g_tai.calls == 1 && btqual_cap == 37, "installed twice");

	/* module stop: out, once; a late call finds no reference and calls nothing */
	btt_btqual_restore();
	CHECK(g_tai.releases == 1 && btqual_cap == 0 && g_qual_uid < 0 && btqual_ref == 0, "release %d", g_tai.releases);
	int before = g_orig.calls;
	btqual_hook(g_conn, 8, NULL, 0);
	CHECK(g_orig.calls == before, "a hook ran with no reference");
	btt_btqual_restore();
	CHECK(g_tai.releases == 1, "restore twice");
}

static void test_loader_immediates(void)
{
	/* the loader fills the two address loads in: whatever it wrote, the site is the same site */
	static const unsigned imm[][2] = { { 0, 0 }, { 0xFFFF, 0xFFFF }, { 0x1234, 0x8105 }, { 0x0801, 0x0FFE }, { 0xA5A5, 0x5A5A } };

	for (unsigned i = 0; i < sizeof(imm) / sizeof(imm[0]); i++) {
		reset();
		cfg_site(imm[i][0], imm[i][1]);
		btt_btqual_sync(BTT_Q_STABLE);
		CHECK(g_tai.calls == 1, "immediates %x %x refused", imm[i][0], imm[i][1]);
	}
}

static void test_refusals(void)
{
	/* other bytes, a missing module or address: nothing is hooked, and it is not tried again every pass */
	for (int what = 0; what < 9; what++) {
		reset();
		if (what == 0) g_text_mem[OFF_CONFIG] ^= 1;               /* the push */
		if (what == 1) g_text_mem[OFF_CONFIG + 3] ^= 0x80;
		if (what == 2) g_text_mem[OFF_CONFIG + 4] ^= 0x80;        /* the movw is not a movw */
		if (what == 3) g_text_mem[OFF_CONFIG + 7] ^= 0x01;        /* ... not into r7 */
		if (what == 4) g_text_mem[OFF_CONFIG + 9] ^= 0x01;        /* the movt is not a movt */
		if (what == 5) g_text_mem[OFF_CFG_TAIL] ^= 1;
		if (what == 6) g_text_mem[OFF_CFG_TAIL + 13] ^= 1;
		if (what == 7) g_have_module = 0;
		if (what == 8) g_offset_ok = 0;
		for (int i = 0; i < 5; i++)
			btt_btqual_sync(BTT_Q_STABLE);
		CHECK(g_tai.calls == 0 && btqual_cap == 0 && g_lookups == 1, "refusal %d: hooks %d cap %u lookups %d", what, g_tai.calls, (unsigned)btqual_cap, g_lookups);
		btt_btqual_restore();
		CHECK(g_tai.releases == 0, "refusal %d released something", what);
	}

	/* a refused install is tried again only after the level has gone to Standard and back */
	reset();
	g_text_mem[OFF_CONFIG] ^= 1;
	btt_btqual_sync(BTT_Q_STABLE);
	btt_btqual_sync(BTT_Q_BALANCED);
	CHECK(g_tai.calls == 0 && g_lookups == 1, "tried again at once");
	cfg_site(0x1234, 0x8101);
	btt_btqual_sync(BTT_Q_BALANCED);
	CHECK(g_tai.calls == 0, "tried again without going to Standard");
	btt_btqual_sync(BTT_Q_STANDARD);
	btt_btqual_sync(BTT_Q_BALANCED);
	CHECK(g_tai.calls == 1 && btqual_cap == 45, "not tried again after Standard");

	/* taiHEN refuses the hook */
	reset();
	g_tai.fail = 1;
	for (int i = 0; i < 4; i++)
		btt_btqual_sync(BTT_Q_STABLE);
	CHECK(g_tai.calls == 1 && btqual_cap == 0 && g_qual_uid < 0 && btqual_ref == 0, "a refused hook: calls %d cap %u", g_tai.calls, (unsigned)btqual_cap);
	g_tai.fail = 0;
	btt_btqual_sync(BTT_Q_STANDARD);
	btt_btqual_sync(BTT_Q_STABLE);
	CHECK(g_tai.calls == 2 && btqual_cap == 37, "not retried after Standard");
	btt_btqual_restore();
	CHECK(g_tai.releases == 1, "released %d", g_tai.releases);
}

int main(void)
{
	static const struct { const char *name; void (*fn)(void); } tests[] = {
		{ "clamp: every level, every range, the edges", test_clamp },
		{ "hook: only the connection's highest bitpool changes", test_hook_behaviour },
		{ "default is stock: Standard hooks nothing, writes nothing", test_default_is_stock },
		{ "installer: one hook, the caps, Standard, stop", test_installer },
		{ "the address loads may hold any immediates", test_loader_immediates },
		{ "refusals: other bytes, no module, a refused hook", test_refusals },
	};
	int n = (int)(sizeof(tests) / sizeof(tests[0]));

	for (int i = 0; i < n; i++) {
		tests[i].fn();
		printf("ok   %s\n", tests[i].name);
	}
	printf("test_btqual: %d tests, 0 failed\n", n);
	return 0;
}
