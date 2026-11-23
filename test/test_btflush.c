/*
 * test_btflush.c - host test of the flush timeout and completed-packets guard of Bluetooth
 * latency Lowest (kernel/btflush.c and the hook bodies in kernel/btflush_hook.c), compiled into this file as they are, with taiHEN's calls replaced by
 * fakes, the Bluetooth code's command sender by a recorder and its event handler by another.
 *
 *   make test      (cc -Itest/host test/test_btflush.c, then run)
 *
 * What is proved:
 *   - nothing is looked at or hooked until Lowest is wanted; the four places in the Bluetooth code
 *     (stream start, stream stop, the command sender and the event handler) must hold the stock bytes,
 *     or nothing is hooked, however often it is asked; the event handler's two address-loading
 *     instructions may hold any immediates (the loader fills them in), nothing else may differ;
 *   - the three hooks go in once, at the right offsets as Thumb code, all or none, and stay in when
 *     Lowest is switched off (nothing is released while the module runs);
 *   - a stream start (a start, not a stop; the A2DP mask bit) runs the original and then sends the
 *     timeout command: opcode 0x0C28, the format "22", the link's handle and 80 slots (50 ms), on the
 *     connection's own context; any other start sends nothing;
 *   - the stop of that stream sends the timeout 0 (never discard: the stock behaviour), once, only on
 *     the link it was set on, and also when Lowest was switched off in between;
 *   - with Lowest off a start sends nothing; a hook whose reference is not set yet calls nothing;
 *   - the guard: an event that reports completed packets for one link goes to the original untouched (the
 *     same bytes at the same address); one that names several links is handed to it as one well-formed
 *     one-link event per link, each with its own handle and count, in order; any other event, one that is
 *     not a well-formed list (a wrong length, a count of zero, cut short) and every event while the
 *     guard is off go through untouched;
 *   - a refused hook means no timeout is ever set and no event is split; module stop takes all three out.
 */
#define BTT_HOST_TAI    1
#define BTT_HOST_INJECT 1
#include <btt_host.h>
#include <taihen.h>
#include <stdlib.h>
#include <stdarg.h>

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

static int g_orig_calls;
static int *g_orig_conn;
static int g_orig_flag;
static uint32_t g_orig_mask;
static void fake_start(int *conn, int flag, uint32_t mask)
{
	g_orig_calls++; g_orig_conn = conn; g_orig_flag = flag; g_orig_mask = mask;
}
static void fake_stop(int *conn, int flag, uint32_t mask)
{
	g_orig_calls++; g_orig_conn = conn; g_orig_flag = flag; g_orig_mask = mask;
}

/* the event handler: records every call, with a copy of the bytes it was given */
#define EVT_MAX 64
static struct { int n; uint32_t *ctx[EVT_MAX]; const uint8_t *ptr[EVT_MAX]; int len[EVT_MAX]; uint8_t bytes[EVT_MAX][256]; } g_evt;
static void fake_evt(uint32_t *ctx, const uint8_t *ev, int len)
{
	CHECK(g_evt.n < EVT_MAX, "too many events");
	g_evt.ctx[g_evt.n] = ctx;
	g_evt.ptr[g_evt.n] = ev;
	g_evt.len[g_evt.n] = len;
	memcpy(g_evt.bytes[g_evt.n], ev, len < 256 ? (size_t)len : 256);
	g_evt.n++;
}

static struct { int calls, releases; uint32_t off[4]; int thumb[4]; const void *hook[4]; int fail_nth; } g_tai;
SceUID taiHookFunctionOffsetForKernel(SceUID pid, tai_hook_ref_t *ref, SceUID modid, int segidx,
                                      uint32_t offset, int thumb, const void *hook)
{
	int i = g_tai.calls++;
	CHECK(pid == KERNEL_PID && modid == 0x777 && segidx == 0, "wrong hook target");
	if (i < 4) {
		g_tai.off[i] = offset; g_tai.thumb[i] = thumb; g_tai.hook[i] = hook;
	}
	if (g_tai.fail_nth == i + 1)
		return -1;
	*ref = (tai_hook_ref_t)(offset == 0x6F50 ? (void *)fake_start : offset == 0x71C8 ? (void *)fake_stop : (void *)fake_evt);
	return 0x1000 + i;
}
int taiHookReleaseForKernel(SceUID uid, tai_hook_ref_t ref) { g_tai.releases++; return 0; }
int ksceKernelDelayThread(SceUInt delay) { return 0; }
void btt_klog_line(const char *line)
{
	if (getenv("BTT_TEST_VERBOSE"))
		printf("    %s\n", line);
}

