/*
 * test_bthq.c - host test of Bluetooth audio quality, High and Highest (kernel/bthq.c, and their part of the connection
 * quality hook, kernel/btqual_hook.c): compiled as they are, with taiHEN's calls replaced by fakes that really patch and
 * unpatch a stand-in for the Bluetooth code's text.
 *
 *   make test      (cc -Itest/host test/test_bthq.c, then run)
 *
 * What is proved:
 *   - the bytes: High writes movw r1,#0x2C18 (2-DH5 allowed) at the packet-type site and movw r3,#0x295 (661) at the
 *     2 Mb/s cap site and installs the hook; Highest writes 0x0C18 (2-DH5 and 3-DH5) and installs the hook, no cap change;
 *     nothing else changes (compared whole); Stable, Balanced and Standard write nothing and do not even look at the system;
 *   - a change of level takes what the new one does not need out first, and any other level puts the stock code back byte
 *     for byte (the hook, if in, stays and does nothing: `btqual_hq` is 0);
 *   - nothing is written unless both windows are the stock code, every byte tried; the refusal is not retried every
 *     pass, but is after the level changes; a refused write or a refused hook leaves nothing half done;
 *   - module stop takes everything out;
 *   - the hook's offer step: only the SBC media codec element of the capability list is read (a transport element or
 *     other categories in front of it are walked over, by their lengths), joint and stereo are taken out when
 *     dual channel is offered and nothing else changes, lists with no dual offer, no SBC element or stopping short are
 *     left as they are, and nothing past `len` is ever read or written;
 *   - the clamp: with the connection left in dual channel mode the highest bitpool is capped at 38 (High) or 59
 *     (Highest), never raised, never below the lowest; any other channel mode and every other level are as Stable and
 *     Balanced alone; the original is called with every argument as it came.
 */
#define BTT_HOST_TAI    1
#define BTT_HOST_INJECT 1
#include <btt_host.h>
#include <taihen.h>
#include <stdlib.h>

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

#define TEXT_SIZE 0x20000
static uint8_t g_text_mem[TEXT_SIZE];
static uint8_t g_pristine[TEXT_SIZE];
static int g_have_module = 1, g_lookups, g_fail_inject_at, g_injects, g_releases;
static struct { int used; uint32_t off; uint8_t orig[4]; } g_t_inj[8];

int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr);
int taiGetModuleInfoForKernel(SceUID pid, const char *name, tai_module_info_t *info)
{
	g_lookups++;
	CHECK(!strcmp(name, "SceBt"), "wrong module looked up: %s", name);
	if (!g_have_module)
		return -1;
	info->modid = 0x777;
	return 0;
}
int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr)
{
	CHECK(modid == 0x777 && segidx == 0 && offset == 0, "wrong segment asked for");
	*addr = (uintptr_t)g_text_mem;
	return 0;
}
SceUID taiInjectDataForKernel(SceUID pid, SceUID modid, int segidx, uint32_t offset, const void *data, size_t size)
{
	CHECK(size == 4 && (offset == 0x7050 || offset == 0x7566), "an inject of %d bytes at 0x%x", (int)size, (unsigned)offset);
	if (g_fail_inject_at && (uint32_t)g_fail_inject_at == offset)
		return -1;
	int i;
	for (i = 0; i < 8 && g_t_inj[i].used; i++) ;
	CHECK(i < 8, "too many injections");
	g_t_inj[i].used = 1;
	g_t_inj[i].off = offset;
	memcpy(g_t_inj[i].orig, g_text_mem + offset, 4);
	memcpy(g_text_mem + offset, data, 4);
	g_injects++;
	return 0x100 + i;
}
int taiInjectReleaseForKernel(SceUID uid)
{
	int i = uid - 0x100;
	CHECK(i >= 0 && i < 8 && g_t_inj[i].used, "released something not injected");
	memcpy(g_text_mem + g_t_inj[i].off, g_t_inj[i].orig, 4);
	g_t_inj[i].used = 0;
	g_releases++;
	return 0;
}

/* a connection, and the routine that applies a configuration: it records its arguments, leaves the channel mode
 * the test sets and stores the headphones' range */
