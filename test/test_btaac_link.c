/*
 * test_btaac_link.c - host test of the main module's side of the optional AAC module (kernel/btaac_link.c, btaac_link_leaf.c), compiled as
 * they are, with the lookup of the AAC module's export and its table replaced by fakes.
 *
 *   make test      (cc -Itest/host test/test_btaac_link.c, then run)
 *
 * What is proved:
 *   - without the AAC module (the normal case): nothing is found, bttGet(BTT_KEY_AAC_AVAIL)'s value is 0 (the Settings page shows no AAC row),
 *     the leaves take and give nothing, the codec sent is SBC whatever the key says, a Codec key of AAC is logged once and only then, and the
 *     lookup is made once, by the module's name, any library, and the number the build gave the export;
 *   - with it: the availability bits follow the self-test's verdict exactly (module present only; plus the C build usable; plus NEON allowed
 *     on a pass only; plus held off by the crash record), so the Hardware Accelerated AAC row appears only after a pass; AAC is honoured only
 *     while the module is there, tested and not held off; the self-test is asked for on every late pass with the master switch; the leaves
 *     reach the table's functions with their arguments and answer with theirs;
 *   - a table that is not as expected (no table, too small, another version, a lookup that fails or gives no function) is refused and logged:
 *     the module is treated as absent, and a table larger than needed (a later build) is accepted.
 * Each scenario runs in its own process: the link keeps its state in statics.
 */
#include <btt_host.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef TAI_ANY_LIBRARY
#define TAI_ANY_LIBRARY 0xFFFFFFFFu
#endif

#include "../kernel/btaac_link.c"
#include "../kernel/btaac_link_leaf.c"

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

static char g_log[4096];
void btt_klog_line(const char *line)
{
	if (strlen(g_log) + strlen(line) + 2 < sizeof(g_log)) {
		strcat(g_log, line);
		strcat(g_log, "\n");
	}
}
static int log_count(const char *needle)
{
	int n = 0;

	for (const char *p = g_log; (p = strstr(p, needle)); p += strlen(needle))
		n++;
	return n;
}

/* ---- the fake AAC module ------------------------------------------------------------------------------------ */

static BtAacInfo g_info;
static int g_syncs, g_last_sync_on, g_starts;
static uint32_t g_push_ret = 77, g_pop_ret = 88, g_last_frames, g_last_cap;
static const int16_t *g_last_pcm;
static uint8_t *g_last_out;
static uint32_t *g_last_seq;

static void f_get_info(BtAacInfo *o) { *o = g_info; }
static void f_sync(int on) { g_syncs++; g_last_sync_on = on; }
static int f_start(uint32_t a, uint32_t b, uint32_t c, uint32_t d, int e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; g_starts++; return 0; }
static void f_stop(void) {}
static uint32_t f_push(const int16_t *pcm, uint32_t frames) { g_last_pcm = pcm; g_last_frames = frames; return g_push_ret; }
static uint32_t f_pop(uint8_t *out, uint32_t cap, uint32_t *seq) { g_last_out = out; g_last_cap = cap; g_last_seq = seq; return g_pop_ret; }
static uint32_t g_trim_keep = 99;
static uint32_t f_trim(uint32_t keep) { g_trim_keep = keep; return keep + 1u; }
static uint32_t g_depth_ret;
static uint32_t f_depth(void) { return g_depth_ret; }

static BtAacApi g_table;
static const BtAacApi *g_table_ptr;
static int g_lookups, g_lookup_ret, g_lookup_null;
static const BtAacApi *get_api(void) { return g_table_ptr; }

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func)
{
	g_lookups++;
	CHECK(pid == KERNEL_PID && !strcmp(modname, "bluetoothturbo_aac") && libnid == TAI_ANY_LIBRARY && funcnid == BTAAC_NID_GET_API,
	      "the lookup: %s 0x%x 0x%x", modname, libnid, funcnid);
	if (g_lookup_ret < 0)
		return g_lookup_ret;
	*func = g_lookup_null ? 0 : (uintptr_t)get_api;
	return 0;
}

static void table_good(void)
{
	memset(&g_table, 0, sizeof(g_table));
	g_table.size = sizeof(BtAacApi);
	g_table.version = BTAAC_API_VERSION;
	g_table.start = f_start;
	g_table.stop = f_stop;
	g_table.sync = f_sync;
	g_table.get_info = f_get_info;
	g_table.push_pcm = f_push;
	g_table.pop_au = f_pop;
	g_table.au_trim = f_trim;
	g_table.au_depth = f_depth;
	g_table_ptr = &g_table;
	memset(&g_info, 0, sizeof(g_info));
}