/* the command sender: records every call */
static struct { int n; int ctx[32]; unsigned op[32]; char fmt[32][8]; int a[32], b[32]; } g_sent;
static int fake_sender(int ctx, unsigned op, const char *fmt, ...)
{
	va_list ap;
	CHECK(g_sent.n < 32, "too many commands");
	va_start(ap, fmt);
	g_sent.ctx[g_sent.n] = ctx;
	g_sent.op[g_sent.n] = op;
	snprintf(g_sent.fmt[g_sent.n], 8, "%s", fmt);
	g_sent.a[g_sent.n] = va_arg(ap, int);
	g_sent.b[g_sent.n] = va_arg(ap, int);
	va_end(ap);
	g_sent.n++;
	return 0;
}

uint32_t ksceKernelGetSystemTimeLow(void) { return 123456u; }
#include "../kernel/btevlog_hook.c"
#include "../kernel/btflush_hook.c"
#include "../kernel/btflush.c"

/* ---- helpers --------------------------------------------------------------------------- */

/* the event handler's entry as the loader leaves it: the two address loads (movw r4 / movt r4) hold
 * whatever immediates the loader wrote */
static void evt_site(unsigned lo, unsigned hi)
{
	uint8_t *p = g_text_mem + OFF_EVT;

	memcpy(p, k_evt_head, 12);
	memcpy(g_text_mem + OFF_EVT_TAIL, k_evt_tail, 14);
	p[4] = (uint8_t)(0x40 | ((lo >> 12) & 0xF)); p[5] = (uint8_t)(0xF2 | (((lo >> 11) & 1) << 2));
	p[6] = (uint8_t)(lo & 0xFF);                 p[7] = (uint8_t)(0x04 | (((lo >> 8) & 7) << 4));
	p[8] = (uint8_t)(0xC0 | ((hi >> 12) & 0xF)); p[9] = (uint8_t)(0xF2 | (((hi >> 11) & 1) << 2));
	p[10] = (uint8_t)(hi & 0xFF);                p[11] = (uint8_t)(0x04 | (((hi >> 8) & 7) << 4));
}

static void reset(void)
{
	memset(g_text_mem, 0x5A, sizeof(g_text_mem));
	memcpy(g_text_mem + OFF_START, k_start_stock, 8);
	memcpy(g_text_mem + OFF_STOP, k_stop_stock, 8);
	memcpy(g_text_mem + OFF_SENDER, k_sender_stock, 6);
	evt_site(0x1234, 0x5678);
	memset(&g_tai, 0, sizeof(g_tai));
	memset(&g_sent, 0, sizeof(g_sent));
	memset(&g_evt, 0, sizeof(g_evt));
	g_have_module = g_offset_ok = 1;
	g_lookups = g_orig_calls = 0;
	/* the module's own state, as at module load */
	g_flush_dead = 0; g_evt_dead = 0; g_flush_uid[0] = g_flush_uid[1] = g_flush_uid[2] = -1;
	btflush_slots = btflush_armed = btflush_guard = 0;
	btflush_in_start = btflush_in_stop = btflush_in_evt = 0;
	btflush_start_ref = btflush_stop_ref = btflush_evt_ref = 0;
	btflush_sender = 0;
	memset(&btevlog_ring, 0, sizeof(btevlog_ring));
	btevlog_on = 0;
}

static void on(void)
{
	btt_btflush_sync(1);
	btflush_sender = (void *)fake_sender;   /* the real sender cannot run on a computer */
}