#define CONN_SIZE 0x4000
static uint8_t g_conn[CONN_SIZE];
static struct { int calls; void *conn; uint32_t flags; const void *caps; uint32_t len; uint8_t seen[16]; } g_orig;
static uint32_t g_range[2] = { 2, 53 };
static int fake_cfg(void *conn, uint32_t flags, const void *caps, uint32_t len)
{
	g_orig.calls++; g_orig.conn = conn; g_orig.flags = flags; g_orig.caps = caps; g_orig.len = len;
	if (caps)
		memcpy(g_orig.seen, caps, len < 16 ? len : 16);
	if (conn && (flags & 8)) {
		((uint32_t *)((uint8_t *)conn + 0x3BB8))[0] = g_range[0];
		((uint32_t *)((uint8_t *)conn + 0x3BB8))[1] = g_range[1];
	}
	return 7;
}
static int g_hooks, g_hook_fail, g_hook_releases;
SceUID taiHookFunctionOffsetForKernel(SceUID pid, tai_hook_ref_t *ref, SceUID modid, int segidx,
                                      uint32_t offset, int thumb, const void *hook)
{
	CHECK(offset == 0x732C && thumb == 1, "hooked 0x%x", (unsigned)offset);
	g_hooks++;
	if (g_hook_fail)
		return -1;
	*ref = (tai_hook_ref_t)fake_cfg;
	return 0x1000;
}
int taiHookReleaseForKernel(SceUID uid, tai_hook_ref_t ref) { g_hook_releases++; return 0; }
int ksceKernelDelayThread(SceUInt delay) { return 0; }
void btt_klog_line(const char *line)
{
	if (getenv("BTT_TEST_VERBOSE"))
		printf("    %s\n", line);
}

#include "../kernel/btqual_hook.c"
#include "../kernel/btqual.c"
#include "../kernel/bthq.c"

static void reset(void)
{
	memset(g_text_mem, 0x5A, sizeof(g_text_mem));
	memcpy(g_text_mem + OFF_CONFIG, k_cfg_head, sizeof(k_cfg_head));
	memcpy(g_text_mem + OFF_CFG_TAIL, k_cfg_tail, sizeof(k_cfg_tail));
	memcpy(g_text_mem + WIN_PTYPE, k_ptype_win, sizeof(k_ptype_win));
	memcpy(g_text_mem + WIN_CAP2M, k_cap2m_win, sizeof(k_cap2m_win));
	memcpy(g_pristine, g_text_mem, sizeof(g_text_mem));
	memset(g_t_inj, 0, sizeof(g_t_inj));
	memset(g_conn, 0xAA, sizeof(g_conn));
	memset(&g_orig, 0, sizeof(g_orig));
	g_range[0] = 2; g_range[1] = 53;
	g_have_module = 1; g_lookups = g_fail_inject_at = g_injects = g_releases = g_hooks = g_hook_fail = g_hook_releases = 0;
	gh_mod = -1; gh_text = 0; gh_state = -1; gh_inj_ptype = gh_inj_cap = -1; gh_mode = 0; gh_failed = 0;
	g_qual_uid = -1; g_qual_failed = 0; btqual_cap = 0; btqual_ref = 0; btqual_in = 0; btqual_hq = 0;
}

static int stock(void) { return !memcmp(g_text_mem, g_pristine, sizeof(g_text_mem)); }
static int has(uint32_t off, const uint8_t *b) { return !memcmp(g_text_mem + off, b, 4); }

