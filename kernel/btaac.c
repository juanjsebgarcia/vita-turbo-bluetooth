/*
 * btaac.c - the AAC engine (see btaac.h): a kernel thread of its own, the encoder in two builds, the self-test and the crash record.
 *
 * Why every piece is the way it is:
 *   - The thread is made at module start and pinned to the system core (mask 0x80000: the console's own cores are left to games);
 *     if the kernel refuses that mask the default placement is used and the log says so. A thread made from inside a system call
 *     cannot be given a kernel entry point (it failed with -1 on the console), so no session ever makes one.
 *   - The audio sender's hooks may not make a system call, so they cannot wake the thread: while a session runs the thread polls
 *     the PCM ring every BTAAC_POLL_US (2 ms: an access unit is 21.3 ms of audio and takes about 5 ms to encode, so the wait
 *     before an encode starts is small against both, and 500 wake-ups a second on the system core cost little). With no session
 *     it sleeps on an event flag, which only a request wakes.
 *   - The encoder is integer only and has no writable globals; its 8 allocations (48,928 bytes in all, every one at start-up)
 *     are served from one 64 KB kernel block, made when a session starts and freed when it ends, by a bump allocator.
 *     The memory operator is handed to the encoder by pointer and used for the whole session (Uninit included): it lives in the
 *     encoder's own record below, never on a stack.
 *   - The plain C build and the NEON build are one module (CMakeLists.txt gives each its symbol prefix). NEON runs only here, in
 *     this thread: the system saves vector state per thread, and a hook in someone else's thread must never touch it. A start-up
 *     self-test encodes a fixed signal with both builds; NEON is used only when the bytes are the same.
 *   - A console that goes down while a session or a self-test runs says so on the next start, from a record kept in
 *     ur0:data/bluetoothturbo/aac.guard (btaac_core.h): the build that was running is not run again by itself.
 */
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/io/stat.h>

#include <taihen.h>      /* KERNEL_PID only */

#include "../include/bluetoothturbo.h"
#include "../third_party/aacenc/common/include/voAAC.h"
#include "btaac.h"
#include "btaac_latm.h"
#include "klog.h"

#define BTAAC_CPU_SYSTEM   0x80000      /* the system core's affinity mask */
#define BTAAC_PRIO         0x70         /* the Bluetooth Turbo worker's own, below the Bluetooth code's threads */
#define BTAAC_STACK        0x4000u      /* the encoder's deepest path is about 3 KB */
#define BTAAC_HEAP         0x10000u     /* the encoder takes 48,928 bytes of it */
#define BTAAC_POLL_US      2000u
#define BTAAC_FRAME_US     21333u       /* 1024 samples at 48 kHz */
#define BTAAC_CATCHUP_US   500u         /* between two encodes of a backlog (after the engine was held off): the system core's other threads get a slice */
#define BTAAC_LATE_US      8000u        /* a 2 ms sleep that took 8 ms more than that is noted (development builds) */
#define BTAAC_TL_MAX       512          /* threads read when looking for the sender's priority */
#define BTAAC_PID_SYSTEM   0x10021      /* the thread list of the whole system (the kernel's own pid 0x10005 is refused by that call) */
#define BTAAC_QUIT_WAIT_US (5u * 1000u * 1000u)
#define BTAAC_ST_KBPS      256u
#define BTAAC_ST_RATE      48000u
#define BTAAC_GUARD_PATH   BTT_DIR "/aac.guard"

#define EV_CMD   0x1u
#define EV_QUIT  0x2u

#define CMD_START      0x1u
#define CMD_STOP       0x2u
#define CMD_SELFTEST   0x4u

/* The encoder's consistency checks (kernel/shim/assert.h) land here. */
volatile unsigned int btaac_assert_failed;

/* The two builds of the encoder (object files made by CMakeLists.txt, every symbol prefixed there). */
extern VO_S32 aacc_voGetAACEncAPI(VO_AUDIO_CODECAPI *api);
extern VO_S32 aacn_voGetAACEncAPI(VO_AUDIO_CODECAPI *api);
/* ... and our one function that reads the encoder's record (kernel/btaac_enclimit.c): the bit reservoir's size. 0 changed, 1 left as it was (already small enough), -1 not as expected. */
extern int aacc_btaac_enc_limit(void *h, unsigned int res_bytes);
extern int aacn_btaac_enc_limit(void *h, unsigned int res_bytes);

/* Needed by the NEON build's unwind annotations (never called). */
void __aeabi_unwind_cpp_pr1(void) {}

/* ---- the engine's state ----------------------------------------------------------------------------------- */