/* a connection as the Bluetooth code lays it out: the context in the first word, the handle at +0x30 */
static void conn_init(int *c, int ctx, int handle)
{
	memset(c, 0, 0x40 * sizeof(int));
	c[0] = ctx;
	c[0xC] = handle;
}

#define MASK_A2DP_START 0x8u

/* ---- tests ----------------------------------------------------------------------------- */

static void test_not_wanted(void)
{
	reset();
	for (int i = 0; i < 5; i++)
		btt_btflush_sync(0);
	CHECK(g_lookups == 0 && g_tai.calls == 0, "looked at or hooked without being wanted");
	btt_btflush_restore();
	CHECK(g_tai.releases == 0, "stop with nothing in released something");
}

static void refuses(const char *what)
{
	for (int i = 0; i < 5; i++)
		btt_btflush_sync(1);
	CHECK(g_tai.calls == 0, "%s: hooked (%d)", what, g_tai.calls);
	CHECK(g_lookups == 1, "%s: looked up %d times", what, g_lookups);
	btt_btflush_restore();
	CHECK(g_tai.releases == 0, "%s: released something", what);
}

static void test_refusals(void)
{
	reset(); g_text_mem[OFF_START + 3] ^= 1; refuses("start bytes differ");
	reset(); g_text_mem[OFF_STOP + 7] ^= 1; refuses("stop bytes differ");
	reset(); g_text_mem[OFF_SENDER] ^= 1; refuses("sender bytes differ");
	reset(); g_text_mem[OFF_EVT] ^= 1; refuses("event handler push differs");
	reset(); g_text_mem[OFF_EVT + 4] ^= 0x80; refuses("event handler: the movw is not a movw");
	reset(); g_text_mem[OFF_EVT + 7] ^= 0x01; refuses("event handler: the movw is not into r4");
	reset(); g_text_mem[OFF_EVT + 9] ^= 0x01; refuses("event handler: the movt is not a movt");
	reset(); g_text_mem[OFF_EVT_TAIL] ^= 1; refuses("event handler tail differs");
	reset(); g_text_mem[OFF_EVT_TAIL + 13] ^= 1; refuses("event handler tail's last byte differs");
	reset(); g_have_module = 0; refuses("module missing");
	reset(); g_offset_ok = 0; refuses("no text address");
}

static void test_loader_immediates(void)
{
	/* the loader fills the two address loads in: whatever it wrote, the site is the same site */
	static const unsigned imm[][2] = { { 0, 0 }, { 0xFFFF, 0xFFFF }, { 0x1234, 0x8105 }, { 0x0801, 0x0FFE }, { 0xA5A5, 0x5A5A } };

	for (unsigned i = 0; i < sizeof(imm) / sizeof(imm[0]); i++) {
		reset();
		evt_site(imm[i][0], imm[i][1]);
		on();
		CHECK(g_tai.calls == 3, "immediates %x %x refused (%d hooks)", imm[i][0], imm[i][1], g_tai.calls);
	}
}