static void test_bytes(void)
{
	/* the three replacement constants as the instruction encodings (movw: imm4 : i : imm3 : imm8) */
	CHECK(!memcmp(k_ptype_2dh5, "\x42\xF6\x18\x41", 4) && !memcmp(k_ptype_5slot, "\x40\xF6\x18\x41", 4), "packet type constants");
	CHECK(!memcmp(k_cap2m_raised, "\x40\xF2\x95\x23", 4), "cap constant");
	reset();
	/* the levels that are not bthq.c's: nothing is looked at */
	for (int i = 0; i < 20; i++) {
		btt_bthq_sync(BTT_Q_STANDARD);
		btt_bthq_sync(BTT_Q_STABLE);
		btt_bthq_sync(BTT_Q_BALANCED);
		btt_bthq_sync(-1);
		btt_bthq_sync(BTT_Q_COUNT);
	}
	CHECK(g_lookups == 0 && g_injects == 0 && g_hooks == 0 && stock(), "a level that is not High or Highest touched the system");

	/* High: both injects and the hook */
	for (int i = 0; i < 10; i++)
		btt_bthq_sync(BTT_Q_HIGH);
	CHECK(g_injects == 2 && g_hooks == 1 && has(0x7050, k_ptype_2dh5) && has(0x7566, k_cap2m_raised) && btqual_hq == BTHQ_HIGH &&
	      g_qual_uid >= 0, "High");
	for (int i = 0; i < TEXT_SIZE; i++)
		if (g_text_mem[i] != g_pristine[i])
			CHECK((i >= 0x7050 && i < 0x7054) || (i >= 0x7566 && i < 0x756A), "a byte outside the sites changed at 0x%x", i);
	/* Highest: the cap goes back, 5-slot types */
	btt_bthq_sync(BTT_Q_HIGHEST);
	CHECK(has(0x7050, k_ptype_5slot) && !has(0x7566, k_cap2m_raised) && !memcmp(g_text_mem + 0x7566, g_pristine + 0x7566, 4) &&
	      g_hooks == 1 && btqual_hq == BTHQ_HIGHEST, "Highest");
	for (int i = 0; i < TEXT_SIZE; i++)
		if (g_text_mem[i] != g_pristine[i])
			CHECK(i >= 0x7050 && i < 0x7054, "Highest changed a byte outside the packet types at 0x%x", i);
	/* every other level: stock again, the hook stays and does nothing */
	static const int others[] = { BTT_Q_STANDARD, BTT_Q_STABLE, BTT_Q_BALANCED };
	for (unsigned k = 0; k < sizeof(others) / sizeof(others[0]); k++) {
		btt_bthq_sync(BTT_Q_HIGHEST);
		btt_bthq_sync(others[k]);
		CHECK(stock() && btqual_hq == 0 && g_qual_uid >= 0 && g_hook_releases == 0, "level %d did not restore", others[k]);
	}
	/* High to Highest and back: whatever the other does not need is out first */
	btt_bthq_sync(BTT_Q_HIGH);
	btt_bthq_sync(BTT_Q_HIGHEST);
	btt_bthq_sync(BTT_Q_HIGH);
	CHECK(has(0x7050, k_ptype_2dh5) && has(0x7566, k_cap2m_raised) && btqual_hq == BTHQ_HIGH, "Highest to High");
	btt_bthq_restore();
	CHECK(stock() && btqual_hq == 0, "stop left something");
	btt_bthq_restore();
	for (int i = 0; i < 8; i++)
		CHECK(!g_t_inj[i].used, "an injection is still in");
}

static void test_windows(void)
{
	for (int site = 0; site < 2; site++) {
		uint32_t win = site ? WIN_CAP2M : WIN_PTYPE;

		for (int i = 0; i < 16; i++) {
			reset();
			g_text_mem[win + i] ^= 1;
			memcpy(g_pristine, g_text_mem, sizeof(g_text_mem));
			for (int k = 0; k < 10; k++)
				btt_bthq_sync(BTT_Q_HIGHEST);
			CHECK(g_injects == 0 && g_hooks == 0 && stock() && btqual_hq == 0, "window %d byte %d differs and something was done", site, i);
			CHECK(g_lookups == 1, "window %d byte %d: looked at %d times", site, i, g_lookups);
			btt_bthq_sync(BTT_Q_HIGH);
			CHECK(g_injects == 0, "another level tried on code that is not the stock code");
		}
	}
}

static void test_failures(void)
{
	/* the hook refused: nothing half done */
	reset();
	g_hook_fail = 1;
	for (int i = 0; i < 10; i++)
		btt_bthq_sync(BTT_Q_HIGH);
	CHECK(stock() && btqual_hq == 0 && g_hooks == 1, "refused hook: %d hooks", g_hooks);
	g_hook_fail = 0;
	btt_bthq_sync(BTT_Q_HIGH);
	CHECK(g_hooks == 1, "retried without a change of level");
	btt_bthq_sync(BTT_Q_HIGHEST);
	CHECK(g_hooks == 2 && btqual_hq == BTHQ_HIGHEST && has(0x7050, k_ptype_5slot), "not tried at another level");
	/* the refusal is forgotten once the level goes to one that is not bthq.c's */
	reset();
	g_hook_fail = 1;
	btt_bthq_sync(BTT_Q_HIGH);
	btt_bthq_sync(BTT_Q_STANDARD);
	g_hook_fail = 0;
	btt_bthq_sync(BTT_Q_HIGH);
	CHECK(btqual_hq == BTHQ_HIGH && has(0x7050, k_ptype_2dh5), "not tried again after Standard");
	/* the cap refused at High: the packet types go back too, and no hook goes in */
	reset();
	g_fail_inject_at = 0x7566;
	for (int i = 0; i < 10; i++)
		btt_bthq_sync(BTT_Q_HIGH);
	CHECK(stock() && g_injects == 1 && g_releases == 1 && g_hooks == 0, "half of High left: %d injects, %d releases", g_injects, g_releases);
	/* the packet types refused */
	reset();
	g_fail_inject_at = 0x7050;
	btt_bthq_sync(BTT_Q_HIGHEST);
	CHECK(stock() && g_hooks == 0, "packet types refused but a hook went in");
	/* no module */
	reset();
	g_have_module = 0;
	for (int i = 0; i < 10; i++)
		btt_bthq_sync(BTT_Q_HIGHEST);
	CHECK(g_injects == 0 && g_lookups == 1, "no module");
}