static SceUID g_th = -1, g_ev = -1;
static volatile uint32_t g_quit;
static volatile uint32_t g_cmds;                 /* CMD_* bits waiting for the thread */
static volatile uint32_t g_state = BTAAC_S_IDLE;
static volatile int32_t  g_last_error;
static volatile uint32_t g_thread_err, g_cpu_mask, g_cpu_mask_err;

static BtAacGuard g_guard;                       /* the record as it stands (thread and module start only) */
static BtAacBoot  g_boot;                        /* what this start may do with it */
static volatile uint32_t g_guard_ok;             /* the record can be written: without that no build is run */
static volatile uint32_t g_hold, g_crashes, g_neon_banned, g_aac_banned;
static volatile uint32_t g_selftest;             /* BtAacSelfTest */
static volatile uint32_t g_selftest_asked, g_selftest_runs;
static volatile uint32_t g_st_aus, g_st_bytes, g_st_c_us, g_st_n_us;
static volatile uint64_t g_st_hash_c, g_st_hash_n;

/* The session the thread was asked for. */
static struct { uint32_t bps, rate, channels, cap; int neon; } g_req;   /* cap: the longest access unit allowed, bytes; 0 = not bounded */

/* The running session, for the log and the readings. */
static volatile uint32_t g_s_neon, g_s_bps, g_s_rate, g_s_channels, g_s_cap, g_s_res, g_s_cap_state;
static volatile uint32_t g_enc_calls, g_enc_sum_us, g_enc_max_us, g_slow;
static volatile uint32_t g_pcm_max;              /* the most frames the PCM input ring held at once this session */

/* The thread's priority: asked for at the start of a session (one below the sender's: btaac_core.h), put back to the module's own at its end. */
static volatile uint32_t g_prio_override;        /* the development port's choice, 0 = computed (takes effect at the next session) */
static volatile uint32_t g_prio_want, g_prio_src, g_prio_actual, g_prio_sender, g_prio_bt, g_prio_fail;
static int g_prio_applied;                       /* engine thread only */
static SceUID g_tl_ids[BTAAC_TL_MAX];

/* ---- the thread's priority ------------------------------------------------------------------------------------ */

/* The sender's priority and the Bluetooth code's own threads' lowest, read from the thread manager's list now. Only from the engine's thread, at a session's start: it is a
 * system call that takes the thread manager's lock for a moment, never made in a hook. -1 for one not found (or the list not readable). */
static void prio_read(int *sender, int *bt)
{
	SceKernelThreadInfo ti;
	int cnt = 0;

	*sender = *bt = -1;
	memset(g_tl_ids, 0, sizeof(g_tl_ids));
	/* the size is in BYTES, the pid the system-wide one, and the ids copied are in `cnt` (the return value is the total, which may be more) */
	if (ksceKernelGetThreadIdList(BTAAC_PID_SYSTEM, g_tl_ids, (int)sizeof(g_tl_ids), &cnt) < 0)
		return;
	if (cnt > BTAAC_TL_MAX)
		cnt = BTAAC_TL_MAX;
	for (int i = 0; i < cnt; i++) {
		memset(&ti, 0, sizeof(ti));
		ti.size = sizeof(ti);
		if (g_tl_ids[i] <= 0 || ksceKernelGetThreadInfo(g_tl_ids[i], &ti) < 0)
			continue;
		ti.name[sizeof(ti.name) - 1] = 0;
		btaac_prio_note(ti.name, ti.currentPriority, sender, bt);
	}
}

/* At a session's start: the thread's priority is the one below the sender's (or the development port's, or the module's own when the sender's cannot be read). A refused change
 * leaves the thread where it is, and the log says so. */
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
	/* logged first: a log write is file I/O and must not be made at the raised priority */
	BTT_KLOG("aac: engine thread priority 0x%x (%s; the audio sender's is %s, the Bluetooth code's lowest %s)", (unsigned)want,
	         src == BTAAC_PRIO_SRC_OVERRIDE ? "set by hand" : src == BTAAC_PRIO_SRC_COMPUTED ? "one below the sender's" : "the module's own: the sender's priority was not found",
	         sp < 0 ? "not found" : "found", bp < 0 ? "not found" : "found");
	r = ksceKernelChangeThreadPriority(0, (int)want);
	g_prio_fail = r < 0 ? (uint32_t)r : 0u;
	g_prio_applied = r >= 0 && want != BTAAC_PRIO;
	g_prio_actual = (uint32_t)ksceKernelGetThreadCurrentPriority();
	if (r < 0)   /* the thread was not raised: this write is at the module's own priority */
		BTT_KLOG("aac: the engine's priority change to 0x%x was refused (0x%08x): the thread stays at 0x%x", (unsigned)want, (unsigned)r, (unsigned)g_prio_actual);
}

