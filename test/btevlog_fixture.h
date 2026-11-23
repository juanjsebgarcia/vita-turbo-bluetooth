/*
 * btevlog_fixture.h - what the link log's host tests share (test/test_btevlog.c): the fakes of the console's calls, the
 * kernel sources compiled in as they are, and helpers that feed the hook-side routine controller events and set up the Bluetooth code's link slots.
 */
#ifndef BTEVLOG_FIXTURE_H
#define BTEVLOG_FIXTURE_H
#include <btt_host.h>
#include <stdlib.h>
#include <stdarg.h>

#define UNUSED __attribute__((unused))

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

/* ---- fakes ----------------------------------------------------------------------------- */

static uint64_t g_now = 100u * 1000000u;
uint64_t ksceKernelGetSystemTimeWide(void) { return g_now; }
uint32_t ksceKernelGetSystemTimeLow(void) { return (uint32_t)g_now; }

#define MAXLINES 2000
static char g_log[MAXLINES][200];
static int g_nlog;
static size_t g_log_bytes;
void btt_klog_line(const char *line)
{
	CHECK(g_nlog < MAXLINES, "too many log lines");
	CHECK(strlen(line) <= 250, "a log line over the logger's 250 bytes would be cut: %s", line);
	snprintf(g_log[g_nlog++], sizeof(g_log[0]), "%s", line);
	g_log_bytes += strlen(line) + 1;
}

static int g_hook_in = 1, g_evt_calls;
int btt_btflush_evt_sync(int want) { g_evt_calls++; return want && g_hook_in; }

static uint8_t g_seg[0x30000];
uintptr_t btt_bt_seg1(void) { return (uintptr_t)g_seg; }
uint8_t *btt_bt_seg1_w(void) { return g_seg; }

#include "../kernel/btevlog_hook.c"
#include "../kernel/btevlog.c"

/* ---- helpers ------------------------------------------------------------------------------- */

static UNUSED int has_line(const char *needle)
{
	for (int i = 0; i < g_nlog; i++)
		if (strstr(g_log[i], needle))
			return 1;
	return 0;
}
static UNUSED int count_lines(const char *needle)
{
	int n = 0;

	for (int i = 0; i < g_nlog; i++)
		if (strstr(g_log[i], needle))
			n++;
	return n;
}
/* the address 11:22:33:44:55:66 as the controller sends it: least significant byte first */
static UNUSED const uint8_t ADDR[6] = { 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };
#define ADDR_LO 0x33445566u
#define ADDR_HI 0x1122u

static UNUSED void ev_conn_req(const uint8_t *a)
{
	uint8_t e[12] = { 0x04, 10, a[0], a[1], a[2], a[3], a[4], a[5], 0x04, 0x04, 0x24, 1 };
	btevlog_note_hci(e, 12);
}
static UNUSED void ev_conn_cmp(const uint8_t *a, int status, int handle, int type)
{
	uint8_t e[13] = { 0x03, 11, (uint8_t)status, (uint8_t)handle, (uint8_t)(handle >> 8), a[0], a[1], a[2], a[3], a[4], a[5], (uint8_t)type, 0 };
	btevlog_note_hci(e, 13);
}
static UNUSED void ev_disc(int status, int handle, int reason)
{
	uint8_t e[6] = { 0x05, 4, (uint8_t)status, (uint8_t)handle, (uint8_t)(handle >> 8), (uint8_t)reason };
	btevlog_note_hci(e, 6);
}
static UNUSED void ev_auth(int status, int handle)
{
	uint8_t e[5] = { 0x06, 3, (uint8_t)status, (uint8_t)handle, (uint8_t)(handle >> 8) };
	btevlog_note_hci(e, 5);
}
static UNUSED void ev_enc(int status, int handle, int on)
{
	uint8_t e[6] = { 0x08, 4, (uint8_t)status, (uint8_t)handle, (uint8_t)(handle >> 8), (uint8_t)on };
	btevlog_note_hci(e, 6);
}
static UNUSED void ev_role(const uint8_t *a, int role)
{
	uint8_t e[10] = { 0x12, 8, 0, a[0], a[1], a[2], a[3], a[4], a[5], (uint8_t)role };
	btevlog_note_hci(e, 10);
}

/* the Bluetooth code's link slot i, with the ACL bit, the address, and channel k set to a PSM */
static UNUSED void slot_up(int i, uint32_t lo, uint32_t hi)
{
	uint8_t *c = g_seg + LS_CONN0 + (size_t)i * LS_STRIDE;

	*(uint32_t *)(c + LS_FLAGS) = 0x100;
	*(uint32_t *)(c + LS_MAC_LO) = lo;
	*(uint32_t *)(c + LS_MAC_HI) = hi;
}
static UNUSED void chan(int slot, int k, unsigned psm, unsigned tag, unsigned flags)
{
	uint8_t *ch = g_seg + LS_CONN0 + (size_t)slot * LS_STRIDE + LS_CH0 + (size_t)k * LS_CH_STRIDE;

	*(uint16_t *)(ch + LS_CH_PSM) = (uint16_t)psm;
	*(uint32_t *)(ch + LS_CH_PROF) = tag;
	*(uint32_t *)(ch + LS_CH_FLAGS) = flags;
}

static UNUSED void reset(void)
{
	memset(g_seg, 0, sizeof(g_seg));
	memset(&btevlog_ring, 0, sizeof(btevlog_ring));
	btevlog_on = 0;
	g_nlog = 0;
	g_log_bytes = 0;
	el_inited = 0;
	el_was_on = 0;
	el_lines = 0;
	el_unknown_lines = 0;
	el_fast = 0;
	g_hook_in = 1;
	g_now = 100u * 1000000u;
}
static UNUSED void tick(unsigned ms)
{
	g_now += (uint64_t)ms * 1000u;
}


#endif
