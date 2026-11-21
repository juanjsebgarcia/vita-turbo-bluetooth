/*
 * btbatt.c - the headset's battery level, as another module can read it.
 *
 * bttGetHeadsetBattery (a syscall of the SceBluetoothTurbo library, optional for its callers) copies a snapshot of
 * what a headset last reported to the console (bthfp_mod.c stores it). It says "unknown" until a headset has reported one,
 * while Bluetooth Turbo is off and after the headset's link has been down for ten minutes. The snapshot is kept here by one
 * writer (the worker) and read by any caller
 * with no lock, so a reader can never hold anything up: the writer bumps a sequence word to an odd value before it
 * changes the fields and to the next even value after, and a reader takes the copy again if the word changed
 * or was odd. A reader that never gets a whole copy says "unknown" rather than a mixed one.
 */
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/kernel/threadmgr.h>

#include "../include/bluetoothturbo.h"
#include "btbatt.h"

static volatile uint32_t g_seq;
static volatile int32_t g_level = BTT_BATT_UNKNOWN;
static volatile uint32_t g_source;
static volatile uint64_t g_arrived_us;      /* 0 = none */
static volatile uint32_t g_connected;
static volatile uint64_t g_down_us;         /* when the link went down, 0 while it is up or was never up */

/* microseconds to milliseconds, clamped to what 32 bits hold, in 32-bit steps (a 64-bit division would call a library routine the
 * module does not otherwise use): the high word and the low word in two 16-bit halves, each divided by 1000 with its remainder carried. */
static uint32_t us_to_ms(uint64_t us)
{
	uint32_t hi = (uint32_t)(us >> 32), lo = (uint32_t)us, x, y, q2, q3, r1 = hi % 1000u;

	if (hi / 1000u)
		return 4294967294u;
	x = (r1 << 16) | (lo >> 16);
	q2 = x / 1000u;
	y = ((x % 1000u) << 16) | (lo & 65535u);
	q3 = y / 1000u;
	if (q2 > 65535u || (q2 << 16) > 4294967294u - q3)
		return 4294967294u;
	return (q2 << 16) + q3;
}

void btt_batt_read(BttHeadsetBattery *out, uint64_t now_us)
{
	int32_t level = BTT_BATT_UNKNOWN;
	uint32_t source = 0, connected = 0, s1, s2;
	uint64_t arrived = 0, down = 0;
	int whole = 0;
	/* a few tries: the writer is quick and runs twice a second at most */
	for (int i = 0; i < 8; i++) {
		s1 = g_seq;
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		level = g_level;
		source = g_source;
		arrived = g_arrived_us;
		connected = g_connected;
		down = g_down_us;
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		s2 = g_seq;
		if (s1 == s2 && !(s1 & 1)) {
			whole = 1;
			break;
		}
	}
	out->level = BTT_BATT_UNKNOWN;
	out->source = BTT_BATT_SRC_NONE;
	out->age_ms = 0;
	out->connected = connected ? 1 : 0;
	if (!whole || !arrived || level < 0 || level > 100 || now_us < arrived)
		return;
	if (!connected && down && now_us - down > (uint64_t)BTT_BATT_KEEP_S * 1000000ull)
		return;
	out->level = level;
	out->source = source;
	out->age_ms = us_to_ms(now_us - arrived);
}

/* The call: the caller's size is read first, and the answer is copied out with the system's checked copies, never
 * through the caller's pointer. */
int bttGetHeadsetBattery(BttHeadsetBattery *out)
{
	BttHeadsetBattery k;
	uint32_t size = 0, n;

	if (!out || ksceKernelMemcpyUserToKernel(&size, out, sizeof(size)) < 0 || size < BTT_BATT_SIZE_MIN)
		return BTT_ERR_ARG;
	memset(&k, 0, sizeof(k));
	btt_batt_read(&k, ksceKernelGetSystemTimeWide());
	k.version = BTT_BATT_VERSION;
	n = size < sizeof(k) ? size : (uint32_t)sizeof(k);
	k.size = n;
	if (ksceKernelMemcpyKernelToUser(out, &k, n) < 0)
		return BTT_ERR_ARG;
	return BTT_OK;
}

static void begin(void) { g_seq++; __atomic_thread_fence(__ATOMIC_RELEASE); }
static void end(void) { __atomic_thread_fence(__ATOMIC_RELEASE); g_seq++; }

void btt_batt_report(int level, unsigned source, uint64_t arrived_us)
{
	if (level < 0 || level > 100)
		return;
	begin();
	g_level = level;
	g_source = source;
	g_arrived_us = arrived_us ? arrived_us : 1;
	end();
}

void btt_batt_link(int connected, uint64_t now_us)
{
	if ((g_connected != 0) == (connected != 0))
		return;
	begin();
	g_connected = connected ? 1 : 0;
	g_down_us = connected ? 0 : now_us;
	end();
}

void btt_batt_reset(void)
{
	begin();
	g_level = BTT_BATT_UNKNOWN;
	g_source = BTT_BATT_SRC_NONE;
	g_arrived_us = 0;
	g_connected = 0;
	g_down_us = 0;
	end();
}
