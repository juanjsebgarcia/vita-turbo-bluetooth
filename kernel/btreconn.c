/*
 * btreconn.c - Reconnect After Standby: connect the headphones again after the console wakes from standby.
 *
 * Before standby the Bluetooth code ends every link itself (a normal "user ended the connection"), so most
 * headphones stay silent after the wake-up until they are switched off and on. With this on, the console
 * pages the headphones it last played sound on.
 *
 *   - Which headphones: SceAVConfig calls the Bluetooth code's ksceBtStartAudio with the device's address
 *     and, for sound, the A2DP flag whenever it starts sound on a device. An import hook on that one call
 *     (btreconn_hook.c, a leaf, two import slots and no code moved) notes the address of the last device it
 *     succeeded on. It is noted in memory only: the module stays loaded across standby.
 *   - When: a system event handler counts the resume events (btreconn_sysevent). The worker, never a
 *     callback, notices a new wake-up, waits until the Bluetooth code says it is up again (the state word
 *     ksceBtGetConfiguration returns, bits 0x38 == 8, as the Bluetooth code itself tests before a connect),
 *     waits 3 s more so headphones that come back by themselves win, and then asks for the device's state
 *     with ksceBtGetConnectingInfo: 1 = no link at all. Any other state (connecting, connected) ends it. Only
 *     with no link does it call ksceBtStartConnect (the call the Settings app's own Connect uses; the device
 *     is already registered, so the profile comes from its entry). A busy answer counts as an attempt: at
 *     most 2 per wake-up, the second 5 s after the first, and never a loop. A wake-up that arrives while a
 *     session runs, or within 30 s of its start, is the same wake-up (the power code sends several events).
 *     If sound has started on any device since the wake-up (the hook noted another address), it ends.
 *
 * The three Bluetooth calls take the Bluetooth code's lock for a few microseconds: the worker raises its
 * priority around each, around each of them, so nothing can preempt it holding the lock.
 * They are looked up by number at run time (the numbers are the same on every firmware 3.60-3.74); a missing
 * one disables the feature, with a log line.
 *
 * Installed only while it is wanted, on the same terms as the other Bluetooth changes (bt_pass in main.c).
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/suspend.h>
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "btreconn.h"
#include "btreconn_hook.h"
#include "klog.h"

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);
int ksceKernelUnregisterSysEventHandler(SceUID uid);   /* not in the SDK header; the stub library has it */

#define LIB_BT_FOR_DRIVER    0xD48CA62D    /* SceBtForDriver, as SceAVConfig imports it                  */
#define NID_START_AUDIO      0x8D47CABDu   /* ksceBtStartAudio(address 0, address 1, flags, ...)          */
#define NID_START_CONNECT    0x6059113Au   /* ksceBtStartConnect(address 0, address 1)                    */
#define NID_GET_CONNINFO     0xE9936446u   /* ksceBtGetConnectingInfo(address 0, address 1)               */
#define NID_GET_CONFIG       0x94C66A3Bu   /* ksceBtGetConfiguration(): the Bluetooth state word's low byte */

#define BT_UP(w)             (((w) & 0x38) == 8)
#define STATE_NO_LINK        1             /* ksceBtGetConnectingInfo: the device has no link             */
#define LOCK_PRIO            0x40          /* the worker's priority while it holds the Bluetooth code's lock */
#define WORKER_PRIO          0x70

#define SETTLE_US      (3 * 1000000ull)    /* after Bluetooth is up: headphones that reconnect by themselves win */
#define RETRY_US       (5 * 1000000ull)    /* between the two attempts                                    */
#define UP_TIMEOUT_US  (60 * 1000000ull)   /* Bluetooth never came up: give up                            */
#define SESSION_US     (90 * 1000000ull)   /* a session never lasts longer than this                      */
#define WAKE_MERGE_US  (30 * 1000000ull)   /* a resume event this soon after a session began is the same wake-up */
#define MAX_ATTEMPTS   2

enum { PH_IDLE = 0, PH_WAIT_UP, PH_SETTLE, PH_RETRY };