static void prio_restore(void)
{
	if (g_prio_applied)
		ksceKernelChangeThreadPriority(0, (int)BTAAC_PRIO);
	g_prio_applied = 0;
}

/* ---- the encoder ------------------------------------------------------------------------------------------- */

typedef struct Enc {
	VO_AUDIO_CODECAPI api;
	VO_HANDLE         h;
	VO_MEM_OPERATOR   mo;       /* the encoder keeps a pointer to this for its whole life */
	SceUID            blk;
	uint8_t          *heap;
	uint32_t          used, allocs, refused;   /* refused: an allocation did not fit the block */
	int               open;
} Enc;

static Enc  g_enc = { .blk = -1 };   /* the session's encoder */
static Enc *g_cur;                  /* the encoder being opened: its allocations come from this one's block */

static VO_U32 mo_alloc(VO_S32 id, VO_MEM_INFO *m)
{
	uint32_t size = ((uint32_t)m->Size + 7u) & ~7u;

	(void)id;
	if (!g_cur || m->Size < 0 || g_cur->used + size > BTAAC_HEAP) {
		if (g_cur)
			g_cur->refused = 1;
		m->VBuffer = 0;
		return 1;
	}
	m->VBuffer = g_cur->heap + g_cur->used;
	g_cur->used += size;
	g_cur->allocs++;
	return 0;
}
static VO_U32 mo_free(VO_S32 id, VO_PTR p) { (void)id; (void)p; return 0; }   /* the block goes whole, after Uninit */
static VO_U32 mo_set(VO_S32 id, VO_PTR p, VO_U8 v, VO_U32 n) { (void)id; memset(p, v, n); return 0; }
static VO_U32 mo_copy(VO_S32 id, VO_PTR d, VO_PTR s, VO_U32 n) { (void)id; memcpy(d, s, n); return 0; }
static VO_U32 mo_check(VO_S32 id, VO_PTR p, VO_U32 n) { (void)id; (void)p; (void)n; return 0; }
static VO_S32 mo_compare(VO_S32 id, VO_PTR a, VO_PTR b, VO_U32 n) { (void)id; return memcmp(a, b, n); }
static VO_U32 mo_move(VO_S32 id, VO_PTR d, VO_PTR s, VO_U32 n) { (void)id; memmove(d, s, n); return 0; }

static void enc_close(Enc *e)
{
	if (e->open && e->api.Uninit)
		e->api.Uninit(e->h);
	e->open = 0;
	if (e->blk >= 0)
		ksceKernelFreeMemBlock(e->blk);
	e->blk = -1;
	e->heap = 0;
}

/* Holds the encoder's bit reservoir to what `cap` (the longest access unit allowed, bytes) leaves over after the average frame (btaac_core.h). Returns a BTAAC_CAP_*; `*res`
 * is the reservoir in bytes that was asked for (0 when none was). With no cap the encoder is left exactly as it is: the self-test and the timing runs use none. */
static int enc_limit(Enc *e, int neon, uint32_t cap, uint32_t rate, uint32_t bps, uint32_t *res)
{
	int r;

	*res = 0;
	if (!cap)
		return BTAAC_CAP_NONE;
	*res = btaac_cap_reservoir(cap, bps, rate);
	if (!*res)
		return BTAAC_CAP_FAILED;
	r = neon ? aacn_btaac_enc_limit(e->h, *res) : aacc_btaac_enc_limit(e->h, *res);
	return r >= 0 ? BTAAC_CAP_APPLIED : BTAAC_CAP_FAILED;
}

/* Opens an encoder of one build for the rate, channels and bit rate, its access units held to `cap` bytes (0 = not held). 0, or a BTAAC_E_* code (the block is freed on a
 * failure). A cap that could not be applied is not a failure: the sender cuts a long unit into pieces; `*cap_state` says (BTAAC_CAP_*). */
