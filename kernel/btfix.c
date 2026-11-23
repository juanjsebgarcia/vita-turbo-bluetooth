/*
 * btfix.c - small byte-checked changes to the system's Bluetooth code.
 *
 *   - Timing fix (on whenever Bluetooth Turbo is on): each Bluetooth audio packet
 *     carries a timestamp that should advance by the 512 samples it holds;
 *     the system advances it by 8000. Most headphones ignore it, some recent
 *     earbuds stay silent because of it. One instruction's constant, 8000 ->
 *     512. Another plugin may already make the same change: then the bytes
 *     are found already fixed and this leaves them alone.
 *   - Bluetooth latency (a Settings list, Standard by default): the sender
 *     lets up to 23 packets (about 245 ms) queue for the radio before it
 *     backs off; Low lowers that to 11 (about 117 ms) and Lowest to 7 (about
 *     75 ms), so lag can't build up as far when the radio is busy. The
 *     recovery point is left as it is. On an AAC stream (btt_bt_set_aac) the same setting means 11 (Standard), 5 (Low) and 3 (Lowest) packets
 *     (btaac_lat.h): an AAC packet is twice as long in sound (21.3 ms), so the limits are about the same time.
 *   - Wireless controller response (a Settings list, Standard by default):
 *     the Bluetooth code puts the Sony controllers it knows into a low-power
 *     mode with an 11.25 ms interval, so their reports can wait that long.
 *     Fast makes that interval 0 at the two places that set it, which means
 *     no low-power mode. It applies from the next time a controller connects.
 *
 * Each is a one-constant change in the code, checked byte for byte against the
 * stock code (identical on every firmware 3.60-3.74) before it goes in, and
 * taken out again with its setting, the master switch or module stop. Nothing
 * else is touched.
 *
 * When each goes in is main.c's business (bt_pass): the timing fix and the queue's limit from
 * BTT_EARLY_UPTIME, the controller response from BTT_MIN_UPTIME (it is given "standard" until then).
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "klog.h"
#include "btfix.h"
#include "btaac_lat.h"

int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr);

#define OFF_TS      0x785C   /* the add that steps the packet timestamp (8000)      */
#define OFF_QUEUE   0x791E   /* the limit of packets in flight before backing off (23) */
#define OFF_CTRL_A  0x132EC  /* movs r4,#0x12: the controller low-power interval, first place  */
#define OFF_CTRL_B  0x1341C  /* the same instruction, the second place                         */

static const uint8_t k_ts_stock[4] = { 0x18, 0xF5, 0xFA, 0x58 };   /* #8000 */
static const uint8_t k_ts_fixed[4] = { 0x18, 0xF5, 0x00, 0x78 };   /* #512  */
static const uint8_t k_q_stock[2]  = { 0x17, 0x2B };               /* #23   */
/* The limit by mode: 0..2 an SBC stream at Standard (the stock #23, nothing written), Low (#11), Lowest (#7); 4..6 an AAC stream at Standard, Low, Lowest (btaac_lat.h: 11, 5, 3) */
static const uint8_t k_q_mode[7] = { 0x17, 0x0B, 0x07, 0x00, 0x0B, 0x05, 0x03 };
_Static_assert(BTT_LAT_COUNT == 3, "the table has one entry per level, twice");
static const uint8_t k_c_stock[2]  = { 0x12, 0x24 };               /* movs r4,#0x12 */
static const uint8_t k_c_fast[2]   = { 0x00, 0x24 };               /* movs r4,#0    */

static SceUID g_mod = -1;
static const volatile uint8_t *g_text;
static int g_state = -1;            /* -1 not looked at yet, 0 unusable, 1 ready */
static int g_ts_other;              /* timing already fixed by someone else      */
static int g_ctrl_ok;               /* the two controller sites hold the stock bytes */
static SceUID g_inj_ts = -1, g_inj_q = -1, g_inj_c[2] = { -1, -1 };
static int g_q_in;                  /* the mode written (0 = none; see k_q_mode)  */
static int g_q_failed;              /* the mode whose write was refused, 0 = none */
static int g_aac;                   /* the stream is AAC (btt_bt_set_aac)         */
static int g_c_failed;              /* the controller change was refused         */

static int same(const volatile uint8_t *p, const uint8_t *b, int n)
{
	for (int i = 0; i < n; i++)
		if (p[i] != b[i])
			return 0;
	return 1;
}

static int find(void)
{
	if (g_state >= 0)
		return g_state;
	g_state = 0;
	tai_module_info_t ti;
	memset(&ti, 0, sizeof(ti));
	ti.size = sizeof(ti);
	uintptr_t base = 0;
	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceBt", &ti) < 0 ||
	    module_get_offset(KERNEL_PID, ti.modid, 0, 0, &base) < 0 || !base) {
		BTT_KLOG("bluetooth: sender not found - no changes");
		return 0;
	}
	g_mod = ti.modid;
	g_text = (const volatile uint8_t *)base;
	if (same(g_text + OFF_TS, k_ts_fixed, 4))
		g_ts_other = 1;
	else if (!same(g_text + OFF_TS, k_ts_stock, 4)) {
		BTT_KLOG("bluetooth: sender code not recognised - no changes");
		return 0;
	}
	if (!same(g_text + OFF_QUEUE, k_q_stock, 2)) {
		BTT_KLOG("bluetooth: sender code not recognised - no changes");
		return 0;
	}
	/* The controller sites are checked on their own: if they differ, only that setting is out. */
	g_ctrl_ok = same(g_text + OFF_CTRL_A, k_c_stock, 2) && same(g_text + OFF_CTRL_B, k_c_stock, 2);
	BTT_KLOG("bluetooth: sender recognised%s%s", g_ts_other ? " (timing already fixed by another plugin)" : "",
	         g_ctrl_ok ? "" : " (controller code not recognised: controller response unchanged)");
	g_state = 1;
	return 1;
}

