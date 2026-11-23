/*
 * btldac.c - the LDAC engine (see btldac.h): a kernel thread of its own, the encoder, the self-test and the crash record. The design is the AAC engine's (btaac.c):
 *   - The thread is made at module start and pinned to the system core (mask 0x80000); a thread cannot be made from inside a system call.
 *   - The audio sender's hooks may not make a system call, so they cannot wake the thread: while a session runs it polls the PCM ring every 2 ms (a call is 2.667 ms of
 *     sound and takes a few tenths of a millisecond to encode); with no session it sleeps on an event flag.
 *   - libldac's integer build takes about 23 KB when an encoder is made and nothing while it encodes: its allocations are served from one static block (btldac_mem.c).
 *     The encoder is general-register code only (the build checks the object for vector and floating-point instructions), and runs in this thread alone.
 *   - A session runs the thread one priority below the audio sender's (btaac_core.h's rule), as the AAC engine does.
 *   - A console that goes down while a session or the self-test runs says so on the next start, from a record in ur0:data/bluetoothturbo/ldac.guard (btldac_core.h).
 *   - The adaptive mode asks Sony's adaptive code for a step every 20 ms with the number of packets waiting in the ring.
 */
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/io/stat.h>

#include <taihen.h>      /* KERNEL_PID only */

#include "../include/bluetoothturbo.h"
#include "btaac_core.h"  /* the priority rule (pure) */
#include "btldac.h"
#include "btldac_mem.h"
#include "klog.h"

#define BTLDAC_CPU_SYSTEM   0x80000u    /* the system core's affinity mask */
#define BTLDAC_PRIO         0x70        /* the Bluetooth Turbo worker's own, below the Bluetooth code's threads */
#define BTLDAC_STACK        0x4000u     /* the encoder's deepest path is about 3.7 KB */
#define BTLDAC_POLL_US      2000u
#define BTLDAC_CALL_US      2667u       /* 128 frames at 48 kHz */
#define BTLDAC_CATCHUP_US   500u        /* between two encodes of a backlog: the system core's other threads get a slice */
#define BTLDAC_TL_MAX       512
#define BTLDAC_PID_SYSTEM   0x10021
#define BTLDAC_QUIT_WAIT_US (5u * 1000u * 1000u)
#define BTLDAC_GUARD_PATH   BTT_DIR "/ldac.guard"

#define EV_CMD   0x1u
#define EV_QUIT  0x2u

#define CMD_START      0x1u
#define CMD_STOP       0x2u
#define CMD_SELFTEST   0x4u

static SceUID g_th = -1, g_ev = -1;
static volatile uint32_t g_quit;
static volatile uint32_t g_cmds;
static volatile uint32_t g_state = BTLDAC_S_IDLE;
static volatile int32_t  g_last_error;
static volatile uint32_t g_thread_err, g_cpu_mask, g_cpu_mask_err;

static BtLdacGuard g_guard;
static BtLdacBoot  g_boot;
static volatile uint32_t g_guard_ok;
static volatile uint32_t g_hold, g_crashes, g_banned;
static volatile uint32_t g_selftest, g_selftest_asked, g_selftest_runs;
static volatile uint32_t g_st_us[2], g_st_pkts[2], g_st_bytes[2];
static volatile uint64_t g_st_hash[2];

static struct { uint32_t mode, mtu; } g_req;
static volatile uint32_t g_mode_now;             /* the mode asked for (a running session follows it) */
static volatile uint32_t g_s_mode, g_s_mtu;
static volatile uint32_t g_enc_calls, g_enc_sum_us, g_enc_max_us, g_slow, g_pcm_max;
static volatile uint32_t g_s_eqmid, g_s_kbps, g_abr_calls, g_abr_down, g_abr_up;

static volatile uint32_t g_prio_override;
static volatile uint32_t g_prio_want, g_prio_src, g_prio_actual, g_prio_sender, g_prio_bt, g_prio_fail;
static int g_prio_applied;
static SceUID g_tl_ids[BTLDAC_TL_MAX];

static BtLdacEnc g_enc;