static int enc_open(Enc *e, int neon, uint32_t rate, uint32_t channels, uint32_t bps, uint32_t cap, uint32_t *cap_state, uint32_t *res)
{
	VO_CODEC_INIT_USERDATA ud;
	AACENC_PARAM prm;

	memset(e, 0, sizeof(*e));
	e->blk = ksceKernelAllocMemBlock("BluetoothTurboAacHeap", SCE_KERNEL_MEMBLOCK_TYPE_KERNEL_RW, BTAAC_HEAP, NULL);
	if (e->blk < 0)
		return BTAAC_E_MEMORY;
	if (ksceKernelGetMemBlockBase(e->blk, (void **)&e->heap) < 0 || !e->heap) {
		enc_close(e);
		return BTAAC_E_MEMORY;
	}
	e->mo.Alloc = mo_alloc;
	e->mo.Free = mo_free;
	e->mo.Set = mo_set;
	e->mo.Copy = mo_copy;
	e->mo.Check = mo_check;
	e->mo.Compare = mo_compare;
	e->mo.Move = mo_move;
	ud.memflag = VO_IMF_USERMEMOPERATOR;
	ud.memData = &e->mo;
	if (neon)
		aacn_voGetAACEncAPI(&e->api);
	else
		aacc_voGetAACEncAPI(&e->api);
	if (!e->api.Init || !e->api.SetInputData || !e->api.GetOutputData || !e->api.SetParam || !e->api.Uninit) {
		enc_close(e);
		return BTAAC_E_INIT;
	}
	g_cur = e;
	btaac_assert_failed = 0;
	if (e->api.Init(&e->h, VO_AUDIO_CodingAAC, &ud)) {
		g_cur = 0;
		enc_close(e);
		return e->refused ? BTAAC_E_MEMORY : BTAAC_E_INIT;
	}
	e->open = 1;
	prm.sampleRate = (int)rate;
	prm.bitRate = (int)bps;
	prm.nChannels = (short)channels;
	prm.adtsUsed = 0;                       /* raw access units: the packet wrapper is the caller's */
	if (e->api.SetParam(e->h, VO_PID_AAC_ENCPARAM, &prm)) {
		g_cur = 0;
		enc_close(e);
		return BTAAC_E_INIT;
	}
	g_cur = 0;
	if (btaac_assert_failed) {
		enc_close(e);
		return BTAAC_E_ASSERT;
	}
	*cap_state = (uint32_t)enc_limit(e, neon, cap, rate, bps, res);
	return BTAAC_OK;
}

/* One frame of 1024 samples a channel in, the access unit out (`*len` bytes, 0 when none came). 0, or a BTAAC_E_* code. */
static int enc_frame(Enc *e, const int16_t *pcm, uint32_t channels, uint8_t *out, uint32_t cap, uint32_t *len)
{
	VO_CODECBUFFER in, ob;
	VO_AUDIO_OUTPUTINFO oi;
	VO_U32 rc;
	unsigned int asserts = btaac_assert_failed;

	in.Buffer = (VO_PBYTE)pcm;
	in.Length = BTAAC_FRAME * channels * 2u;
	in.Time = 0;
	ob.Buffer = out;
	ob.Length = cap;
	ob.Time = 0;
	memset(&oi, 0, sizeof(oi));
	*len = 0;
	rc = e->api.SetInputData(e->h, &in);
	if (!rc)
		rc = e->api.GetOutputData(e->h, &ob, &oi);
	if (btaac_assert_failed != asserts)
		return BTAAC_E_ASSERT;
	if (rc)
		return BTAAC_E_ENCODE;
	if (ob.Length > cap)
		return BTAAC_E_TOOBIG;
	*len = ob.Length;
	return BTAAC_OK;
}

/* ---- the crash record --------------------------------------------------------------------------------------- */

/* Writes the record in place; 1 if it went. */
static int guard_write(void)
{
	uint8_t b[BTAAC_G_BYTES];
	SceUID fd;
	int ok;

	btaac_guard_encode(&g_guard, b);
	ksceIoMkdir(BTT_DIR, 0777);
	fd = ksceIoOpen(BTAAC_GUARD_PATH, SCE_O_WRONLY | SCE_O_CREAT, 0666);
	if (fd < 0)
		return 0;
	ok = ksceIoWrite(fd, b, sizeof(b)) == (int)sizeof(b);
	ksceIoClose(fd);
	return ok;
}

static void guard_publish(void)
{
	g_crashes = g_guard.crashes;
	g_hold = g_boot.hold || !g_guard_ok;
	g_neon_banned = g_boot.neon_banned;
	g_aac_banned = g_boot.aac_banned;
}