static void check_leaves_idle(void)
{
	int16_t pcm[4] = { 1, 2, 3, 4 };
	uint8_t out[8];
	uint32_t seq = 5;

	CHECK(btt_aac_push_pcm(pcm, 2) == 0 && btt_aac_pop_au(out, sizeof(out), &seq) == 0 && seq == 5, "no module: the leaves take and give nothing");
	CHECK(btt_aac_push_pcm(NULL, 0) == 0 && btt_aac_pop_au(NULL, 0, NULL) == 0, "and take no arguments seriously");
	CHECK(btt_aac_au_trim(3) == 0 && g_trim_keep == 99, "no module: nothing is trimmed");
	CHECK(btt_aac_au_depth() == 0, "no module: nothing waits");
}

/* ---- the scenarios ------------------------------------------------------------------------------------------- */

static void t_absent(void)
{
	CHECK(btt_aac_avail() == 0 && btt_aac_codec_effective(BTT_CODEC_AAC) == BTT_CODEC_SBC, "before the lookup: nothing offered");
	check_leaves_idle();
	g_lookup_ret = -1;
	btt_aac_link_find();
	btt_aac_link_find();
	btt_aac_link_find();
	CHECK(g_lookups == 1, "the lookup is made once (%d)", g_lookups);
	CHECK(log_count("the AAC module is not loaded") == 1, "logged once:\n%s", g_log);
	CHECK(btt_aac_avail() == 0, "the availability is 0: no AAC row on the Settings page");
	CHECK(btt_aac_codec_effective(BTT_CODEC_AAC) == BTT_CODEC_SBC && btt_aac_codec_effective(BTT_CODEC_SBC) == BTT_CODEC_SBC, "SBC is sent whatever the key says");
	check_leaves_idle();
	/* a Codec key of SBC says nothing; AAC says it once */
	btt_aac_link_sync(1, BTT_CODEC_SBC);
	CHECK(log_count("Bluetooth Codec is AAC") == 0, "SBC selected: nothing said");
	btt_aac_link_sync(1, BTT_CODEC_AAC);
	btt_aac_link_sync(1, BTT_CODEC_AAC);
	btt_aac_link_sync(0, BTT_CODEC_AAC);
	CHECK(log_count("the Bluetooth Codec is AAC but the AAC module is not loaded: SBC is sent") == 1, "AAC selected: said once:\n%s", g_log);
}

static void t_absent_no_function(void)
{
	g_lookup_null = 1;                     /* found, but no function behind the number */
	btt_aac_link_find();
	CHECK(btt_aac_avail() == 0 && log_count("not loaded") == 1, "no function: treated as absent");
}

static void t_found_verdicts(void)
{
	static const struct { int verdict, neon, hold, want, codec_aac; } k[] = {
		{ BTAAC_ST_NOT_RUN,     0, 0, BTT_AAC_AVAIL_MODULE, 0 },
		{ BTAAC_ST_PASS,        1, 0, BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_TESTED | BTT_AAC_AVAIL_NEON, 1 },
		{ BTAAC_ST_NEON_DIFF,   0, 0, BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_TESTED, 1 },
		{ BTAAC_ST_NEON_BANNED, 0, 0, BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_TESTED, 1 },
		{ BTAAC_ST_C_FAIL,      0, 0, BTT_AAC_AVAIL_MODULE, 0 },
		{ BTAAC_ST_ERROR,       0, 0, BTT_AAC_AVAIL_MODULE, 0 },
		{ BTAAC_ST_PASS,        1, 1, BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_TESTED | BTT_AAC_AVAIL_NEON | BTT_AAC_AVAIL_HELD, 0 },
		{ BTAAC_ST_NEON_DIFF,   0, 1, BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_TESTED | BTT_AAC_AVAIL_HELD, 0 },
		{ BTAAC_ST_NOT_RUN,     0, 1, BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_HELD, 0 },
	};

	table_good();
	CHECK(btt_aac_avail() == 0, "found only when looked for");
	btt_aac_link_find();
	CHECK(g_lookups == 1 && btaac_link_api == &g_table && log_count("the AAC module is loaded (interface version 4)") == 1, "found:\n%s", g_log);
	for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
		g_info.selftest = (uint32_t)k[i].verdict;
		g_info.neon_allowed = (uint32_t)k[i].neon;
		g_info.hold = (uint32_t)k[i].hold;
		CHECK(btt_aac_avail() == k[i].want, "case %u: bits %d, expected %d", i, btt_aac_avail(), k[i].want);
		CHECK(btt_aac_codec_effective(BTT_CODEC_AAC) == (k[i].codec_aac ? BTT_CODEC_AAC : BTT_CODEC_SBC), "case %u: the codec sent", i);
		CHECK(btt_aac_codec_effective(BTT_CODEC_SBC) == BTT_CODEC_SBC, "case %u: SBC stays SBC", i);
		/* the Hardware Accelerated AAC row is offered after a pass only: its bit is the NEON one */
		CHECK(((btt_aac_avail() & BTT_AAC_AVAIL_NEON) != 0) == (k[i].verdict == BTAAC_ST_PASS), "case %u: the NEON bit follows a pass", i);
	}
	/* the self-test is asked for on every late pass, with the master switch; a codec of AAC with the module there says nothing */
	btt_aac_link_sync(1, BTT_CODEC_AAC);
	CHECK(g_syncs == 1 && g_last_sync_on == 1, "sync passed on");
	btt_aac_link_sync(0, BTT_CODEC_SBC);
	CHECK(g_syncs == 2 && g_last_sync_on == 0, "and the master switch off");
	CHECK(log_count("not loaded") == 0, "nothing logged as missing");
}