/* ---- the hook's parts -------------------------------------------------------------------------- */

#define CANARY 0xC5

/* caps in a buffer with canaries on both sides: nothing outside [pad, pad + len) may be read as data or changed */
static uint8_t g_buf[64];
static uint8_t *caps_set(const uint8_t *c, int n)
{
	memset(g_buf, CANARY, sizeof(g_buf));
	memcpy(g_buf + 16, c, (size_t)n);
	return g_buf + 16;
}
static int canaries_ok(int n)
{
	for (int i = 0; i < 16; i++)
		if (g_buf[i] != CANARY)
			return 0;
	for (int i = 16 + n; i < (int)sizeof(g_buf); i++)
		if (g_buf[i] != CANARY)
			return 0;
	return 1;
}

static void test_offer(void)
{
	/* a transport element (category 1, length 0), a content protection element (4, length 2), then the codec element */
	static const uint8_t full[] = { 0x01, 0x00, 0x04, 0x02, 0x02, 0x00, 0x07, 0x06, 0x00, 0x00, 0x3F, 0xFF, 0x02, 0x35 };
	uint8_t *p;

	p = caps_set(full, sizeof(full));
	CHECK(hq_prefer_dual(p, sizeof(full)) == 1 && p[10] == 0x3C && !memcmp(p, full, 10) && !memcmp(p + 11, full + 11, 3) && canaries_ok(sizeof(full)),
	      "joint+stereo+dual+mono: %02x", p[10]);
	/* dual not offered: left alone */
	uint8_t nodual[sizeof(full)];
	memcpy(nodual, full, sizeof(full)); nodual[10] = 0x3B;
	p = caps_set(nodual, sizeof(nodual));
	CHECK(hq_prefer_dual(p, sizeof(nodual)) == 0 && !memcmp(p, nodual, sizeof(nodual)) && canaries_ok(sizeof(nodual)), "no dual offered");
	/* only dual */
	nodual[10] = 0x24;
	p = caps_set(nodual, sizeof(nodual));
	CHECK(hq_prefer_dual(p, sizeof(nodual)) == 1 && p[10] == 0x24, "dual only");
	/* the codec is not SBC (type 2): not touched */
	uint8_t other[sizeof(full)];
	memcpy(other, full, sizeof(full)); other[9] = 0x02;
	p = caps_set(other, sizeof(other));
	CHECK(hq_prefer_dual(p, sizeof(other)) == -1 && !memcmp(p, other, sizeof(other)), "another codec");
	/* every shorter length: never reads past it, never writes past it, and only a whole element counts */
	for (int n = 0; n <= (int)sizeof(full); n++) {
		p = caps_set(full, n);
		int r = hq_prefer_dual(p, (uint32_t)n);
		CHECK(canaries_ok(n), "length %d: wrote outside the list", n);
		CHECK(r == (n == (int)sizeof(full) ? 1 : -1), "length %d: answer %d", n, r);
		if (n < (int)sizeof(full))
			CHECK(!memcmp(p, full, (size_t)n), "length %d: the list changed", n);
	}
	/* an element whose length runs past the end */
	static const uint8_t runs[] = { 0x07, 0x7F, 0x00, 0x00, 0x3F, 0xFF, 0x02, 0x35 };
	p = caps_set(runs, sizeof(runs));
	CHECK(hq_prefer_dual(p, sizeof(runs)) == -1 && !memcmp(p, runs, sizeof(runs)) && canaries_ok(sizeof(runs)), "a length past the end");
	/* an empty list and a one-byte list */
	p = caps_set(runs, 1);
	CHECK(hq_prefer_dual(p, 0) == -1 && hq_prefer_dual(p, 1) == -1 && canaries_ok(1), "empty and one byte");
	/* a codec element shorter than SBC's six bytes */
	static const uint8_t shortel[] = { 0x07, 0x03, 0x00, 0x00, 0x3F };
	p = caps_set(shortel, sizeof(shortel));
	CHECK(hq_prefer_dual(p, sizeof(shortel)) == -1 && !memcmp(p, shortel, sizeof(shortel)), "a short codec element");
}