static void test_hooks_and_commands(void)
{
	int conn[0x40], other[0x40];
	reset();
	conn_init(conn, 0x1234, 0x2A);
	conn_init(other, 0x1234, 0x2B);

	on();
	CHECK(g_tai.calls == 3, "hooks installed %d times", g_tai.calls);
	CHECK(g_tai.off[0] == 0x6F50 && g_tai.off[1] == 0x71C8 && g_tai.off[2] == 0xC848 &&
	      g_tai.thumb[0] == 1 && g_tai.thumb[1] == 1 && g_tai.thumb[2] == 1, "offsets %x %x %x", g_tai.off[0], g_tai.off[1], g_tai.off[2]);
	CHECK(g_tai.hook[0] == (const void *)btflush_start_hook && g_tai.hook[1] == (const void *)btflush_stop_hook &&
	      g_tai.hook[2] == (const void *)btflush_evt_hook, "hook bodies");
	CHECK(btflush_slots == 80 && btflush_guard == 1, "the timeout and the guard are not wanted after on");
	on();
	on();
	CHECK(g_tai.calls == 3, "installed again on a quiet pass");

	/* a start of the A2DP stream: the original first, then the timeout, on the connection's own context */
	btflush_start_hook(conn, 0, MASK_A2DP_START);
	CHECK(g_orig_calls == 1 && g_orig_conn == conn && g_orig_flag == 0 && g_orig_mask == MASK_A2DP_START, "original not run as called");
	CHECK(g_sent.n == 1 && g_sent.ctx[0] == 0x1234 && g_sent.op[0] == 0x0C28 && !strcmp(g_sent.fmt[0], "22") &&
	      g_sent.a[0] == 0x2A && g_sent.b[0] == 80, "timeout command wrong (n %d op %x a %d b %d)", g_sent.n, g_sent.op[0], g_sent.a[0], g_sent.b[0]);
	CHECK(FLUSH_SLOTS * 625 / 1000 == 50, "80 slots is not 50 ms");

	/* the stop of another link sends nothing; the stop of this one sends 0, once */
	btflush_stop_hook(other, 0, MASK_A2DP_START);
	CHECK(g_sent.n == 1, "stop of another link sent something");
	btflush_stop_hook(conn, 0, MASK_A2DP_START);
	CHECK(g_sent.n == 2 && g_sent.op[1] == 0x0C28 && g_sent.a[1] == 0x2A && g_sent.b[1] == 0 && !strcmp(g_sent.fmt[1], "22"),
	      "stop did not set the timeout back to 0");
	btflush_stop_hook(conn, 0, MASK_A2DP_START);
	CHECK(g_sent.n == 2, "stop sent again");

	/* not an A2DP source start, a "stop" flavour of the start routine, or no connection: nothing */
	btflush_start_hook(conn, 0, 0x4);
	btflush_start_hook(conn, 0, 0x20000000);
	btflush_start_hook(conn, 1, MASK_A2DP_START);
	btflush_start_hook(NULL, 0, MASK_A2DP_START);
	CHECK(g_sent.n == 2, "a start that is not the A2DP source's sent a command (%d)", g_sent.n);
	int c0[0x40];
	conn_init(c0, 0, 0x2A);
	btflush_start_hook(c0, 0, MASK_A2DP_START);
	CHECK(g_sent.n == 2, "sent with no context");
	btflush_stop_hook(conn, 0, 0x4);
	CHECK(g_sent.n == 2, "a stop that is not the A2DP source's sent a command");

	/* handle 0 is a real handle */
	int c1[0x40];
	conn_init(c1, 0x99, 0);
	btflush_start_hook(c1, 0, MASK_A2DP_START);
	CHECK(g_sent.n == 3 && g_sent.a[2] == 0 && g_sent.b[2] == 80, "handle 0");
	btflush_stop_hook(c1, 0, MASK_A2DP_START);
	CHECK(g_sent.n == 4 && g_sent.b[3] == 0, "handle 0 stop");
}

static void test_switched_off(void)
{
	int conn[0x40];
	reset();
	conn_init(conn, 0x1234, 0x2A);
	on();

	/* Lowest off: the hooks stay in, a start sends nothing */
	btt_btflush_sync(0);
	CHECK(g_tai.releases == 0, "released while the module runs");
	btflush_start_hook(conn, 0, MASK_A2DP_START);
	CHECK(g_sent.n == 0 && g_orig_calls == 1, "a start sent a command with Lowest off");

	/* a stream started with Lowest on, then Lowest off: the stop still clears the timeout */
	btt_btflush_sync(1);
	btflush_start_hook(conn, 0, MASK_A2DP_START);
	CHECK(g_sent.n == 1, "no timeout set");
	btt_btflush_sync(0);
	btflush_stop_hook(conn, 0, MASK_A2DP_START);
	CHECK(g_sent.n == 2 && g_sent.b[1] == 0, "the timeout was left set after the stop");

	/* back on: the same hooks, no second install */
	btt_btflush_sync(1);
	CHECK(g_tai.calls == 3, "installed again (%d)", g_tai.calls);
	btflush_start_hook(conn, 0, MASK_A2DP_START);
	CHECK(g_sent.n == 3 && g_sent.b[2] == 80, "not set again");

	/* module stop: all three out, once; a late call finds no reference and calls nothing */
	btt_btflush_restore();
	CHECK(g_tai.releases == 3 && btflush_slots == 0 && btflush_guard == 0, "released %d", g_tai.releases);
	int before = g_orig_calls, sent = g_sent.n;
	uint8_t ev[7] = { 0x13, 5, 1, 0x2A, 0, 3, 0 };
	btflush_start_hook(conn, 0, MASK_A2DP_START);
	btflush_stop_hook(conn, 0, MASK_A2DP_START);
	btflush_evt_hook(NULL, ev, 7);
	CHECK(g_orig_calls == before && g_sent.n == sent && g_evt.n == 0, "a hook ran with no reference");
	btt_btflush_restore();
	CHECK(g_tai.releases == 3, "restore twice");
}

