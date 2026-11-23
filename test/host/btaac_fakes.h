/*
 * btaac_fakes.h - the console's kernel calls as the AAC engine's host tests need them (test/test_btaac.c): the
 * engine's thread is a POSIX thread, its event flag a condition variable, its memory blocks calloc'd, the crash record's file a byte
 * array (every write recorded), and the encoder the vendored one behind both of the engine's build entry points, each with a fault
 * that can be switched on. Included after the test's own CHECK macro and after kernel/btaac_leaf.c and kernel/btaac.c.
 */
#ifndef BTAAC_FAKES_H
#define BTAAC_FAKES_H

#pragma GCC diagnostic ignored "-Wunused-function"   /* each test uses some of the helpers */

/* ---- the fake kernel -------------------------------------------------------------------------------------- */

static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;

/* log */
static char g_log[16384];
void btt_klog_init(void) {}
void btt_klog_line(const char *line)
{
	pthread_mutex_lock(&g_mx);
	if (strlen(g_log) + strlen(line) + 2 < sizeof(g_log)) {
		strcat(g_log, line);
		strcat(g_log, "\n");
	}
	pthread_mutex_unlock(&g_mx);
}
static int log_has(const char *needle) { return strstr(g_log, needle) != NULL; }

/* the crash record's file: one file, written in place, every write recorded */
static uint8_t g_file[64];
static int g_file_len, g_file_exists, g_file_write_fails, g_file_pos;
static int g_write_fail_from = -1;     /* every write from this one on fails (counted from 0), the record file otherwise fine */
static uint8_t g_writes[64][BTAAC_G_BYTES];
static int g_nwrites;
SceUID ksceIoOpen(const char *path, int flags, int mode)
{
	(void)mode;
	if (strcmp(path, BTT_DIR "/aac.guard"))
		return -1;
	if (!g_file_exists) {
		if (!(flags & SCE_O_CREAT) || g_file_write_fails)
			return -1;
		g_file_exists = 1;
		g_file_len = 0;
	}
	CHECK(!(flags & SCE_O_TRUNC), "the crash record must be written in place, never truncated");
	g_file_pos = 0;
	return 5;
}
int ksceIoRead(SceUID fd, void *buf, SceSize n)
{
	(void)fd;
	if ((int)n > g_file_len)
		n = (SceSize)g_file_len;
	memcpy(buf, g_file, n);
	return (int)n;
}
int ksceIoWrite(SceUID fd, const void *buf, SceSize n)
{
	(void)fd;
	if (g_file_write_fails || (g_write_fail_from >= 0 && g_nwrites >= g_write_fail_from))
		return -1;
	memcpy(g_file + g_file_pos, buf, n);
	g_file_pos += (int)n;
	if (g_file_pos > g_file_len)
		g_file_len = g_file_pos;
	CHECK(n == BTAAC_G_BYTES && g_nwrites < 64, "the record is written whole, in one write");
	memcpy(g_writes[g_nwrites++], buf, n);
	return (int)n;
}
int ksceIoClose(SceUID fd) { (void)fd; return 0; }
int ksceIoMkdir(const char *path, int mode) { (void)path; (void)mode; return 0; }

/* memory blocks */
static void *g_blocks[16];
static int g_live_blocks, g_block_fail, g_blocks_made;
SceUID ksceKernelAllocMemBlock(const char *name, unsigned type, unsigned size, void *opt)
{
	int i;

	(void)name; (void)opt;
	CHECK(type == SCE_KERNEL_MEMBLOCK_TYPE_KERNEL_RW && size == 0x10000, "the encoder's block is one 64 KB kernel block");
	pthread_mutex_lock(&g_mx);
	if (g_block_fail) {
		pthread_mutex_unlock(&g_mx);
		return -1;
	}
	for (i = 0; i < 16 && g_blocks[i]; i++) ;
	CHECK(i < 16, "too many blocks");
	g_blocks[i] = calloc(1, size);
	g_live_blocks++;
	g_blocks_made++;
	pthread_mutex_unlock(&g_mx);
	return 0x100 + i;
}
int ksceKernelGetMemBlockBase(SceUID uid, void **base) { *base = g_blocks[uid - 0x100]; return 0; }
int ksceKernelFreeMemBlock(SceUID uid)
{
	pthread_mutex_lock(&g_mx);
	CHECK(uid >= 0x100 && g_blocks[uid - 0x100], "a block freed that was not there");
	free(g_blocks[uid - 0x100]);
	g_blocks[uid - 0x100] = NULL;
	g_live_blocks--;
	pthread_mutex_unlock(&g_mx);
	return 0;
}

