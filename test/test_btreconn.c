/*
 * test_btreconn.c - host test of Reconnect After Standby: the hook body that notes the headphones' address and
 * the standby handler (kernel/btreconn_hook.c) and the worker side (kernel/btreconn.c), compiled as they are,
 * with the console's calls replaced by fakes and a clock the test advances by half a second per worker pass.
 *
 *   make test      (cc -Itest/host test/test_btreconn.c, then run)
 *
 * What is proved:
 *   - Off (the default) does nothing at all: no lookup, no hook, no standby handler, no call of the connect
 *     function, whatever happens (wake-ups included, which are not even counted);
 *   - On puts the hook in once, on SceAVConfig's import of the start-of-sound call (the right library and
 *     function numbers), registers one standby handler, and finds the three Bluetooth calls by number;
 *   - the hook runs the original with every argument as it came and returns its answer; it notes the address
 *     only for a successful call that starts A2DP, never for another profile or a failed call; with no
 *     reference set it calls nothing;
 *   - after a wake-up the worker waits until Bluetooth is up, then 3 s, and only if the noted device has no link
 *     calls the connect function, with that device's address, at most twice (the second 5 s after the first),
 *     never a third time, whatever the answers; a device that has a link at any check, headphones that come back by
 *     themselves, sound starting on another device, Bluetooth that never comes up, no device noted and Off
 *     all end it with no call;
 *   - several resume events of one wake-up (and any within 30 s of its start) are one wake-up; a later one is a
 *     new wake-up with its own two attempts; a resume event that is not a resume (resume == 0) counts for nothing;
 *   - the three Bluetooth calls are made at the raised priority and the worker's own priority is back after each;
 *   - a missing call, a refused hook or a refused handler turn it off, once, until it is switched off and on;
 *   - module stop takes the hook and the handler out and notes nothing after.
 */
#define BTT_HOST_TAI 1
#include <btt_host.h>
#include <taihen.h>
#include <stdlib.h>

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

/* ---- fakes -------------------------------------------------------------------------------- */

static uint64_t g_now;
uint64_t ksceKernelGetSystemTimeWide(void) { return g_now; }
int ksceKernelDelayThread(SceUInt delay) { return 0; }
static int g_prio = 0x70;
int ksceKernelChangeThreadPriority(SceUID thid, int priority) { CHECK(thid == 0, "another thread's priority"); g_prio = priority; return 0; }
void btt_klog_line(const char *line)
{
	if (getenv("BTT_TEST_VERBOSE"))
		printf("    [%6.1f] %s\n", (double)g_now / 1e6, line);
}

/* the Bluetooth code's three calls */
static int g_bt_config = 8, g_state = 1, g_connect_ret, g_lookups_missing;
static int g_n_config, g_n_state, g_n_connect;
static unsigned g_connect_lo[8], g_connect_hi[8];
static int fake_config(void) { CHECK(g_prio == 0x40, "config call at priority 0x%x", g_prio); g_n_config++; return g_bt_config; }
static int fake_state(unsigned lo, unsigned hi) { CHECK(g_prio == 0x40, "state call at priority 0x%x", g_prio); g_n_state++; return g_state; }
static int fake_connect(unsigned lo, unsigned hi)
{
	CHECK(g_prio == 0x40, "connect call at priority 0x%x", g_prio);
	CHECK(g_n_connect < 8, "too many connect calls");
	g_connect_lo[g_n_connect] = lo; g_connect_hi[g_n_connect] = hi;
	g_n_connect++;
	return g_connect_ret;
}
static int g_lookups;
int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func)
{
	g_lookups++;
	CHECK(pid == KERNEL_PID && !strcmp(modname, "SceBt") && libnid == 0xD48CA62Du, "wrong export lookup");
	if (g_lookups_missing)
		return -1;
	switch (funcnid) {
	case 0x6059113Au: *func = (uintptr_t)fake_connect; return 0;
	case 0xE9936446u: *func = (uintptr_t)fake_state; return 0;
	case 0x94C66A3Bu: *func = (uintptr_t)fake_config; return 0;
	}
	CHECK(0, "an export nobody expects: 0x%08x", funcnid);
	return -1;
}