static int (*gr_start_connect)(unsigned, unsigned);
static int (*gr_get_conninfo)(unsigned, unsigned);
static int (*gr_get_config)(void);
static SceUID gr_hook = -1, gr_sev = -1;
static int gr_failed;                  /* installing was refused: not tried again until it is switched off and on */
static int gr_phase;
static uint64_t gr_phase_at, gr_session_at;
static int gr_attempts;
static uint32_t gr_wakes_seen, gr_rec_at_wake;

static int rc_lookup(void)
{
	uintptr_t f = 0;

	if (module_get_export_func(KERNEL_PID, "SceBt", LIB_BT_FOR_DRIVER, NID_START_CONNECT, &f) < 0 || !f)
		return -1;
	gr_start_connect = (int (*)(unsigned, unsigned))f;
	f = 0;
	if (module_get_export_func(KERNEL_PID, "SceBt", LIB_BT_FOR_DRIVER, NID_GET_CONNINFO, &f) < 0 || !f)
		return -1;
	gr_get_conninfo = (int (*)(unsigned, unsigned))f;
	f = 0;
	if (module_get_export_func(KERNEL_PID, "SceBt", LIB_BT_FOR_DRIVER, NID_GET_CONFIG, &f) < 0 || !f)
		return -1;
	gr_get_config = (int (*)(void))f;   /* last: set only when all three are found */
	return 0;
}

/* The three calls, the hook and the handler; 0 if all are in. Each step is skipped if it is already done, so
 * a half-installed state is finished or refused as a whole. A hook or handler that did get in stays in
 * (passing through, and counting nothing while not armed) and is released at module stop. */
static int rc_install(void)
{
	if (!gr_get_config && rc_lookup() < 0) {
		BTT_KLOG("bluetooth reconnect: the Bluetooth code's connect calls were not found - off");
		return -1;
	}
	if (gr_hook < 0) {
		btreconn_ref = 0;
		gr_hook = taiHookFunctionImportForKernel(KERNEL_PID, (tai_hook_ref_t *)&btreconn_ref, "SceAVConfig",
		                                        LIB_BT_FOR_DRIVER, NID_START_AUDIO, (const void *)btreconn_hook);
		if (gr_hook < 0) {
			BTT_KLOG("bluetooth reconnect: hook failed 0x%08x - off", (unsigned)gr_hook);
			gr_hook = -1;
			btreconn_ref = 0;
			return -1;
		}
	}
	if (gr_sev < 0) {
		gr_sev = ksceKernelRegisterSysEventHandler("BluetoothTurboReconnect", btreconn_sysevent, NULL);
		if (gr_sev < 0) {
			BTT_KLOG("bluetooth reconnect: standby handler failed 0x%08x - off", (unsigned)gr_sev);
			gr_sev = -1;
			return -1;
		}
	}
	gr_wakes_seen = btreconn_wakes;
	BTT_KLOG("bluetooth reconnect: hook and standby handler on");
	return 0;
}

/* The three calls take the Bluetooth code's lock: at a raised priority, so nothing preempts us holding it. */
static int rc_bt_up(void)
{
	int w;

	ksceKernelChangeThreadPriority(0, LOCK_PRIO);
	w = gr_get_config();
	ksceKernelChangeThreadPriority(0, WORKER_PRIO);
	return w >= 0 && BT_UP(w);
}

static int rc_state(uint32_t mac0, uint32_t mac1)
{
	int s;

	ksceKernelChangeThreadPriority(0, LOCK_PRIO);
	s = gr_get_conninfo(mac0, mac1);
	ksceKernelChangeThreadPriority(0, WORKER_PRIO);
	return s;
}

static int rc_connect(uint32_t mac0, uint32_t mac1)
{
	int r;

	ksceKernelChangeThreadPriority(0, LOCK_PRIO);
	r = gr_start_connect(mac0, mac1);
	ksceKernelChangeThreadPriority(0, WORKER_PRIO);
	return r;
}

/* The noted address, read twice so a half-written one (the hook's two stores) is never used. */
static int rc_mac(uint32_t *m0, uint32_t *m1)
{
	uint32_t a0, a1, b0, b1, n0, n1;
	int tries = 0;

	if (!btreconn_recorded)
		return 0;
	do {
		n0 = btreconn_recorded;
		a0 = btreconn_mac0; a1 = btreconn_mac1;
		b0 = btreconn_mac0; b1 = btreconn_mac1;
		n1 = btreconn_recorded;   /* the hook counts after its two stores: a changed count means a write in between */
	} while ((a0 != b0 || a1 != b1 || n0 != n1) && ++tries < 3);
	*m0 = b0;
	*m1 = b1;
	return a0 == b0 && a1 == b1 && n0 == n1;
}