/* ---- the thread's priority ------------------------------------------------------------------------------------ */

static void prio_read(int *sender, int *bt)
{
	SceKernelThreadInfo ti;
	int cnt = 0;

	*sender = *bt = -1;
	memset(g_tl_ids, 0, sizeof(g_tl_ids));
	if (ksceKernelGetThreadIdList(BTLDAC_PID_SYSTEM, g_tl_ids, (int)sizeof(g_tl_ids), &cnt) < 0)
		return;
	if (cnt > BTLDAC_TL_MAX)
		cnt = BTLDAC_TL_MAX;
	for (int i = 0; i < cnt; i++) {
		memset(&ti, 0, sizeof(ti));
		ti.size = sizeof(ti);
		if (g_tl_ids[i] <= 0 || ksceKernelGetThreadInfo(g_tl_ids[i], &ti) < 0)
			continue;
		ti.name[sizeof(ti.name) - 1] = 0;
		btaac_prio_note(ti.name, ti.currentPriority, sender, bt);
	}
}

static void prio_apply(void)
{
	int sp, bp, r;
	uint32_t src, want;

	prio_read(&sp, &bp);
	want = btaac_prio_choose(g_prio_override, sp, &src);
	g_prio_sender = sp < 0 ? 0u : (uint32_t)sp;
	g_prio_bt = bp < 0 ? 0u : (uint32_t)bp;
	g_prio_want = want;
	g_prio_src = src;
	BTT_KLOG("ldac: engine thread priority 0x%x (%s; the audio sender's is %s)", (unsigned)want,
	         src == BTAAC_PRIO_SRC_OVERRIDE ? "set by hand" : src == BTAAC_PRIO_SRC_COMPUTED ? "one below the sender's" : "the module's own: the sender's priority was not found",
	         sp < 0 ? "not found" : "found");
	r = ksceKernelChangeThreadPriority(0, (int)want);
	g_prio_fail = r < 0 ? (uint32_t)r : 0u;
	g_prio_applied = r >= 0 && want != BTLDAC_PRIO;
	g_prio_actual = (uint32_t)ksceKernelGetThreadCurrentPriority();
	if (r < 0)
		BTT_KLOG("ldac: the engine's priority change to 0x%x was refused (0x%08x): the thread stays at 0x%x", (unsigned)want, (unsigned)r, (unsigned)g_prio_actual);
}

static void prio_restore(void)
{
	if (g_prio_applied)
		ksceKernelChangeThreadPriority(0, (int)BTLDAC_PRIO);
	g_prio_applied = 0;
}

/* ---- the crash record --------------------------------------------------------------------------------------- */

static int guard_write(void)
{
	uint8_t b[BTLDAC_G_BYTES];
	SceUID fd;
	int ok;

	btldac_guard_encode(&g_guard, b);
	ksceIoMkdir(BTT_DIR, 0777);
	fd = ksceIoOpen(BTLDAC_GUARD_PATH, SCE_O_WRONLY | SCE_O_CREAT, 0666);
	if (fd < 0)
		return 0;
	ok = ksceIoWrite(fd, b, sizeof(b)) == (int)sizeof(b);
	ksceIoClose(fd);
	return ok;
}

static void guard_publish(void)
{
	g_crashes = g_guard.count;
	g_hold = g_boot.hold || !g_guard_ok;
	g_banned = (uint32_t)g_boot.banned;
}

static void guard_load(void)
{
	uint8_t b[BTLDAC_G_BYTES];
	SceUID fd = ksceIoOpen(BTLDAC_GUARD_PATH, SCE_O_RDONLY, 0);
	int n = 0;

	if (fd >= 0) {
		n = ksceIoRead(fd, b, sizeof(b));
		ksceIoClose(fd);
	}
	if (!btldac_guard_decode(&g_guard, b, n) && n > 0)
		BTT_KLOG("ldac: the crash record is damaged (%d bytes): starting it afresh", n);
	g_boot = btldac_guard_boot(&g_guard);
	g_guard_ok = (uint32_t)guard_write();   /* the record as it stands goes back at once: a record that cannot be written protects nothing */
	guard_publish();
	if (g_boot.hold_now)
		BTT_KLOG("ldac: the last LDAC session did not end cleanly (%u in a row): no LDAC this start", (unsigned)g_guard.count);
	else if (g_boot.hold)
		BTT_KLOG("ldac: held off by the crash record (%u in a row, test byte '%c')", (unsigned)g_guard.count, g_guard.test);
	if (!g_guard_ok)
		BTT_KLOG("ldac: the crash record cannot be written: no LDAC this start");
}