static void guard_load(void)
{
	uint8_t b[BTAAC_G_BYTES];
	SceUID fd = ksceIoOpen(BTAAC_GUARD_PATH, SCE_O_RDONLY, 0);
	int n = 0;

	if (fd >= 0) {
		n = ksceIoRead(fd, b, sizeof(b));
		ksceIoClose(fd);
	}
	if (!btaac_guard_decode(&g_guard, b, n) && n > 0)
		BTT_KLOG("aac: the crash record is damaged (%d bytes): starting it afresh", n);
	g_boot = btaac_guard_boot(&g_guard);
	/* the record as it stands now goes back at once: a record that cannot be written protects nothing, and then no build is run */
	g_guard_ok = (uint32_t)guard_write();
	guard_publish();
	if (g_boot.unclean)
		BTT_KLOG("aac: the last AAC session did not end cleanly (%u in a row): no AAC this start%s", (unsigned)g_guard.crashes,
		         g_boot.neon_banned ? ", and the NEON build is switched off" : "");
	else if (g_boot.hold)
		BTT_KLOG("aac: held off by the crash record (%u in a row, test byte '%c')", (unsigned)g_guard.crashes, g_guard.test);
	if (!g_guard_ok)
		BTT_KLOG("aac: the crash record cannot be written: no AAC this start");
}

/* The test byte when no test is running: a ban stays on the record. */
static char test_rest(void)
{
	return g_boot.neon_banned ? 'N' : 'K';
}

/* Writes the record after a test run has put its marker away. A marker left on disk would be read at the next start as a crash that did
 * not happen and ban a build for good, so a failed write is tried once more and then holds AAC off for this start. */
static void guard_clear_or_hold(const char *which)
{
	if (guard_write() || guard_write())
		return;
	g_guard_ok = 0;
	guard_publish();
	BTT_KLOG("aac: the crash record could not be cleared after the %s test: no AAC this start", which);
}

/* ---- the self-test ------------------------------------------------------------------------------------------ */

/* Encodes the fixed signal with one build into `run`; returns its time in microseconds, or 0 if it could not run at all (no
 * memory: `run` is then not used). */
static uint32_t selftest_encode(int neon, BtAacRun *run)
{
	static int16_t pcm[BTAAC_FRAME * 2u] __attribute__((aligned(8)));
	static uint8_t out[BTAAC_AU_MAX + 64u] __attribute__((aligned(8)));
	Enc *e = &g_enc;
	uint64_t t0 = ksceKernelGetSystemTimeWide();
	uint32_t n = 0, us, cs, res;

	memset(run, 0, sizeof(*run));
	if (enc_open(e, neon, BTAAC_ST_RATE, 2, BTAAC_ST_KBPS * 1000u, 0, &cs, &res) != BTAAC_OK)
		return 0;
	run->ok = 1;
	run->hash = BTAAC_FNV_BASIS;
	for (uint32_t f = 0; f < BTAAC_ST_FRAMES && run->ok; f++) {
		uint32_t len = 0;

		for (uint32_t i = 0; i < BTAAC_FRAME; i++)
			btaac_sig_selftest(f * BTAAC_FRAME + i, &pcm[2u * i], &pcm[2u * i + 1u]);
		if (enc_frame(e, pcm, 2, out, sizeof(out), &len) != BTAAC_OK || len > BTAAC_AU_MAX) {
			run->ok = 0;
			break;
		}
		if (len) {
			run->len[n++] = (uint16_t)len;
			run->bytes += len;
			run->hash = btaac_fnv(run->hash, out, len);
		}
	}
	run->aus = n;
	enc_close(e);
	us = (uint32_t)(ksceKernelGetSystemTimeWide() - t0);
	return us ? us : 1u;
}