static void rc_done(const char *why)
{
	gr_phase = PH_IDLE;
	BTT_KLOG("bluetooth reconnect: %s", why);
}

static void rc_step(uint64_t now)
{
	uint32_t m0, m1;
	int st, r;

	if (gr_phase == PH_IDLE)
		return;
	if (now - gr_session_at >= SESSION_US) {
		rc_done("gave up (the wake-up took too long)");
		return;
	}
	if (gr_phase == PH_WAIT_UP) {
		if (now - gr_phase_at >= UP_TIMEOUT_US)
			rc_done("Bluetooth did not come up - nothing done");
		else if (rc_bt_up()) {
			gr_phase = PH_SETTLE;
			gr_phase_at = now;
			BTT_KLOG("bluetooth reconnect: Bluetooth is up - checking in 3 s");
		}
		return;
	}
	/* PH_SETTLE and PH_RETRY */
	if (btreconn_recorded != gr_rec_at_wake) {
		rc_done("sound started on headphones by itself - nothing to do");
		return;
	}
	if (now - gr_phase_at < (gr_phase == PH_SETTLE ? SETTLE_US : RETRY_US))
		return;
	if (!rc_mac(&m0, &m1)) {
		rc_done("no headphones noted yet - nothing to do");
		return;
	}
	st = rc_state(m0, m1);
	if (st < 0)
		return;   /* the Bluetooth code could not answer yet: ask again on the next pass (SESSION_US bounds it) */
	if (st != STATE_NO_LINK) {
		rc_done(gr_attempts ? "the headphones are connected" : "the headphones came back by themselves");
		return;
	}
	if (gr_attempts >= MAX_ATTEMPTS) {
		rc_done("gave up after two attempts");
		return;
	}
	gr_attempts++;
	r = rc_connect(m0, m1);
	BTT_KLOG("bluetooth reconnect: attempt %d of %d, ksceBtStartConnect returned 0x%08x", gr_attempts, MAX_ATTEMPTS, (unsigned)r);
	gr_phase = PH_RETRY;
	gr_phase_at = now;
}

void btt_btreconn_sync(int on)
{
	uint64_t now = ksceKernelGetSystemTimeWide();

	if (!on) {
		gr_failed = 0;                       /* a failed install may be tried again after off and on */
		btreconn_rec = 0;
		btreconn_armed = 0;
		if (gr_phase != PH_IDLE)
			rc_done("switched off - stopped");
		return;
	}
	if (gr_failed)
		return;
	if ((gr_hook < 0 || gr_sev < 0) && rc_install() < 0) {
		gr_failed = 1;
		return;
	}
	if (!btreconn_armed) {
		btreconn_armed = 1;
		btreconn_rec = 1;
		gr_wakes_seen = btreconn_wakes;      /* wake-ups while it was off are not ours */
	}
	if (btreconn_wakes != gr_wakes_seen) {
		gr_wakes_seen = btreconn_wakes;
		/* a new wake-up, unless it is another event of the one in hand */
		if (gr_phase == PH_IDLE && (!gr_session_at || now - gr_session_at >= WAKE_MERGE_US)) {
			gr_phase = PH_WAIT_UP;
			gr_phase_at = gr_session_at = now;
			gr_attempts = 0;
			gr_rec_at_wake = btreconn_recorded;
			BTT_KLOG("bluetooth reconnect: the console woke from standby - waiting for Bluetooth");
		}
	}
	rc_step(now);
}

void btt_btreconn_restore(void)
{
	btreconn_rec = 0;
	btreconn_armed = 0;
	gr_phase = PH_IDLE;
	if (gr_sev >= 0) {
		ksceKernelUnregisterSysEventHandler(gr_sev);
		gr_sev = -1;
	}
	if (gr_hook >= 0) {
		taiHookReleaseForKernel(gr_hook, btreconn_ref);
		gr_hook = -1;
	}
	for (int i = 0; i < 100 && btreconn_in; i++)
		ksceKernelDelayThread(1000);
	btreconn_ref = 0;
}
