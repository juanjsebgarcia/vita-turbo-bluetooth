/*
 * btflush.c - the flush timeout and the completed-packets guard for Bluetooth latency Lowest. See btflush.h;
 * the hook bodies are in btflush_hook.c.
 *
 * The Bluetooth code never sets a flush timeout, so a media packet the radio cannot deliver is
 * retried until it succeeds, however late. With Lowest the send queue is only 7 packets (about 75 ms);
 * a flush timeout of 50 ms on the link makes the controller discard a packet that old rather than
 * deliver it. The timeout is set with the controller command Write_Automatic_Flush_Timeout, through the
 * Bluetooth code's own command sender, when the A2DP stream starts, and set back to 0 (never discard,
 * the stock behaviour) when it stops.
 *
 * Three hooks, on the Bluetooth code's stream-start and stream-stop routines (text offsets 0x6F50 and
 * 0x71C8) and on its event handler (0xC848). None begins with a load relative to the program counter,
 * and each is checked against the stock bytes before it goes in: the first eight (start, stop) or six
 * (sender) bytes hold no absolute address; the event handler's entry holds two instructions that load
 * an address the loader fills in, so their immediates are masked and the bytes after them checked too.
 * They run in the Bluetooth code's own threads, where the original already used the same sender, and
 * never sleep. All three go in or none does: a timeout without the guard is never set.
 *
 * Tested on a console with Lowest under Wi-Fi load: no clicks, no overflows. Only the guard's path for an
 * event that names several links has not run on hardware (one device was connected).
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "btflush.h"
#include "btflush_hook.h"
#include "klog.h"

int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr);

#define OFF_START    0x6F50u    /* stream start: (connection, flag, mask)                 */
#define OFF_STOP     0x71C8u    /* stream stop: (connection, flag, mask)                  */
#define OFF_SENDER   0x10434u   /* the command sender: (context, opcode, format, values...) */
#define OFF_EVT      0xC848u    /* the event handler: (context, event, length)            */
#define OFF_EVT_TAIL 0xC854u    /* its code after the address loads                       */
#define FLUSH_SLOTS  80u        /* 0.625 ms slots: 50 ms. Not below 40 ms (signalling shares the link) */

static const uint8_t k_start_stock[8] = { 0x2D, 0xE9, 0xF0, 0x47, 0x88, 0x46, 0x86, 0xB0 };
static const uint8_t k_stop_stock[8]  = { 0x00, 0xF5, 0x78, 0x53, 0x2D, 0xE9, 0xF0, 0x41 };
static const uint8_t k_sender_stock[6] = { 0x0C, 0xB4, 0x2D, 0xE9, 0xF0, 0x4F };
/* push.w {r4-fp,lr}; movw r4,#imm; movt r4,#imm; the immediates are the loader's */
static const uint8_t k_evt_head[12] = { 0x2D, 0xE9, 0xF0, 0x4F, 0x40, 0xF2, 0x00, 0x04, 0xC0, 0xF2, 0x00, 0x04 };
static const uint8_t k_evt_mask[12] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFB, 0x00, 0x0F, 0xF0, 0xFB, 0x00, 0x0F };
static const uint8_t k_evt_tail[14] = { 0x01, 0x2A, 0xA5, 0xB0, 0x92, 0x46, 0x23, 0x68, 0x05, 0x46, 0x8B, 0x46, 0x23, 0x93 };

#define N_HOOKS 3
static SceUID g_flush_uid[N_HOOKS] = { -1, -1, -1 };
static int g_flush_dead;                           /* installing failed: never set a timeout   */

static int flush_same(const volatile uint8_t *p, const uint8_t *b, int n)
{
	for (int i = 0; i < n; i++)
		if (p[i] != b[i])
			return 0;
	return 1;
}

static int flush_same_masked(const volatile uint8_t *p, const uint8_t *b, const uint8_t *mask, int n)
{
	for (int i = 0; i < n; i++)
		if ((p[i] & mask[i]) != (b[i] & mask[i]))
			return 0;
	return 1;
}