static void selftest_run(void)
{
	static BtAacRun c, n;
	int have_c = 0, have_n = 0;
	int verdict;

	if (g_state == BTAAC_S_RUNNING || g_state == BTAAC_S_STARTING) {
		BTT_KLOG("aac: self-test skipped: a session is running");     /* its encoder block is in use, and the verdict stands */
		return;
	}
	g_selftest_runs++;
	g_st_c_us = g_st_n_us = 0;
	if (g_boot.aac_banned) {
		verdict = BTAAC_ST_C_FAIL;
		goto done;
	}
	if (!g_guard_ok) {
		verdict = BTAAC_ST_ERROR;
		goto done;
	}
	g_guard.test = 'c';
	if (!guard_write()) {
		g_guard.test = test_rest();
		verdict = BTAAC_ST_ERROR;
		goto done;
	}
	g_st_c_us = selftest_encode(0, &c);
	have_c = g_st_c_us != 0;
	g_guard.test = test_rest();
	guard_clear_or_hold("C");
	if (have_c && c.ok && !g_boot.neon_banned && g_guard_ok) {
		g_guard.test = 'n';
		if (guard_write()) {
			g_st_n_us = selftest_encode(1, &n);
			have_n = g_st_n_us != 0;
		}
		g_guard.test = test_rest();
		guard_clear_or_hold("NEON");
	}
	verdict = btaac_selftest_decide(&g_boot, have_c ? &c : 0, have_n ? &n : 0);
	g_st_aus = c.aus;
	g_st_bytes = c.bytes;
	g_st_hash_c = have_c ? c.hash : 0;
	g_st_hash_n = have_n ? n.hash : 0;
done:
	g_selftest = (uint32_t)verdict;
	BTT_KLOG("aac: self-test %s: C %u us, NEON %u us, %u access units, %u bytes (hash %08x%08x, NEON %08x%08x), NEON %s", verdict == BTAAC_ST_PASS ? "passed" :
	         verdict == BTAAC_ST_NEON_DIFF ? "NEON DIFFERS" : verdict == BTAAC_ST_NEON_BANNED ? "NEON banned" :
	         verdict == BTAAC_ST_C_FAIL ? "FAILED" : "could not run", (unsigned)g_st_c_us, (unsigned)g_st_n_us, (unsigned)g_st_aus,
	         (unsigned)g_st_bytes, (unsigned)(g_st_hash_c >> 32), (unsigned)g_st_hash_c, (unsigned)(g_st_hash_n >> 32), (unsigned)g_st_hash_n,
	         btaac_neon_allowed(verdict) ? "allowed" : "not used");
}

/* ---- the session -------------------------------------------------------------------------------------------- */

static void session_end(int state, int error)
{
	btaac_ring_close();
	prio_restore();
	enc_close(&g_enc);
	btaac_guard_session_end(&g_guard);
	if (!guard_write() && !guard_write()) {
		/* 'S' or 'Z' stays on disk: the next start would count a crash that did not happen; hold AAC off for this start instead */
		g_guard_ok = 0;
		guard_publish();
		BTT_KLOG("aac: the crash record could not be cleared at the end of the session: no AAC this start");
	}
	g_crashes = g_guard.crashes;
	g_last_error = error;
	g_state = (uint32_t)state;
}

static void session_start(void)
{
	int neon = g_req.neon && btaac_neon_allowed((int)g_selftest);
	int err;
	uint32_t cap_state = BTAAC_CAP_NONE, res = 0;

	if (g_hold || !btaac_c_allowed((int)g_selftest)) {
		g_last_error = BTAAC_E_REFUSED;
		g_state = BTAAC_S_FAILED;
		return;
	}
	err = enc_open(&g_enc, neon, g_req.rate, g_req.channels, g_req.bps, g_req.cap, &cap_state, &res);
	if (err != BTAAC_OK) {
		g_last_error = err;
		g_state = BTAAC_S_FAILED;
		BTT_KLOG("aac: the encoder did not start (error %d)", err);
		return;
	}
	btaac_guard_session_begin(&g_guard, neon);
	if (!guard_write()) {
		/* a session that could not be recorded would not be known to have crashed */
		g_guard.session = 'K';
		enc_close(&g_enc);
		g_last_error = BTAAC_E_INIT;
		g_state = BTAAC_S_FAILED;
		return;
	}
	g_enc_calls = g_enc_sum_us = g_enc_max_us = g_slow = g_pcm_max = 0;
	g_s_neon = (uint32_t)neon;
	g_s_bps = g_req.bps;
	g_s_rate = g_req.rate;
	g_s_channels = g_req.channels;
	g_s_cap = g_req.cap;
	g_s_res = res;
	g_s_cap_state = cap_state;
	g_last_error = BTAAC_E_NONE;
	/* every log write of the session's start comes before the priority is raised */
	BTT_KLOG("aac: session started: %u kb/s, %u Hz, %u channel(s), %s build", (unsigned)(g_req.bps / 1000u), (unsigned)g_req.rate,
	         (unsigned)g_req.channels, neon ? "NEON" : "C");
	if (g_req.cap)
		BTT_KLOG("aac: access units held to %u bytes (reservoir %u bytes): %s", (unsigned)g_req.cap, (unsigned)res,
		         cap_state == BTAAC_CAP_APPLIED ? "applied" : "NOT APPLIED, a longer unit is cut into pieces");
	prio_apply();
	btaac_ring_open(g_req.channels);
	g_state = BTAAC_S_RUNNING;
}