/* event flag and threads */
static unsigned g_flags;
static int g_ev_made, g_ev_deleted;
SceUID ksceKernelCreateEventFlag(const char *name, unsigned attr, unsigned pattern, void *opt)
{
	(void)name; (void)attr; (void)pattern; (void)opt;
	g_ev_made++;
	return 0x200;
}
int ksceKernelDeleteEventFlag(SceUID ev) { (void)ev; g_ev_deleted++; return 0; }
int ksceKernelSetEventFlag(SceUID ev, unsigned bits)
{
	(void)ev;
	pthread_mutex_lock(&g_mx);
	g_flags |= bits;
	pthread_cond_broadcast(&g_cv);
	pthread_mutex_unlock(&g_mx);
	return 0;
}
int ksceKernelWaitEventFlag(SceUID ev, unsigned bits, unsigned mode, unsigned *res, SceUInt *timeout)
{
	(void)ev;
	CHECK(mode == (SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT) && !timeout, "the idle thread waits with no timeout");
	pthread_mutex_lock(&g_mx);
	while (!(g_flags & bits))
		pthread_cond_wait(&g_cv, &g_mx);
	*res = g_flags & bits;
	g_flags &= ~bits;
	pthread_mutex_unlock(&g_mx);
	return 0;
}

static SceKernelThreadEntry g_entry;
static pthread_t g_pt;
static int g_thread_made, g_thread_cpu = -1, g_refuse_mask, g_thread_cpus_tried[4], g_thread_tries, g_thread_started, g_thread_deleted;
static int g_thread_prio, g_thread_stack, g_cur_prio = 0x70;
static void *thread_tramp(void *a) { (void)a; g_entry(0, NULL); return NULL; }
SceUID ksceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int prio, SceSize stack, unsigned attr, int cpu, void *opt)
{
	(void)name; (void)attr; (void)opt;
	g_thread_cpus_tried[g_thread_tries++ & 3] = cpu;
	if (cpu && g_refuse_mask)
		return (int)0x80020000;
	g_entry = entry;
	g_thread_cpu = cpu;
	g_thread_prio = prio;
	g_cur_prio = prio;
	g_thread_stack = (int)stack;
	g_thread_made++;
	return 0x300;
}
int ksceKernelStartThread(SceUID th, SceSize args, void *argp)
{
	(void)th; (void)args; (void)argp;
	g_thread_started++;
	return pthread_create(&g_pt, NULL, thread_tramp, NULL);
}
int ksceKernelWaitThreadEnd(SceUID th, int *res, SceUInt *timeout)
{
	(void)th; (void)res; (void)timeout;
	pthread_join(g_pt, NULL);
	return 0;
}
int ksceKernelDeleteThread(SceUID th) { (void)th; g_thread_deleted++; return 0; }
int ksceKernelDelayThread(SceUInt us) { usleep(us); return 0; }
int ksceKernelCpuId(void) { return 3; }
static uint32_t g_now_low;                    /* 0 = the real clock; a test may set the microsecond clock the events are stamped with */
uint32_t ksceKernelGetSystemTimeLow(void)
{
	struct timespec ts;

	if (g_now_low)
		return g_now_low;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u);
}