/* the hook: the original start-of-sound call */
static struct { int calls; uint32_t a, b, c, d; } g_orig;
static int g_orig_ret;
static int fake_start_audio(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	g_orig.calls++; g_orig.a = a; g_orig.b = b; g_orig.c = c; g_orig.d = d;
	return g_orig_ret;
}
static int g_hooks, g_hook_fail, g_releases;
static struct { const char *module; uint32_t lib, func; const void *hook; } g_hook_args;
SceUID taiHookFunctionImportForKernel(SceUID pid, tai_hook_ref_t *ref, const char *module,
                                      uint32_t lib_nid, uint32_t func_nid, const void *hook)
{
	CHECK(pid == KERNEL_PID, "wrong pid");
	g_hooks++;
	g_hook_args.module = module; g_hook_args.lib = lib_nid; g_hook_args.func = func_nid; g_hook_args.hook = hook;
	if (g_hook_fail)
		return -1;
	*ref = (tai_hook_ref_t)fake_start_audio;
	return 0x1000;
}
int taiHookReleaseForKernel(SceUID uid, tai_hook_ref_t ref) { CHECK(uid == 0x1000, "released another hook"); g_releases++; return 0; }

/* the standby handler */
static int g_sev_regs, g_sev_unregs, g_sev_fail;
static SceSysEventHandler g_sev_cb;
int ksceKernelRegisterSysEventHandler(const char *name, SceSysEventHandler handler, void *args)
{
	CHECK(!strncmp(name, "BluetoothTurbo", 14), "handler name '%s' is not the product's", name);
	g_sev_regs++;
	if (g_sev_fail)
		return -1;
	g_sev_cb = handler;
	return 0x2000;
}
int ksceKernelUnregisterSysEventHandler(SceUID uid) { CHECK(uid == 0x2000, "wrong handler released"); g_sev_unregs++; g_sev_cb = 0; return 0; }

#include "../kernel/btreconn_hook.c"
#include "../kernel/btreconn.c"

/* ---- helpers ------------------------------------------------------------------------------- */

static void reset(void)
{
	g_now = 100ull * 1000000;
	g_prio = 0x70;
	g_bt_config = 8; g_state = 1; g_connect_ret = 0; g_lookups_missing = 0;
	g_n_config = g_n_state = g_n_connect = 0;
	memset(&g_orig, 0, sizeof(g_orig));
	g_orig_ret = 0;
	g_hooks = g_hook_fail = g_releases = g_sev_regs = g_sev_unregs = g_sev_fail = g_lookups = 0;
	g_sev_cb = 0;
	/* the module's own state, as at module load */
	gr_start_connect = 0; gr_get_conninfo = 0; gr_get_config = 0;
	gr_hook = -1; gr_sev = -1; gr_failed = 0; gr_phase = PH_IDLE; gr_phase_at = gr_session_at = 0; gr_attempts = 0;
	gr_wakes_seen = gr_rec_at_wake = 0;
	btreconn_ref = 0; btreconn_in = btreconn_rec = btreconn_armed = 0;
	btreconn_mac0 = btreconn_mac1 = btreconn_recorded = btreconn_wakes = 0;
}

/* worker passes (every half second) for `ms` milliseconds, with the setting `on` */
static void run(int on, int ms)
{
	for (int t = 0; t < ms; t += 500) {
		g_now += 500 * 1000ull;
		btt_btreconn_sync(on);
	}
}
static void resume_event(void) { CHECK(g_sev_cb != 0, "no handler registered"); g_sev_cb(1, 0x100001, 0, 0); }

/* the console starts sound on a device: SceAVConfig's call of the hooked import */
static int start_audio(uint32_t lo, uint32_t hi, uint32_t flags)
{
	CHECK(gr_hook >= 0, "the hook is not in");
	return btreconn_hook(lo, hi, flags, 0x55);
}

#define MAC0 0x11223344u
#define MAC1 0x5566u

/* in, with the headphones noted */
static void armed_with_headphones(void)
{
	reset();
	run(1, 1000);
	CHECK(start_audio(MAC0, MAC1, 8) >= 0, "start_audio");
}

/* ---- tests ----------------------------------------------------------------------------------- */

static void test_off(void)
{
	reset();
	run(0, 60000);
	CHECK(g_lookups == 0 && g_hooks == 0 && g_sev_regs == 0 && g_n_connect == 0 && g_n_config == 0 && g_n_state == 0,
	      "Off touched the system: %d lookups, %d hooks, %d handlers", g_lookups, g_hooks, g_sev_regs);
	CHECK(btreconn_rec == 0 && btreconn_armed == 0, "Off is armed");
	btt_btreconn_restore();
	CHECK(g_releases == 0 && g_sev_unregs == 0, "a stop with nothing in released something");
}