static void release(SceUID *inj)
{
	if (*inj >= 0) {
		taiInjectReleaseForKernel(*inj);
		*inj = -1;
	}
}

/* The send queue's limit follows `level` (BttLatency) and the codec of the stream. A change of either takes the old byte out
 * first. A refused write is not tried again every pass: only after the wanted mode changes. */
static void sync_queue(int level, int aac)
{
	static const char *const name[BTT_LAT_COUNT] = { "standard", "low", "lowest" };
	int mode = level + (aac ? 4 : 0);

	if (mode != g_q_failed)
		g_q_failed = 0;
	if (g_q_in == mode)
		return;
	if (g_inj_q >= 0) {
		release(&g_inj_q);
		BTT_KLOG("bluetooth: latency %s%s off", name[g_q_in & 3], (g_q_in & 4) ? " (AAC)" : "");
		g_q_in = 0;
	}
	if (mode && mode != g_q_failed) {
		g_inj_q = taiInjectDataForKernel(KERNEL_PID, g_mod, 0, OFF_QUEUE, &k_q_mode[mode], 1);
		if (g_inj_q >= 0) {
			g_q_in = mode;
			BTT_KLOG("bluetooth: latency %s on%s: %u packets", name[level], aac ? " (AAC stream)" : "", (unsigned)k_q_mode[mode]);
		} else {
			g_inj_q = -1;
			g_q_failed = mode;
			BTT_KLOG("bluetooth: latency %s%s failed", name[level], aac ? " (AAC)" : "");
		}
	}
}

void btt_bt_set_aac(int aac)
{
	g_aac = aac != 0;
}

/* Wireless controller response: both sites or neither. */
static void sync_ctrl(int fast)
{
	if (!fast)
		g_c_failed = 0;
	if (!fast && g_inj_c[0] >= 0) {
		release(&g_inj_c[0]);
		release(&g_inj_c[1]);
		BTT_KLOG("bluetooth: controller response standard");
	} else if (fast && g_inj_c[0] < 0 && !g_c_failed && g_ctrl_ok) {
		g_inj_c[0] = taiInjectDataForKernel(KERNEL_PID, g_mod, 0, OFF_CTRL_A, k_c_fast, 2);
		g_inj_c[1] = g_inj_c[0] >= 0 ? taiInjectDataForKernel(KERNEL_PID, g_mod, 0, OFF_CTRL_B, k_c_fast, 2) : -1;
		if (g_inj_c[1] < 0)
			release(&g_inj_c[0]);   /* never half a change */
		if (g_inj_c[1] >= 0) {
			BTT_KLOG("bluetooth: controller response fast (from the next connection)");
		} else {
			g_inj_c[0] = g_inj_c[1] = -1;
			g_c_failed = 1;
			BTT_KLOG("bluetooth: controller response fast failed");
		}
	}
}

void btt_bt_sync(int ts_fix, int latency, int ctrl_fast)
{
	if (!find())
		return;
	if (latency < 0 || latency >= BTT_LAT_COUNT)
		latency = 0;
	if (ts_fix && !g_ts_other && g_inj_ts == -1) {
		g_inj_ts = taiInjectDataForKernel(KERNEL_PID, g_mod, 0, OFF_TS, k_ts_fixed, 4);
		BTT_KLOG("bluetooth: timing fix %s", g_inj_ts >= 0 ? "on" : "failed");
		if (g_inj_ts < 0)
			g_inj_ts = -2;   /* don't retry every pass */
	} else if (!ts_fix && g_inj_ts >= 0) {
		release(&g_inj_ts);
		BTT_KLOG("bluetooth: timing fix off");
	}
	/* A failed change may be tried again after the switch goes off and on. */
	if (!ts_fix && g_inj_ts == -2)
		g_inj_ts = -1;
	sync_queue(latency, g_aac);
	sync_ctrl(ctrl_fast);
}

void btt_bt_restore(void)
{
	release(&g_inj_ts);
	release(&g_inj_q);
	release(&g_inj_c[0]);
	release(&g_inj_c[1]);
	g_q_in = 0;
	g_aac = 0;
}

/* The Bluetooth code's data segment: its address, only once the code is recognised (the offsets read from it were checked
 * against exactly this code), and only if both ends of the segment resolve. 0 otherwise. */
#define BTT_BT_SEG1_BYTES  0x473D0u   /* its size in every firmware 3.60-3.74 */
static uintptr_t g_seg1;

uintptr_t btt_bt_seg1(void)
{
	uintptr_t last = 0;

	if (g_state != 1)
		return 0;
	if (!g_seg1 && (module_get_offset(KERNEL_PID, g_mod, 1, 0, &g_seg1) < 0 ||
	                module_get_offset(KERNEL_PID, g_mod, 1, BTT_BT_SEG1_BYTES - 1, &last) < 0 || !g_seg1 || !last))
		g_seg1 = 0;
	return g_seg1;
}