/* the thread manager: a list of threads (name, priority) the engine reads for the audio sender's, and its own priority, which it may change */
static struct { const char *name; int prio; } g_threads[1024] = { { "SceBtWorker", 0x12 }, { "SceAVConfigDataSend", 0x12 }, { "SceBtIntr", 0x12 }, { "SceShellMain", 0x40 } };
static int g_nthreads = 4, g_list_fail, g_info_fail_at = -1;
static int g_prio_calls, g_prio_refuse, g_prio_last = -1;
int ksceKernelGetThreadIdList(SceUID pid, SceUID *ids, int n, int *copy_count)
{
	int room = n >> 2;                        /* the size is in bytes (the thread list's size is counted in bytes) */

	if (pid == KERNEL_PID)
		return (int)0x80020005;               /* the kernel's own pid is refused: only the system-wide one lists */
	CHECK(pid == 0x10021, "the system-wide thread list");
	if (g_list_fail)
		return (int)0x80020001;
	for (int i = 0; i < g_nthreads && i < room; i++)
		ids[i] = 0x1000 + i;
	*copy_count = g_nthreads < room ? g_nthreads : room;
	return g_nthreads;                        /* the total, which may be more than was copied */
}
int ksceKernelGetThreadInfo(SceUID thid, SceKernelThreadInfo *info)
{
	static uint64_t run;

	CHECK(info->size == sizeof(*info), "the information's size is set");
	if (thid == 0x300) {                      /* the engine's own thread */
		info->runClocks = run += 1000;
		info->currentPriority = g_cur_prio;
		info->name[0] = 0;
		return 0;
	}
	if (g_info_fail_at == thid - 0x1000)
		return -1;
	CHECK(thid >= 0x1000 && thid - 0x1000 < g_nthreads, "a thread of the list");
	strncpy(info->name, g_threads[thid - 0x1000].name, sizeof(info->name));
	info->currentPriority = g_threads[thid - 0x1000].prio;
	return 0;
}
int ksceKernelChangeThreadPriority(SceUID thid, int priority)
{
	CHECK(thid == 0, "only the engine's own thread changes its priority");
	g_prio_calls++;
	g_prio_last = priority;
	if (g_prio_refuse)
		return (int)0x80020002;
	g_cur_prio = priority;
	return 0;
}
int ksceKernelGetThreadCurrentPriority(void) { return g_cur_prio; }
static uint64_t (*g_clock_hook)(void);        /* a test may script the clock (the timing run's arithmetic) */
uint64_t ksceKernelGetSystemTimeWide(void)
{
	struct timespec ts;

	if (g_clock_hook)
		return g_clock_hook();
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* the encoder: the vendored one, behind both entry points, each with a fault that can be switched on */
extern VO_S32 voGetAACEncAPI(VO_AUDIO_CODECAPI *api);
static VO_U32 (*real_get[2])(VO_HANDLE, VO_CODECBUFFER *, VO_AUDIO_OUTPUTINFO *);
static int g_fault[2];            /* [0] the C entry, [1] the NEON entry */
enum { F_NONE = 0, F_FLIP_BYTE, F_FLIP_LENGTH, F_ERROR, F_ASSERT, F_TOO_BIG, F_FLIP_LAST_AU };
static int g_calls[2];
static VO_U32 wrapped_get(int v, VO_HANDLE h, VO_CODECBUFFER *out, VO_AUDIO_OUTPUTINFO *oi)
{
	VO_U32 rc = real_get[v](h, out, oi);
	int n = g_calls[v]++;

	if (rc)
		return rc;
	switch (g_fault[v]) {
	case F_FLIP_BYTE:   if (n == 3 && out->Length > 10) out->Buffer[10] ^= 1; break;
	case F_FLIP_LENGTH: if (n == 3 && out->Length > 10) out->Length--; break;
	case F_ERROR:       if (n == 2) return VO_ERR_FAILED; break;
	case F_ASSERT:      if (n == 2) btaac_assert_failed++; break;
	case F_TOO_BIG:     if (n == 2) out->Length = BTAAC_AU_MAX + 100; break;
	case F_FLIP_LAST_AU: if (n == BTAAC_ST_FRAMES - 1 && out->Length > 3) out->Buffer[2] ^= 0x80; break;
	}
	return rc;
}
static VO_U32 w_get_c(VO_HANDLE h, VO_CODECBUFFER *o, VO_AUDIO_OUTPUTINFO *i) { return wrapped_get(0, h, o, i); }
static VO_U32 w_get_n(VO_HANDLE h, VO_CODECBUFFER *o, VO_AUDIO_OUTPUTINFO *i) { return wrapped_get(1, h, o, i); }
VO_S32 aacc_voGetAACEncAPI(VO_AUDIO_CODECAPI *api)
{
	VO_S32 r = voGetAACEncAPI(api);

	real_get[0] = api->GetOutputData;
	api->GetOutputData = w_get_c;
	return r;
}
VO_S32 aacn_voGetAACEncAPI(VO_AUDIO_CODECAPI *api)
{
	VO_S32 r = voGetAACEncAPI(api);

	real_get[1] = api->GetOutputData;
	api->GetOutputData = w_get_n;
	return r;
}

/* the reservoir limit (kernel/btaac_enclimit.c, built with the encoder: the symbol without a prefix here), behind both entry points; a test may make it refuse */
extern int btaac_enc_limit(void *h, unsigned int res_bytes);
static int g_limit_refuse;
static unsigned int g_limit_asked;         /* the reservoir the last call asked for, bytes */
static int g_limit_calls;
int aacc_btaac_enc_limit(void *h, unsigned int res_bytes) { g_limit_calls++; g_limit_asked = res_bytes; return g_limit_refuse ? -1 : btaac_enc_limit(h, res_bytes); }
int aacn_btaac_enc_limit(void *h, unsigned int res_bytes) { g_limit_calls++; g_limit_asked = res_bytes; return g_limit_refuse ? -1 : btaac_enc_limit(h, res_bytes); }

/* ---- helpers ----------------------------------------------------------------------------------------------- */

static void fs_set(const char *session_test_crashes)   /* "SKn" style: session, test, crash digit; NULL = no file */
{
	g_file_len = 0;
	g_file_exists = 0;
	if (!session_test_crashes)
		return;
	memcpy(g_file, "BAG1", 4);
	g_file[4] = (uint8_t)session_test_crashes[0];
	g_file[5] = (uint8_t)session_test_crashes[1];
	g_file[6] = (uint8_t)session_test_crashes[2];
	g_file[7] = '\n';
	g_file_len = 8;
	g_file_exists = 1;
}
static void fs_expect(char session, char test, int crashes)
{
	CHECK(g_file_len == 8 && !memcmp(g_file, "BAG1", 4) && g_file[4] == (uint8_t)session && g_file[5] == (uint8_t)test &&
	      g_file[6] == (uint8_t)('0' + crashes), "the record is %.8s, expected %c %c %d", g_file, session, test, crashes);
}

static void wait_for(int (*cond)(void), const char *what)
{
	for (int i = 0; i < 4000 && !cond(); i++)
		usleep(1000);
	CHECK(cond(), "timed out waiting for: %s", what);
}
static int c_selftest_done(void) { return g_selftest != BTAAC_ST_NOT_RUN; }
static int c_running(void) { return g_state == BTAAC_S_RUNNING; }
static int c_failed(void) { return g_state == BTAAC_S_FAILED; }
static int c_idle(void) { return g_state == BTAAC_S_IDLE; }

static BtAacInfo info(void) { BtAacInfo i; btaac_get_info(&i); return i; }

/* init, and the self-test through the worker's own request */
static void engine_up(void)
{
	btaac_init();
	CHECK(g_th >= 0, "the thread was made");
}
static void run_selftest(void)
{
	btaac_sync(1);
	wait_for(c_selftest_done, "the self-test");
}
static void engine_down(void)
{
	btaac_exit();
	CHECK(g_live_blocks == 0, "every encoder block freed (%d live)", g_live_blocks);
}

#endif /* BTAAC_FAKES_H */