static void set_mode(uint32_t m) { memcpy(g_conn + 0x3BC4, &m, 4); }

static int apply(uint32_t flags, uint8_t *caps, uint32_t len)
{
	return btqual_hook(g_conn, flags, caps, len);
}

static void test_clamp(void)
{
	static const uint8_t cl[] = { 0x07, 0x06, 0x00, 0x00, 0x3F, 0xFF, 0x02, 0x35 };
	uint8_t *p;
	uint32_t *bp = (uint32_t *)(g_conn + 0x3BB8);

	reset();
	btt_bthq_sync(BTT_Q_HIGHEST);
	/* dual: capped at 59, never raised, the original called with the offer changed and everything else as it came */
	set_mode(1); g_range[1] = 64;
	p = caps_set(cl, sizeof(cl));
	CHECK(apply(8, p, sizeof(cl)) == 7, "the original's answer");
	CHECK(g_orig.calls == 1 && g_orig.conn == g_conn && g_orig.flags == 8 && g_orig.caps == p && g_orig.len == sizeof(cl) &&
	      g_orig.seen[4] == 0x3C, "the original saw the changed offer (%02x) or other arguments", g_orig.seen[4]);
	CHECK(bp[1] == 59 && bp[0] == 2, "Highest, dual, highest 64: %u", bp[1]);
	g_range[1] = 53;
	apply(8, caps_set(cl, sizeof(cl)), sizeof(cl));
	CHECK(bp[1] == 53, "never raised: %u", bp[1]);
	/* a range whose lowest is above the cap: the lowest wins (the encoder's range stays valid) */
	g_range[0] = 61; g_range[1] = 64;
	apply(8, caps_set(cl, sizeof(cl)), sizeof(cl));
	CHECK(bp[1] == 61, "lowest above the cap: %u", bp[1]);
	g_range[0] = 2;
	/* any other channel mode: untouched */
	for (int m = 0; m <= 3; m++) {
		if (m == 1)
			continue;
		set_mode((uint32_t)m); g_range[1] = 64;
		apply(8, caps_set(cl, sizeof(cl)), sizeof(cl));
		CHECK(bp[1] == 64, "channel mode %d lowered to %u", m, bp[1]);
	}
	/* High: 38; and below a cap of Stable or Balanced too (not reachable through the setting, which holds one level) */
	btt_bthq_sync(BTT_Q_HIGH);
	set_mode(1); g_range[1] = 53;
	apply(8, caps_set(cl, sizeof(cl)), sizeof(cl));
	CHECK(bp[1] == 38, "High: %u", bp[1]);
	btt_btqual_sync(BTT_Q_STABLE);   /* 37 */
	apply(8, caps_set(cl, sizeof(cl)), sizeof(cl));
	CHECK(bp[1] == 37, "High and a cap of 37: %u", bp[1]);
	btt_btqual_sync(BTT_Q_STANDARD);
	/* a call that only reads capabilities (no flag 8), and no capability list: nothing of the High and Highest changes */
	p = caps_set(cl, sizeof(cl));
	g_range[1] = 53;
	bp[1] = 99;
	apply(4, p, sizeof(cl));
	CHECK(p[4] == 0x3F && bp[1] == 99, "a read-only call changed something");
	apply(8, 0, 0);
	CHECK(bp[1] == 38, "no list: the cap still applies, %u", bp[1]);
	/* Standard: joint stereo, the offer untouched, no cap */
	btt_bthq_sync(BTT_Q_STANDARD);
	set_mode(1); g_range[1] = 53;
	p = caps_set(cl, sizeof(cl));
	apply(8, p, sizeof(cl));
	CHECK(p[4] == 0x3F && bp[1] == 53, "Standard changed the offer or the bitpool: %02x %u", p[4], bp[1]);
	btt_bthq_restore();
	btt_btqual_restore();
}

int main(void)
{
	test_bytes();
	test_windows();
	test_failures();
	test_offer();
	test_clamp();
	printf("test_bthq: ok\n");
	return 0;
}