/* Encodes every whole frame waiting while the access unit ring has room. */
static void session_pump(void)
{
	static int16_t pcm[BTAAC_FRAME * 2u] __attribute__((aligned(8)));
	static uint8_t out[BTAAC_AU_MAX + 64u] __attribute__((aligned(8)));

	for (;;) {
		uint32_t len = 0, us, avail = btaac_pcm_avail();
		uint64_t t0;
		int err;

		if (avail > g_pcm_max)
			g_pcm_max = avail;
		if (g_quit || avail < BTAAC_FRAME || !btaac_au_room())
			break;
		btaac_pcm_take(pcm, BTAAC_FRAME);
		t0 = ksceKernelGetSystemTimeWide();
		err = enc_frame(&g_enc, pcm, g_s_channels, out, sizeof(out), &len);
		us = (uint32_t)(ksceKernelGetSystemTimeWide() - t0);
		g_enc_calls++;
		g_enc_sum_us += us;
		if (us > g_enc_max_us)
			g_enc_max_us = us;
		if (us > BTAAC_FRAME_US) {
			g_slow++;
		}
		if (err == BTAAC_OK && len > BTAAC_AU_MAX)
			err = BTAAC_E_TOOBIG;
		if (err != BTAAC_OK) {
			BTT_KLOG("aac: the session ended: encoder error %d", err);
			session_end(BTAAC_S_FAILED, err);
			return;
		}
		if (len)
			btaac_au_put(out, len);
		if (btaac_pcm_avail() >= BTAAC_FRAME)    /* a backlog: the thread, now at a high priority, gives the core's other threads a slice between frames */
			ksceKernelDelayThread(BTAAC_CATCHUP_US);
	}
}

/* ---- requests ------------------------------------------------------------------------------------------------- */

static void post(uint32_t cmd)
{
	__atomic_or_fetch(&g_cmds, cmd, __ATOMIC_ACQ_REL);
	if (g_ev >= 0)
		ksceKernelSetEventFlag(g_ev, EV_CMD);
}