static void test_hook_failures(void)
{
	int conn[0x40];

	/* the first refused: the others are not tried, no timeout is ever set */
	reset();
	conn_init(conn, 0x1234, 0x2A);
	g_tai.fail_nth = 1;
	for (int i = 0; i < 4; i++)
		btt_btflush_sync(1);
	CHECK(g_tai.calls == 1 && btflush_slots == 0 && btflush_guard == 0, "the second hook was tried, or a timeout armed (%d)", g_tai.calls);
	btt_btflush_restore();
	CHECK(g_tai.releases == 0, "released something that was never in");

	/* the second refused: the first stays in doing nothing, and goes at module stop */
	reset();
	g_tai.fail_nth = 2;
	for (int i = 0; i < 4; i++)
		btt_btflush_sync(1);
	btflush_sender = (void *)fake_sender;
	CHECK(g_tai.calls == 2 && btflush_slots == 0 && btflush_guard == 0, "retried, or a timeout armed (%d)", g_tai.calls);
	btflush_start_hook(conn, 0, MASK_A2DP_START);
	CHECK(g_orig_calls == 1 && g_sent.n == 0, "the lone start hook sent a command");
	btt_btflush_sync(0);
	btt_btflush_sync(1);
	CHECK(g_tai.calls == 2 && btflush_slots == 0, "tried again after off and on");
	btt_btflush_restore();
	CHECK(g_tai.releases == 1, "the hook that was in is not released (%d)", g_tai.releases);

	/* the event hook refused: the other two are in but do nothing, no timeout, no split events */
	reset();
	g_tai.fail_nth = 3;
	for (int i = 0; i < 4; i++)
		btt_btflush_sync(1);
	btflush_sender = (void *)fake_sender;
	CHECK(g_tai.calls == 3 && btflush_slots == 0 && btflush_guard == 0, "a timeout or the guard armed with a hook missing (%d)", g_tai.calls);
	btflush_start_hook(conn, 0, MASK_A2DP_START);
	CHECK(g_orig_calls == 1 && g_sent.n == 0, "the start hook sent a command with the event hook missing");
	btt_btflush_restore();
	CHECK(g_tai.releases == 2, "the two hooks that were in are not released (%d)", g_tai.releases);
}

/* ---- the completed-packets guard ----------------------------------------------------------- */

/* A Number_Of_Completed_Packets event for `n` links: the handles, then the counts. Returns its length. */
static int nocp(uint8_t *ev, int n, const unsigned *handle, const unsigned *count)
{
	ev[0] = 0x13;
	ev[1] = (uint8_t)(1 + 4 * n);
	ev[2] = (uint8_t)n;
	for (int i = 0; i < n; i++) {
		ev[3 + 2 * i] = (uint8_t)(handle[i] & 0xFF);
		ev[4 + 2 * i] = (uint8_t)(handle[i] >> 8);
		ev[3 + 2 * n + 2 * i] = (uint8_t)(count[i] & 0xFF);
		ev[4 + 2 * n + 2 * i] = (uint8_t)(count[i] >> 8);
	}
	return 3 + 4 * n;
}