static void test_install(void)
{
	reset();
	run(1, 5000);
	CHECK(g_hooks == 1 && g_sev_regs == 1 && g_lookups == 3, "installs: %d hooks, %d handlers, %d lookups", g_hooks, g_sev_regs, g_lookups);
	CHECK(!strcmp(g_hook_args.module, "SceAVConfig") && g_hook_args.lib == 0xD48CA62Du && g_hook_args.func == 0x8D47CABDu &&
	      g_hook_args.hook == (const void *)btreconn_hook, "hooked the wrong import");
	CHECK(btreconn_rec == 1 && btreconn_armed == 1, "not armed");
	CHECK(g_n_connect == 0, "connected with nothing to connect");
	/* switched off: disarmed, the hook stays in, nothing is noted */
	run(0, 2000);
	CHECK(btreconn_rec == 0 && g_hooks == 1 && g_releases == 0, "off: the hook went out or stayed armed");
	CHECK(start_audio(MAC0, MAC1, 8) >= 0 && btreconn_recorded == 0, "Off noted an address");
	/* on again: no second install */
	run(1, 2000);
	CHECK(g_hooks == 1 && g_sev_regs == 1 && btreconn_rec == 1, "on again installed again");
	btt_btreconn_restore();
	CHECK(g_releases == 1 && g_sev_unregs == 1 && btreconn_rec == 0 && btreconn_armed == 0, "stop: %d releases, %d handlers out", g_releases, g_sev_unregs);
	btt_btreconn_restore();
	CHECK(g_releases == 1 && g_sev_unregs == 1, "stop twice released twice");
}

static void test_hook(void)
{
	reset();
	run(1, 1000);
	g_orig_ret = 0x1234;
	CHECK(start_audio(MAC0, MAC1, 8) == 0x1234, "the original's answer was not returned");
	CHECK(g_orig.calls == 1 && g_orig.a == MAC0 && g_orig.b == MAC1 && g_orig.c == 8 && g_orig.d == 0x55, "arguments changed on the way");
	CHECK(btreconn_recorded == 1 && btreconn_mac0 == MAC0 && btreconn_mac1 == MAC1, "address not noted");
	/* another profile (no A2DP flag): not noted */
	start_audio(0xAAAA, 0xBB, 0x10);
	CHECK(btreconn_recorded == 1 && btreconn_mac0 == MAC0, "a non-A2DP call was noted");
	/* a failed call: not noted */
	g_orig_ret = (int)0x802F0D01;
	start_audio(0xCCCC, 0xDD, 8);
	CHECK(btreconn_recorded == 1 && btreconn_mac0 == MAC0, "a failed call was noted");
	/* a later successful call replaces it */
	g_orig_ret = 0;
	start_audio(0x1, 0x2, 8 | 1);
	CHECK(btreconn_recorded == 2 && btreconn_mac0 == 1 && btreconn_mac1 == 2, "a newer device was not noted");
	CHECK(btreconn_in == 0, "left the in-hook flag set");
	/* no reference: nothing is called */
	tai_hook_ref_t keep = btreconn_ref;
	btreconn_ref = 0;
	int calls = g_orig.calls;
	CHECK(btreconn_hook(5, 6, 8, 0) == (int)0x802F0D01 && g_orig.calls == calls, "called through with no reference");
	btreconn_ref = keep;
}

static void wake(void) { resume_event(); }

static void test_session_two_attempts(void)
{
	armed_with_headphones();
	g_bt_config = 0;                 /* Bluetooth is down at the wake-up */
	wake();
	run(1, 5000);
	CHECK(g_n_connect == 0 && g_n_state == 0, "acted before Bluetooth was up");
	g_bt_config = 8;
	run(1, 2000);                    /* up; 2 s of the 3 */
	CHECK(g_n_connect == 0, "connected before the 3 s");
	run(1, 1500);                    /* 3.5 s after it was seen */
	CHECK(g_n_connect == 1 && g_connect_lo[0] == MAC0 && g_connect_hi[0] == MAC1, "first attempt: %d calls", g_n_connect);
	uint64_t t1 = g_now;
	run(1, 4000);
	CHECK(g_n_connect == 1, "second attempt before 5 s");
	run(1, 1500);
	CHECK(g_n_connect == 2 && g_now - t1 >= 5000000ull, "second attempt: %d calls", g_n_connect);
	CHECK(g_connect_lo[1] == MAC0 && g_connect_hi[1] == MAC1, "second attempt to another device");
	run(1, 60000);
	CHECK(g_n_connect == 2, "a third attempt: %d calls", g_n_connect);
	CHECK(gr_phase == PH_IDLE, "the session did not end");
	CHECK(g_prio == 0x70, "the worker's priority is not back");
}