/* ---- the self-test ------------------------------------------------------------------------------------------ */

static void selftest_run(void)
{
	BtLdacRun run[2];
	uint32_t m, verdict = BTLDAC_ST_PASS;
	int ran[2] = { 0, 0 };

	if (g_state == BTLDAC_S_RUNNING || g_state == BTLDAC_S_STARTING) {
		BTT_KLOG("ldac: self-test skipped: a session is running");
		return;
	}
	g_selftest_runs++;
	if (g_boot.banned || !g_guard_ok) {
		verdict = g_boot.banned ? BTLDAC_ST_FAIL : BTLDAC_ST_NORUN;
		goto done;
	}
	g_guard.test = 't';
	if (!guard_write()) {
		g_guard.test = 'K';
		verdict = BTLDAC_ST_NORUN;
		goto done;
	}
	for (m = 0; m < 2; m++) {
		uint64_t t0 = ksceKernelGetSystemTimeWide();
		int r = btldac_selftest_encode(m, &run[m]);

		g_st_us[m] = (uint32_t)(ksceKernelGetSystemTimeWide() - t0);
		ran[m] = r == 0;
		g_st_pkts[m] = ran[m] ? run[m].pkts : 0;
		g_st_bytes[m] = ran[m] ? run[m].bytes : 0;
		g_st_hash[m] = ran[m] ? run[m].hash : 0;
		if (!ran[m] || !btldac_run_pass(&run[m], m))
			verdict = BTLDAC_ST_FAIL;
	}
	g_guard.test = 'K';
	if (!guard_write() && !guard_write()) {
		/* a marker left on disk would be read at the next start as a crash that did not happen and ban the encoder for good */
		g_guard_ok = 0;
		guard_publish();
		verdict = BTLDAC_ST_NORUN;
		BTT_KLOG("ldac: the crash record could not be cleared after the self-test: no LDAC this start");
	}
done:
	g_selftest = verdict;
	BTT_KLOG("ldac: self-test %s: 330 kb/s %u us, %u packets, %u bytes (hash %08x%08x); 660 kb/s %u us, %u packets, %u bytes (hash %08x%08x)",
	         verdict == BTLDAC_ST_PASS ? "passed" : verdict == BTLDAC_ST_FAIL ? "FAILED" : "could not run",
	         (unsigned)g_st_us[0], (unsigned)g_st_pkts[0], (unsigned)g_st_bytes[0], (unsigned)(g_st_hash[0] >> 32), (unsigned)g_st_hash[0],
	         (unsigned)g_st_us[1], (unsigned)g_st_pkts[1], (unsigned)g_st_bytes[1], (unsigned)(g_st_hash[1] >> 32), (unsigned)g_st_hash[1]);
}

/* ---- the session -------------------------------------------------------------------------------------------- */

static void session_end(int state, int error)
{
	btldac_ring_close();
	prio_restore();
	btldac_enc_close(&g_enc);
	btldac_guard_session_end(&g_guard);
	if (!guard_write() && !guard_write()) {
		/* 'S' stays on disk: the next start would count a crash that did not happen; hold LDAC off for this start instead */
		g_guard_ok = 0;
		guard_publish();
		BTT_KLOG("ldac: the crash record could not be cleared at the end of the session: no LDAC this start");
	}
	g_crashes = g_guard.count;
	g_last_error = error;
	g_state = (uint32_t)state;
}