/* the one event the original must have been given: one link, one count */
static void expect_one(int k, unsigned handle, unsigned count)
{
	CHECK(g_evt.len[k] == 7 && g_evt.bytes[k][0] == 0x13 && g_evt.bytes[k][1] == 5 && g_evt.bytes[k][2] == 1 &&
	      g_evt.bytes[k][3] == (handle & 0xFF) && g_evt.bytes[k][4] == (handle >> 8) &&
	      g_evt.bytes[k][5] == (count & 0xFF) && g_evt.bytes[k][6] == (count >> 8),
	      "call %d: handle %x count %x, got %02x%02x %02x%02x (len %d)", k, handle, count,
	      g_evt.bytes[k][4], g_evt.bytes[k][3], g_evt.bytes[k][6], g_evt.bytes[k][5], g_evt.len[k]);
}

/* an event goes to the original untouched: once, the same bytes at the same address */
static void expect_untouched(const uint8_t *ev, int len, uint32_t *ctx, const char *what)
{
	CHECK(g_evt.n == 1 && g_evt.ptr[0] == ev && g_evt.len[0] == len && g_evt.ctx[0] == ctx && !memcmp(g_evt.bytes[0], ev, (size_t)len),
	      "%s: %d calls, not the event as it came", what, g_evt.n);
}

static void test_evt_alone(void)
{
	uint32_t ctx[4];
	uint8_t disc[6] = { 0x05, 4, 0, 0x2A, 0, 0x13 };
	int conn[0x40];

	/* not wanted: nothing is looked at */
	reset();
	CHECK(btt_btflush_evt_sync(0) == 0 && g_lookups == 0 && g_tai.calls == 0, "the log not wanted: nothing looked at or hooked");
	/* wanted: only the event handler is hooked, once, as Thumb code; it passes events on and tells the log (when the log is on) */
	CHECK(btt_btflush_evt_sync(1) == 1 && g_tai.calls == 1 && g_tai.off[0] == 0xC848 && g_tai.thumb[0] == 1, "the event hook alone goes in at the event handler");
	CHECK(btt_btflush_evt_sync(1) == 1 && btt_btflush_evt_sync(0) == 1 && g_tai.calls == 1, "it is not hooked again, and stays in when not wanted");
	btflush_evt_hook(ctx, disc, 6);
	CHECK(g_evt.n == 1 && g_evt.len[0] == 6 && !memcmp(g_evt.bytes[0], disc, 6) && g_evt.ptr[0] == disc, "the event goes on untouched");
	CHECK(btevlog_ring.head == 0, "log off: nothing noted");
	btevlog_on = 1;
	btflush_evt_hook(ctx, disc, 6);
	CHECK(g_evt.n == 2 && btevlog_ring.head == 1 && btevlog_ring.e[0].kind == BTEL_K_DISC, "log on: the event goes on and one record is stored");
	/* Lowest wanted afterwards: the start and stop hooks go in, the event hook is not hooked twice and its site (now taiHEN's jump) is not looked at again */
	memset(g_text_mem + OFF_EVT, 0xEE, 12);
	memset(g_text_mem + OFF_EVT_TAIL, 0xEE, 14);
	on();
	CHECK(g_tai.calls == 3 && g_tai.off[1] == 0x6F50 && g_tai.off[2] == 0x71C8, "Lowest adds the two stream hooks only (%d calls)", g_tai.calls);
	CHECK(g_flush_uid[0] >= 0 && g_flush_uid[1] >= 0 && g_flush_uid[2] >= 0 && btflush_guard == 1, "all three in, the guard on");
	CHECK(btflush_evt_ref != 0, "the event hook's reference was not cleared by the second install");
	btt_btflush_restore();
	CHECK(g_tai.releases == 3, "module stop releases all three");

	/* Lowest first, the log after: nothing more to hook */
	reset();
	on();
	CHECK(g_tai.calls == 3, "Lowest alone: three hooks");
	CHECK(btt_btflush_evt_sync(1) == 1 && g_tai.calls == 3, "the log finds the event hook there");
	btt_btflush_restore();

	/* the event handler's bytes differ: refused, said once, not tried again; the stream sites are not consulted for the log */
	reset();
	g_text_mem[OFF_EVT_TAIL + 5] ^= 1;
	g_text_mem[OFF_START] ^= 1;
	for (int i = 0; i < 4; i++)
		CHECK(btt_btflush_evt_sync(1) == 0, "refused");
	CHECK(g_tai.calls == 0 && g_lookups == 1, "refused once, then not looked at again (%d lookups)", g_lookups);

	/* the code cannot be read yet: asked again later, hooked when it can */
	reset();
	g_have_module = 0;
	CHECK(btt_btflush_evt_sync(1) == 0 && btt_btflush_evt_sync(1) == 0 && g_tai.calls == 0, "not readable: no hook");
	g_have_module = 1;
	CHECK(btt_btflush_evt_sync(1) == 1 && g_tai.calls == 1, "readable later: hooked");

	/* the hook is refused: no log, not retried; Lowest still works on its own (its three, the event hook among them, are separate) */
	reset();
	g_tai.fail_nth = 1;
	CHECK(btt_btflush_evt_sync(1) == 0 && btt_btflush_evt_sync(1) == 0 && g_tai.calls == 1, "a refused hook is not retried");

	/* the stream sites differ while the log's hook is in: Lowest is refused, the log's hook stays and nothing is set */
	reset();
	CHECK(btt_btflush_evt_sync(1) == 1, "log hook in");
	g_text_mem[OFF_STOP] ^= 1;
	btt_btflush_sync(1);
	CHECK(g_tai.calls == 1 && btflush_slots == 0 && btflush_guard == 0 && g_flush_uid[2] >= 0, "Lowest refused, the log's hook untouched, no timeout, no guard");
	conn_init(conn, 7, 3);
	btt_btflush_restore();
	CHECK(g_tai.releases == 1, "module stop releases the one hook");
	puts("  ok: the event hook alone, for the link log");
}