static void test_ends_without_call(void)
{
	/* a link at the first check: the headphones came back by themselves */
	for (int st = 2; st <= 6; st++) {
		armed_with_headphones();
		g_state = st;
		wake();
		run(1, 30000);
		CHECK(g_n_connect == 0 && gr_phase == PH_IDLE, "state %d: %d connect calls", st, g_n_connect);
	}
	/* a link after the first attempt: no second */
	armed_with_headphones();
	wake();
	run(1, 4000);
	CHECK(g_n_connect == 1, "first attempt: %d", g_n_connect);
	g_state = 5;
	run(1, 30000);
	CHECK(g_n_connect == 1, "a second attempt although the headphones connected");
	/* sound starts on another device meanwhile */
	armed_with_headphones();
	wake();
	run(1, 1500);
	start_audio(0x99, 0x77, 8);
	run(1, 30000);
	CHECK(g_n_connect == 0, "connected after sound started on another device");
	/* Bluetooth never comes up */
	armed_with_headphones();
	g_bt_config = 0;
	wake();
	run(1, 120000);
	CHECK(g_n_connect == 0 && gr_phase == PH_IDLE, "connected with Bluetooth down");
	/* no device noted */
	reset();
	run(1, 1000);
	wake();
	run(1, 30000);
	CHECK(g_n_connect == 0 && g_n_state == 0, "acted with no device noted");
	/* switched off in the middle */
	armed_with_headphones();
	wake();
	run(1, 1500);
	run(0, 30000);
	CHECK(g_n_connect == 0 && gr_phase == PH_IDLE, "connected after being switched off");
	/* the Bluetooth code cannot answer (a negative state): asked again, no attempt used */
	armed_with_headphones();
	g_state = (int)0x802F1503;
	wake();
	run(1, 8000);
	CHECK(g_n_connect == 0 && g_n_state > 1, "a negative state: %d connects, %d state calls", g_n_connect, g_n_state);
	g_state = 1;
	run(1, 3000);
	CHECK(g_n_connect == 1, "no attempt once the state could be read");
}

static void test_wake_bursts(void)
{
	armed_with_headphones();
	/* the power code sends several resume events for one wake-up, a few seconds apart */
	wake(); run(1, 500); wake(); wake(); run(1, 3000); wake(); run(1, 20000); wake(); run(1, 5000);
	CHECK(g_n_connect == 2, "one wake-up with many events made %d attempts", g_n_connect);
	/* a later wake-up has its own two */
	run(1, 60000);
	wake();
	run(1, 30000);
	CHECK(g_n_connect == 4, "the next wake-up made %d attempts in all", g_n_connect);
	/* an event that is not a resume counts for nothing */
	int n = g_n_connect;
	run(1, 60000);
	g_sev_cb(0, 0x201, 0, 0);
	run(1, 30000);
	CHECK(g_n_connect == n, "a non-resume event started an attempt");
	/* wake-ups while Off are not counted, and not acted on when it goes on */
	run(0, 2000);
	CHECK(btreconn_armed == 0, "armed while off");
	uint32_t w = btreconn_wakes;
	g_sev_cb(1, 0x100001, 0, 0);
	CHECK(btreconn_wakes == w, "counted a wake-up while Off");
	run(1, 30000);
	CHECK(g_n_connect == n, "acted on a wake-up that came while Off");
}

static void test_failures(void)
{
	reset();
	g_lookups_missing = 1;
	run(1, 20000);
	CHECK(g_hooks == 0 && g_sev_regs == 0 && g_lookups == 1, "missing call: %d hooks, %d lookups", g_hooks, g_lookups);
	run(0, 1000);
	g_lookups_missing = 0;
	run(1, 5000);
	CHECK(g_hooks == 1 && g_sev_regs == 1, "not tried again after off and on");

	reset();
	g_hook_fail = 1;
	run(1, 20000);
	CHECK(g_hooks == 1 && g_sev_regs == 0 && btreconn_armed == 0, "refused hook: %d hooks, %d handlers", g_hooks, g_sev_regs);
	CHECK(g_n_connect == 0, "connected with no hook");

	reset();
	g_sev_fail = 1;
	run(1, 20000);
	CHECK(g_hooks == 1 && g_sev_regs == 1 && btreconn_armed == 0, "refused handler: armed anyway");
	btt_btreconn_restore();
	CHECK(g_releases == 1, "a hook that got in was not released at stop");
	/* off and on again: finishes the half-done install, no second hook */
	run(0, 1000);
	g_sev_fail = 0;
	gr_hook = 0x1000;   /* restore released it: as it was before the stop */
	gr_sev = -1;
	run(1, 3000);
	CHECK(g_hooks == 1 && g_sev_regs == 2 && btreconn_armed == 1, "the half-done install was not finished");
}

int main(void)
{
	test_off();
	test_install();
	test_hook();
	test_session_two_attempts();
	test_ends_without_call();
	test_wake_bursts();
	test_failures();
	printf("test_btreconn: ok\n");
	return 0;
}