static void session_start(void)
{
	int err;

	if (g_hold || g_selftest != BTLDAC_ST_PASS) {
		g_last_error = BTLDAC_E_REFUSED;
		g_state = BTLDAC_S_FAILED;
		return;
	}
	err = btldac_enc_open(&g_enc, g_req.mode, g_req.mtu);
	if (err != BTLDAC_E_NONE) {
		g_last_error = err;
		g_state = BTLDAC_S_FAILED;
		BTT_KLOG("ldac: the encoder did not start (error %d, library code %d)", err, g_enc.err);
		return;
	}
	btldac_guard_session_begin(&g_guard);
	if (!guard_write()) {
		g_guard.session = 'K';   /* a session that could not be recorded would not be known to have crashed */
		btldac_enc_close(&g_enc);
		g_last_error = BTLDAC_E_RECORD;
		g_state = BTLDAC_S_FAILED;
		return;
	}
	g_enc_calls = g_enc_sum_us = g_enc_max_us = g_slow = g_pcm_max = 0;
	g_abr_calls = g_abr_down = g_abr_up = 0;
	g_s_mode = g_req.mode;
	g_s_mtu = g_req.mtu;
	g_s_eqmid = (uint32_t)g_enc.eqmid;
	g_s_kbps = 0;
	g_mode_now = g_req.mode;
	g_last_error = BTLDAC_E_NONE;
	BTT_KLOG("ldac: session started: mode %u (%s), media channel limit %u", (unsigned)g_req.mode, g_req.mode == BTLDAC_MODE_ABR ? "adaptive, from 330 kb/s" : g_req.mode == BTLDAC_MODE_660 ? "660 kb/s" : "330 kb/s",
	         (unsigned)g_req.mtu);
	prio_apply();   /* every log write of the session's start comes before the priority is raised */
	btldac_ring_open();
	g_state = BTLDAC_S_RUNNING;
}

/* Encodes every whole call's worth waiting while the packet ring has room. */
static void session_pump(void)
{
	static int16_t pcm[BTLDAC_FRAME * 2u] __attribute__((aligned(8)));
	static uint8_t out[BTLDAC_OUT_MAX] __attribute__((aligned(8)));
	static uint64_t last_abr;

	if (g_mode_now != g_enc.mode && g_mode_now < BTLDAC_MODE_COUNT) {   /* the setting changed while the session runs */
		if (btldac_enc_set_mode(&g_enc, g_mode_now) == BTLDAC_E_NONE) {
			g_s_mode = g_mode_now;
			g_s_eqmid = (uint32_t)g_enc.eqmid;
			last_abr = 0;
		}
	}
	for (;;) {
		uint32_t len = 0, nf = 0, us, avail = btldac_pcm_avail();
		uint64_t t0;
		int err;

		if (avail > g_pcm_max)
			g_pcm_max = avail;
		if (g_quit || avail < BTLDAC_FRAME || !btldac_pkt_room())
			break;
		btldac_pcm_take(pcm, BTLDAC_FRAME);
		t0 = ksceKernelGetSystemTimeWide();
		err = btldac_enc_call(&g_enc, pcm, out, &len, &nf);
		us = (uint32_t)(ksceKernelGetSystemTimeWide() - t0);
		g_enc_calls++;
		g_enc_sum_us += us;
		if (us > g_enc_max_us)
			g_enc_max_us = us;
		if (us > BTLDAC_CALL_US)
			g_slow++;
		if (err != BTLDAC_E_NONE) {
			BTT_KLOG("ldac: the session ended: encoder error %d (library code %d)", err, g_enc.err);
			session_end(BTLDAC_S_FAILED, err);
			return;
		}
		if (len) {
			btldac_pkt_put(out, len, nf);
			g_s_kbps = (uint32_t)g_enc.kbps;
		}
		if (g_enc.mode == BTLDAC_MODE_ABR && t0 / 1000u >= last_abr + BTLDAC_ABR_MS) {
			last_abr = t0 / 1000u;
			btldac_enc_abr(&g_enc, btldac_pkt_depth());
			g_s_eqmid = (uint32_t)g_enc.eqmid;
			g_abr_calls = g_enc.abr_calls;
			g_abr_down = g_enc.abr_steps_down;
			g_abr_up = g_enc.abr_steps_up;
		}
		if (btldac_pcm_avail() >= BTLDAC_FRAME)    /* a backlog: the thread, now at a high priority, gives the core's other threads a slice between calls */
			ksceKernelDelayThread(BTLDAC_CATCHUP_US);
	}
}