static int flush_find(void)
{
	tai_module_info_t ti;
	uintptr_t base = 0;

	memset(&ti, 0, sizeof(ti));
	ti.size = sizeof(ti);
	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceBt", &ti) < 0 ||
	    module_get_offset(KERNEL_PID, ti.modid, 0, 0, &base) < 0 || !base)
		return -1;
	const volatile uint8_t *text = (const volatile uint8_t *)base;
	if (!flush_same(text + OFF_START, k_start_stock, 8) || !flush_same(text + OFF_STOP, k_stop_stock, 8) ||
	    !flush_same(text + OFF_SENDER, k_sender_stock, 6) ||
	    !flush_same_masked(text + OFF_EVT, k_evt_head, k_evt_mask, 12) || !flush_same(text + OFF_EVT_TAIL, k_evt_tail, 14)) {
		BTT_KLOG("bluetooth flush: stream or event code not recognised - no flush timeout");
		return -1;
	}
	btflush_sender = (void *)(base + OFF_SENDER + 1);   /* Thumb */
	return (int)ti.modid;
}

static void flush_release_all(void)
{
	static volatile tai_hook_ref_t *const ref[N_HOOKS] = { &btflush_start_ref, &btflush_stop_ref, &btflush_evt_ref };

	for (int i = N_HOOKS - 1; i >= 0; i--)
		if (g_flush_uid[i] >= 0) {
			taiHookReleaseForKernel(g_flush_uid[i], *ref[i]);
			g_flush_uid[i] = -1;
		}
}

void btt_btflush_sync(int lowest)
{
	if (!lowest) {
		btflush_slots = 0;   /* the hooks stay in, doing nothing */
		btflush_guard = 0;
		return;
	}
	if (g_flush_dead)
		return;   /* a refused install is not tried again until the module is loaded again */
	if (g_flush_uid[0] < 0) {
		static const struct { uint32_t off; const void *hook; } site[N_HOOKS] = {
			{ OFF_START, (const void *)btflush_start_hook },
			{ OFF_STOP, (const void *)btflush_stop_hook },
			{ OFF_EVT, (const void *)btflush_evt_hook },
		};
		static volatile tai_hook_ref_t *const ref[N_HOOKS] = { &btflush_start_ref, &btflush_stop_ref, &btflush_evt_ref };
		SceUID r[N_HOOKS] = { -1, -1, -1 };
		int modid = flush_find(), i;

		if (modid < 0) {
			g_flush_dead = 1;
			return;
		}
		*ref[0] = *ref[1] = *ref[2] = 0;
		for (i = 0; i < N_HOOKS; i++) {
			r[i] = taiHookFunctionOffsetForKernel(KERNEL_PID, (tai_hook_ref_t *)ref[i], modid, 0,
			                                      site[i].off, 1, site[i].hook);
			if (r[i] < 0)
				break;
			g_flush_uid[i] = r[i];   /* in: released at module stop even if a later one is refused */
		}
		if (i < N_HOOKS) {
			/* A hook that did get in stays in and does nothing; the timeout is never set. */
			BTT_KLOG("bluetooth flush: hooks failed (0x%08x at site %d) - no flush timeout", (unsigned)r[i], i);
			g_flush_dead = 1;
			return;
		}
		BTT_KLOG("bluetooth flush: hooks on (stream start, stream stop, events)");
	}
	btflush_slots = FLUSH_SLOTS;
	btflush_guard = 1;
}

void btt_btflush_restore(void)
{
	btflush_slots = 0;
	btflush_guard = 0;
	flush_release_all();
	for (int i = 0; i < 100 && (btflush_in_start || btflush_in_stop || btflush_in_evt); i++)
		ksceKernelDelayThread(1000);
	btflush_start_ref = btflush_stop_ref = btflush_evt_ref = 0;
}