static void t_leaves_through_table(void)
{
	int16_t pcm[8] = { 0 };
	uint8_t out[64];
	uint32_t seq = 0;

	table_good();
	btt_aac_link_find();
	CHECK(btt_aac_push_pcm(pcm, 512) == 77 && g_last_pcm == pcm && g_last_frames == 512, "push reaches the table");
	CHECK(btt_aac_pop_au(out, sizeof(out), &seq) == 88 && g_last_out == out && g_last_cap == sizeof(out) && g_last_seq == &seq, "pop reaches the table");
	CHECK(btt_aac_au_trim(3) == 4 && g_trim_keep == 3, "trim reaches the table with its argument and brings its answer back");
	g_depth_ret = 5;
	CHECK(btt_aac_au_depth() == 5, "the depth reaches the table and its answer comes back");
	g_push_ret = 0;
	g_pop_ret = 0;
	CHECK(btt_aac_push_pcm(pcm, 512) == 0 && btt_aac_pop_au(out, sizeof(out), NULL) == 0, "and brings their answers back");
}

static void refused(void)
{
	btt_aac_link_find();
	CHECK(btaac_link_api == NULL && btt_aac_avail() == 0 && btt_aac_codec_effective(BTT_CODEC_AAC) == BTT_CODEC_SBC, "refused: treated as absent");
	check_leaves_idle();
	btt_aac_link_sync(1, BTT_CODEC_AAC);
	CHECK(g_syncs == 0, "and never asked for anything");
}
static void t_table_null(void) { table_good(); g_table_ptr = NULL; refused(); CHECK(log_count("does not match this build (table size 0 version 0") == 1, "logged:\n%s", g_log); }
static void t_table_small(void) { table_good(); g_table.size = sizeof(BtAacApi) - 4; refused(); CHECK(log_count("does not match this build") == 1, "logged:\n%s", g_log); }
static void t_table_version(void) { table_good(); g_table.version = BTAAC_API_VERSION + 1; refused(); CHECK(log_count("does not match this build") == 1, "logged:\n%s", g_log); }
static void t_table_larger(void)
{
	table_good();
	g_table.size = sizeof(BtAacApi) + 64;
	btt_aac_link_find();
	CHECK(btaac_link_api == &g_table && btt_aac_avail() == BTT_AAC_AVAIL_MODULE, "a table larger than needed (a later build) is accepted");
}

typedef struct { const char *name; void (*fn)(void); } Scn;
static const Scn k_scn[] = {
	{ "no AAC module: nothing offered, SBC, one log line", t_absent },
	{ "export found but no function", t_absent_no_function },
	{ "AAC module found: availability follows the self-test", t_found_verdicts },
	{ "the leaves reach the table", t_leaves_through_table },
	{ "no table", t_table_null },
	{ "table too small", t_table_small },
	{ "table of another version", t_table_version },
	{ "table larger than needed", t_table_larger },
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
	if (failed)
		return 1;
	printf("test_btaac_link: all %u scenarios passed\n", (unsigned)(sizeof(k_scn) / sizeof(k_scn[0])));
	return 0;
}