static void test_guard(void)
{
	uint32_t ctx[4];
	uint8_t ev[64];
	unsigned h1[] = { 0x2A }, c1[] = { 3 };
	unsigned h2[] = { 0x2A, 0x07 }, c2[] = { 3, 1 };
	unsigned h3[] = { 0x07, 0x2A, 0x0F01 }, c3[] = { 0x0102, 0x0304, 0x0001 };
	int len;

	reset();
	on();
	CHECK(btflush_guard == 1, "guard not on");

	/* one link: the original is given the very event, untouched */
	len = nocp(ev, 1, h1, c1);
	btflush_evt_hook(ctx, ev, len);
	expect_untouched(ev, len, ctx, "one link");

	/* two links, the headphones first and a controller second: one well-formed event each, in order */
	g_evt.n = 0;
	len = nocp(ev, 2, h2, c2);
	btflush_evt_hook(ctx, ev, len);
	CHECK(g_evt.n == 2 && g_evt.ctx[0] == ctx && g_evt.ctx[1] == ctx, "two links: %d calls", g_evt.n);
	expect_one(0, 0x2A, 3);
	expect_one(1, 0x07, 1);

	/* the second link first: the headphones' completion is the second pair, and is not lost */
	g_evt.n = 0;
	{
		unsigned h[] = { 0x07, 0x2A }, c[] = { 1, 3 };
		len = nocp(ev, 2, h, c);
	}
	btflush_evt_hook(ctx, ev, len);
	CHECK(g_evt.n == 2, "two links, headphones second: %d calls", g_evt.n);
	expect_one(0, 0x07, 1);
	expect_one(1, 0x2A, 3);

	/* three links, 16-bit values in both fields (the flag bits of a handle and big counts are passed on as they are) */
	g_evt.n = 0;
	len = nocp(ev, 3, h3, c3);
	btflush_evt_hook(ctx, ev, len);
	CHECK(g_evt.n == 3, "three links: %d calls", g_evt.n);
	expect_one(0, 0x07, 0x0102);
	expect_one(1, 0x2A, 0x0304);
	expect_one(2, 0x0F01, 0x0001);
	CHECK(g_evt.ptr[0] != ev, "the split event is the original's own buffer");

	/* the largest event a controller can send: 63 links (255 bytes of parameters) */
	{
		unsigned h[63], c[63];
		uint8_t big[3 + 4 * 63];

		for (int i = 0; i < 63; i++) {
			h[i] = 0x100 + (unsigned)i;
			c[i] = 1 + (unsigned)i;
		}
		g_evt.n = 0;
		len = nocp(big, 63, h, c);
		CHECK(len == 255 && big[1] == 253, "length of 63 links: %d", len);
		btflush_evt_hook(ctx, big, len);
		CHECK(g_evt.n == 63, "63 links: %d calls", g_evt.n);
		for (int i = 0; i < 63; i++)
			expect_one(i, h[i], c[i]);
	}

	/* events that are not completed-packets events go through untouched */
	for (int code = 0; code < 256; code++) {
		if (code == 0x13)
			continue;
		uint8_t other[9] = { (uint8_t)code, 7, 2, 1, 0, 2, 0, 5, 0 };
		g_evt.n = 0;
		btflush_evt_hook(ctx, other, 9);
		expect_untouched(other, 9, ctx, "another event");
	}

	/* not a well-formed list: untouched */
	{
		uint8_t bad[16];
		memcpy(bad, ev, 16);
		g_evt.n = 0;
		bad[0] = 0x13; bad[1] = 8; bad[2] = 2;            /* two links need 9 bytes of parameters, not 8 */
		btflush_evt_hook(ctx, bad, 10);
		expect_untouched(bad, 10, ctx, "wrong parameter length");

		len = nocp(bad, 2, h2, c2);
		g_evt.n = 0;
		btflush_evt_hook(ctx, bad, len - 1);               /* cut short: the length says one byte less */
		expect_untouched(bad, len - 1, ctx, "cut short");
		g_evt.n = 0;
		btflush_evt_hook(ctx, bad, len + 1);               /* longer than the parameters say */
		expect_untouched(bad, len + 1, ctx, "longer than its parameters");
		g_evt.n = 0;
		bad[2] = 3;                                        /* Num_Handles that does not match the length */
		btflush_evt_hook(ctx, bad, len);
		expect_untouched(bad, len, ctx, "a count of links that does not match");
		bad[2] = 0; bad[1] = 1;                            /* no links at all */
		g_evt.n = 0;
		btflush_evt_hook(ctx, bad, 3);
		expect_untouched(bad, 3, ctx, "no links");
		g_evt.n = 0;
		btflush_evt_hook(ctx, bad, 2);                     /* nothing after the header */
		expect_untouched(bad, 2, ctx, "header only");
		g_evt.n = 0;
		btflush_evt_hook(ctx, bad, 0);
		expect_untouched(bad, 0, ctx, "empty");
	}

	/* the guard off (Lowest off): several links go through untouched, and one link still does */
	btt_btflush_sync(0);
	CHECK(btflush_guard == 0, "guard still on");
	len = nocp(ev, 2, h2, c2);
	g_evt.n = 0;
	btflush_evt_hook(ctx, ev, len);
	expect_untouched(ev, len, ctx, "guard off");

	/* and on again */
	btt_btflush_sync(1);
	g_evt.n = 0;
	btflush_evt_hook(ctx, ev, len);
	CHECK(g_evt.n == 2, "guard not back on: %d calls", g_evt.n);
	CHECK(btflush_in_evt == 0, "left the in-hook flag set");
}

int main(int argc, char **argv)
{
	static const struct { const char *name; void (*fn)(void); } tests[] = {
		{ "nothing is looked at until Lowest is wanted", test_not_wanted },
		{ "the event hook alone, for the link log", test_evt_alone },
		{ "refuses on other bytes, a missing module or address", test_refusals },
		{ "the hooks and the commands they send", test_hooks_and_commands },
		{ "switched off, stop, and no reference", test_switched_off },
		{ "a refused hook: no timeout and no guard", test_hook_failures },
		{ "the event handler's address loads may hold any immediates", test_loader_immediates },
		{ "the guard: one event per link, everything else untouched", test_guard },
	};
	int n = (int)(sizeof(tests) / sizeof(tests[0])), fails = 0;

	if (argc > 1) {   /* a child runs one test: btflush.c keeps state in statics */
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
	printf("test_btflush: %d tests, %d failed\n", n, fails);
	return fails ? 1 : 0;
}