/* ---- requests ------------------------------------------------------------------------------------------------- */

static void post(uint32_t cmd)
{
	__atomic_or_fetch(&g_cmds, cmd, __ATOMIC_ACQ_REL);
	if (g_ev >= 0)
		ksceKernelSetEventFlag(g_ev, EV_CMD);
}

int btldac_start(uint32_t mode, uint32_t mtu)
{
	uint32_t expect;

	if (g_th < 0)
		return BTLDAC_ERR_THREAD;
	if (mode >= BTLDAC_MODE_COUNT)
		return BTLDAC_ERR_ARG;
	if (g_hold)
		return BTLDAC_ERR_HELD;
	if (g_selftest != BTLDAC_ST_PASS)
		return BTLDAC_ERR_NOTEST;
	if (!btldac_mtu_ok(mtu))
		return BTLDAC_ERR_MTU;
	expect = BTLDAC_S_IDLE;
	if (!__atomic_compare_exchange_n(&g_state, &expect, (uint32_t)BTLDAC_S_STARTING, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		expect = BTLDAC_S_FAILED;                 /* the last session ended badly: a new one may be asked for */
		if (!__atomic_compare_exchange_n(&g_state, &expect, (uint32_t)BTLDAC_S_STARTING, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return BTLDAC_ERR_BUSY;
	}
	g_req.mode = mode;
	g_req.mtu = mtu;
	post(CMD_START);
	return BTLDAC_OK;
}

void btldac_stop(void)
{
	if (g_th >= 0)
		post(CMD_STOP);
}

void btldac_set_mode(uint32_t mode)
{
	if (mode < BTLDAC_MODE_COUNT)
		g_mode_now = mode;
}

void btldac_sync(int on)
{
	if (on && g_th >= 0 && !g_selftest_asked) {
		g_selftest_asked = 1;
		post(CMD_SELFTEST);
	}
}

void btldac_get_info(BtLdacInfo *o)
{
	uint32_t calls = g_enc_calls, used, peak, allocs, refused;

	memset(o, 0, sizeof(*o));
	btldac_heap_stats(&used, &peak, &allocs, &refused);
	o->state = g_state;
	o->last_error = g_last_error;
	o->selftest = g_selftest;
	o->hold = g_hold;
	o->banned = g_banned;
	o->guard_ok = g_guard_ok;
	o->crashes = g_crashes;
	o->mode = g_s_mode;
	o->eqmid = g_s_eqmid;
	o->kbps = g_s_kbps;
	o->mtu = g_s_mtu;
	btldac_ring_info(&o->pcm_overruns, &o->pcm_frames_in, &o->pkts_popped, &o->pkt_drops, &o->pkts_out);
	o->slow_calls = g_slow;
	o->enc_max_us = g_enc_max_us;
	o->enc_mean_us = calls ? g_enc_sum_us / calls : 0;
	o->thread_err = g_thread_err;
	o->cpu_mask = g_cpu_mask;
	o->cpu_mask_err = g_cpu_mask_err;
	o->prio = g_prio_actual;
	o->prio_src = g_prio_src;
	o->pcm_max_frames = g_pcm_max;
	o->abr_calls = g_abr_calls;
	o->abr_down = g_abr_down;
	o->abr_up = g_abr_up;
	o->heap_peak = peak;
	o->heap_refused = refused;
	for (int i = 0; i < 2; i++) {
		o->st_us[i] = g_st_us[i];
		o->st_pkts[i] = g_st_pkts[i];
		o->st_bytes[i] = g_st_bytes[i];
		o->st_hash[2 * i] = (uint32_t)(g_st_hash[i] >> 32);
		o->st_hash[2 * i + 1] = (uint32_t)g_st_hash[i];
	}
}

/* ---- the thread ----------------------------------------------------------------------------------------------- */

static int worker_main(SceSize args, void *argp)
{
	(void)args;
	(void)argp;
	for (;;) {
		uint32_t cmds = __atomic_exchange_n(&g_cmds, 0u, __ATOMIC_ACQ_REL);
		unsigned int bits = 0;

		if (g_quit)
			break;
		if ((cmds & CMD_START) && g_state == BTLDAC_S_STARTING)
			session_start();
		if ((cmds & CMD_STOP) && g_state == BTLDAC_S_RUNNING)
			session_end(BTLDAC_S_IDLE, BTLDAC_E_NONE);
		if (cmds & CMD_SELFTEST)
			selftest_run();
		if (g_state == BTLDAC_S_RUNNING) {
			session_pump();
			if (g_quit)
				break;
			ksceKernelDelayThread(BTLDAC_POLL_US);
			continue;
		}
		if (ksceKernelWaitEventFlag(g_ev, EV_CMD | EV_QUIT, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, &bits, NULL) < 0) {
			g_last_error = BTLDAC_E_WAIT;
			BTT_KLOG("ldac: the thread's wait failed: the engine stops");
			break;
		}
	}
	if (g_state == BTLDAC_S_RUNNING)
		session_end(BTLDAC_S_IDLE, BTLDAC_E_NONE);    /* the module is stopping: the session ends cleanly */
	return 0;
}

void btldac_init(void)
{
	g_th = g_ev = -1;
	guard_load();
	g_ev = ksceKernelCreateEventFlag("BluetoothTurboLdac", 0, 0, NULL);
	if (g_ev < 0) {
		g_thread_err = (uint32_t)g_ev;
		g_ev = -1;
		BTT_KLOG("ldac: no event flag (0x%08x): no LDAC", (unsigned)g_thread_err);
		return;
	}
	g_cpu_mask = BTLDAC_CPU_SYSTEM;
	g_th = ksceKernelCreateThread("BluetoothTurboLdac", worker_main, BTLDAC_PRIO, BTLDAC_STACK, 0, BTLDAC_CPU_SYSTEM, NULL);
	if (g_th < 0) {
		g_cpu_mask_err = (uint32_t)g_th;
		g_cpu_mask = 0;
		BTT_KLOG("ldac: the system core's mask was refused (0x%08x): default placement", (unsigned)g_cpu_mask_err);
		g_th = ksceKernelCreateThread("BluetoothTurboLdac", worker_main, BTLDAC_PRIO, BTLDAC_STACK, 0, 0, NULL);
	}
	if (g_th < 0 || ksceKernelStartThread(g_th, 0, NULL) < 0) {
		g_thread_err = (uint32_t)(g_th < 0 ? g_th : -2);
		BTT_KLOG("ldac: the engine's thread could not be made (0x%08x): no LDAC", (unsigned)g_thread_err);
		if (g_th >= 0)
			ksceKernelDeleteThread(g_th);
		g_th = -1;
		ksceKernelDeleteEventFlag(g_ev);
		g_ev = -1;
		return;
	}
	BTT_KLOG("ldac: engine ready (thread mask 0x%x)", (unsigned)g_cpu_mask);
}

void btldac_exit(void)
{
	if (g_th >= 0) {
		unsigned int timeout = BTLDAC_QUIT_WAIT_US;

		g_quit = 1;
		ksceKernelSetEventFlag(g_ev, EV_QUIT);
		if (ksceKernelWaitThreadEnd(g_th, NULL, &timeout) >= 0) {
			ksceKernelDeleteThread(g_th);
			if (g_ev >= 0)
				ksceKernelDeleteEventFlag(g_ev);   /* only once the thread is gone */
			g_ev = -1;
		} else {
			BTT_KLOG("ldac: the thread did not end within %u s", (unsigned)(BTLDAC_QUIT_WAIT_US / 1000000u));
		}
		g_th = -1;
		if (g_state == BTLDAC_S_STARTING)
			g_state = BTLDAC_S_IDLE;   /* a start that was queued and never taken */
	}
}