int btaac_start(uint32_t kbps, uint32_t rate, uint32_t channels, uint32_t max_kbps, int accel, uint32_t mtu)
{
	uint32_t expect, bps, cap = 0, max_bps;

	if (kbps > 100000u)                      /* far above anything the encoder takes: the clamp's top, without a wrap of the 32 bits */
		kbps = 100000u;
	if (max_kbps > 100000u)
		max_kbps = 100000u;
	if (g_th < 0)
		return BTAAC_ERR_THREAD;
	if (!btaac_format_ok(rate, channels))
		return BTAAC_ERR_ARG;
	if (max_kbps && max_kbps * 1000u < BTAAC_MIN_BPS_CH * channels)
		return BTAAC_ERR_RATE;
	if (g_hold)
		return BTAAC_ERR_HELD;
	if (!btaac_c_allowed((int)g_selftest))
		return BTAAC_ERR_NOTEST;
	max_bps = max_kbps * 1000u;
	if (mtu) {
		/* the packet limit is the bound: no access unit longer than one packet takes, and a bit rate whose average frame leaves the reservoir no room comes down */
		uint32_t cap_bps;

		cap = btaac_au_cap(btaac_mtu_sane(mtu));
		cap_bps = btaac_cap_max_bps(cap, rate);
		if (cap_bps && (!max_bps || cap_bps < max_bps))
			max_bps = cap_bps;
	}
	bps = btaac_clamp_bps(kbps * 1000u, rate, channels, max_bps);
	expect = BTAAC_S_IDLE;
	if (!__atomic_compare_exchange_n(&g_state, &expect, (uint32_t)BTAAC_S_STARTING, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		expect = BTAAC_S_FAILED;                 /* the last session ended badly: a new one may be asked for */
		if (!__atomic_compare_exchange_n(&g_state, &expect, (uint32_t)BTAAC_S_STARTING, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return BTAAC_ERR_BUSY;
	}
	g_req.bps = bps;
	g_req.rate = rate;
	g_req.channels = channels;
	g_req.cap = cap;
	g_req.neon = accel != 0;
	post(CMD_START);
	return BTAAC_OK;
}

void btaac_stop(void)
{
	if (g_th >= 0)
		post(CMD_STOP);
}

void btaac_sync(int on)
{
	if (on && g_th >= 0 && !g_selftest_asked) {
		g_selftest_asked = 1;
		post(CMD_SELFTEST);
	}
}

void btaac_get_info(BtAacInfo *o)
{
	uint32_t calls = g_enc_calls;

	memset(o, 0, sizeof(*o));
	o->state = g_state;
	o->last_error = g_last_error;
	o->selftest = g_selftest;
	o->neon_allowed = (uint32_t)btaac_neon_allowed((int)g_selftest);
	o->hold = g_hold;
	o->crashes = g_crashes;
	o->neon_in_use = g_s_neon;
	o->bps = g_s_bps;
	o->rate = g_s_rate;
	o->channels = g_s_channels;
	o->au_cap = g_s_cap;
	o->reservoir = g_s_res;
	o->cap_state = g_s_cap_state;
	btaac_ring_info(&o->pcm_overruns, &o->pcm_frames_in, &o->aus_popped, &o->au_drops, &o->aus_out);
	o->slow_frames = g_slow;
	o->enc_max_us = g_enc_max_us;
	o->enc_mean_us = calls ? g_enc_sum_us / calls : 0;
	o->thread_err = g_thread_err;
	o->cpu_mask = g_cpu_mask;
	o->cpu_mask_err = g_cpu_mask_err;
	o->assert_fails = btaac_assert_failed;
	o->prio = g_prio_actual;
	o->prio_src = g_prio_src;
	o->pcm_max_frames = g_pcm_max;
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
		/* a start and a stop in one batch: the stop came last, so the session is made and ended */
		if ((cmds & CMD_START) && g_state == BTAAC_S_STARTING)
			session_start();
		if ((cmds & CMD_STOP) && g_state == BTAAC_S_RUNNING)
			session_end(BTAAC_S_IDLE, BTAAC_E_NONE);
		if (cmds & CMD_SELFTEST)
			selftest_run();
		if (g_state == BTAAC_S_RUNNING) {
			session_pump();
			if (g_quit)
				break;
			ksceKernelDelayThread(BTAAC_POLL_US);   /* the sender's hooks cannot wake the thread: see the top of the file */
			continue;
		}
		if (ksceKernelWaitEventFlag(g_ev, EV_CMD | EV_QUIT, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, &bits, NULL) < 0) {
			g_last_error = BTAAC_E_WAIT;
			BTT_KLOG("aac: the thread's wait failed: the engine stops");
			break;
		}
	}
	if (g_state == BTAAC_S_RUNNING)
		session_end(BTAAC_S_IDLE, BTAAC_E_NONE);    /* the module is stopping: the session ends cleanly */
	return 0;
}

void btaac_init(void)
{
	g_th = g_ev = -1;
	guard_load();
	g_ev = ksceKernelCreateEventFlag("BluetoothTurboAac", 0, 0, NULL);
	if (g_ev < 0) {
		g_thread_err = (uint32_t)g_ev;
		g_ev = -1;
		BTT_KLOG("aac: no event flag (0x%08x): no AAC", (unsigned)g_thread_err);
		return;
	}
	g_cpu_mask = BTAAC_CPU_SYSTEM;
	g_th = ksceKernelCreateThread("BluetoothTurboAac", worker_main, BTAAC_PRIO, BTAAC_STACK, 0, BTAAC_CPU_SYSTEM, NULL);
	if (g_th < 0) {
		/* the system core refused the mask: the system's own placement, and the log says so */
		g_cpu_mask_err = (uint32_t)g_th;
		g_cpu_mask = 0;
		BTT_KLOG("aac: the system core's mask was refused (0x%08x): default placement", (unsigned)g_cpu_mask_err);
		g_th = ksceKernelCreateThread("BluetoothTurboAac", worker_main, BTAAC_PRIO, BTAAC_STACK, 0, 0, NULL);
	}
	if (g_th < 0 || ksceKernelStartThread(g_th, 0, NULL) < 0) {
		g_thread_err = (uint32_t)(g_th < 0 ? g_th : -2);
		BTT_KLOG("aac: the engine's thread could not be made (0x%08x): no AAC", (unsigned)g_thread_err);
		if (g_th >= 0)
			ksceKernelDeleteThread(g_th);
		g_th = -1;
		ksceKernelDeleteEventFlag(g_ev);
		g_ev = -1;
		return;
	}
	BTT_KLOG("aac: engine ready (thread mask 0x%x)", (unsigned)g_cpu_mask);
}

void btaac_exit(void)
{
	if (g_th >= 0) {
		unsigned int timeout = BTAAC_QUIT_WAIT_US;

		g_quit = 1;
		ksceKernelSetEventFlag(g_ev, EV_QUIT);
		if (ksceKernelWaitThreadEnd(g_th, NULL, &timeout) >= 0)
			ksceKernelDeleteThread(g_th);
		else
			BTT_KLOG("aac: the thread did not end within %u s", (unsigned)(BTAAC_QUIT_WAIT_US / 1000000u));
		g_th = -1;
	}
	if (g_ev >= 0)
		ksceKernelDeleteEventFlag(g_ev);
	g_ev = -1;
}
