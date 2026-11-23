/*
 * test_btldac.c - host test of the LDAC add-on (kernel/btldac.c, btldac_enc.c, btldac_mem.c, btldac_leaf.c, btldac_core.h), compiled as they are, with the console's kernel
 * calls replaced by fakes that really run the engine's thread (a POSIX thread), its event flag and the crash record's file, and with the vendored encoder
 * (third_party/libldac, the integer build with the add-on's allocator) behind it.
 *
 *   make test      (the library is built from source for the host, then this test is linked with it and run)
 *
 * What is proved:
 *   - the packet: the RTP header and the payload header byte, byte for byte; refusals; the timestamp step; the media channel's limit; the latency bound; the adaptive clamp;
 *   - the crash record and what a start does with it (the same table as the AAC and aptX add-ons');
 *   - the encoder: the self-test's hashes are the ones of the host build of the same sources (a second build with a wrong hash is failed by the self-test);
 *     330 and 660 kb/s make 660-byte packets of 6 and 3 frames, the first one after 6 and 3 calls, every frame starting with the sync byte, 412,500 and 825,000 bytes
 *     for 10 s (the host totals); a media channel limit under 679 is refused and 679 and 1021 give the same packets; a change of mode in mid-stream goes on with the
 *     new frame size; the adaptive mode starts on 330, never goes above 660 and comes down when the queue is deep; the encoder takes 7 allocations in 32 KB and none
 *     while it encodes;
 *   - the rings: whole blocks only, order across wraps, a full ring refuses and counts, packets of an earlier session are skipped, a trim drops the oldest, a producer and a
 *     consumer running flat out lose and reorder nothing;
 *   - the engine in a thread of its own: the self-test marks the record 't' then 'K'; a session marks it 'S' and ends it 'K' with the count at 0, by a stop and by module stop;
 *     a start that finds a session left running holds LDAC off, three in a row too, a self-test left running bans the encoder; a limit under 679, a request before the
 *     self-test or while held is refused with its own error; a change of the bit rate setting reaches a running session; the priority is one below the sender's.
 * Each scenario runs in its own process: the engine keeps its state in statics.
 */
#include <btt_host.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include "../kernel/btldac_mem.c"
#include "../kernel/btldac_enc.c"
#include "../kernel/btldac_leaf.c"
#include "../kernel/btldac.c"

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

#pragma GCC diagnostic ignored "-Wunused-function"

/* ---- the fake kernel ---------------------------------------------------------------------------------------- */

static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
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
static uint8_t g_writes[64][BTLDAC_G_BYTES];
static int g_nwrites;
SceUID ksceIoOpen(const char *path, int flags, int mode)
{
	(void)mode;
	if (strcmp(path, BTT_DIR "/ldac.guard"))
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
	if (g_file_write_fails)
		return -1;
	memcpy(g_file + g_file_pos, buf, n);
	g_file_pos += (int)n;
	if (g_file_pos > g_file_len)
		g_file_len = g_file_pos;
	CHECK(n == BTLDAC_G_BYTES && g_nwrites < 64, "the record is written whole, in one write");
	memcpy(g_writes[g_nwrites++], buf, n);
	return (int)n;
}
int ksceIoClose(SceUID fd) { (void)fd; return 0; }
int ksceIoMkdir(const char *path, int mode) { (void)path; (void)mode; return 0; }

static unsigned g_flags;
SceUID ksceKernelCreateEventFlag(const char *name, unsigned attr, unsigned pattern, void *opt) { (void)name; (void)attr; (void)pattern; (void)opt; return 0x200; }
int ksceKernelDeleteEventFlag(SceUID ev) { (void)ev; return 0; }
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
static int g_thread_cpu = -1, g_refuse_mask, g_thread_prio, g_cur_prio = 0x70, g_prio_calls, g_prio_last = -1, g_list_fail;
static void *thread_tramp(void *a) { (void)a; g_entry(0, NULL); return NULL; }
SceUID ksceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int prio, SceSize stack, unsigned attr, int cpu, void *opt)
{
	(void)name; (void)attr; (void)opt;
	CHECK(stack >= 0x4000, "the encoder's deepest path is about 3.7 KB: the stack is 16 KB");
	if (cpu && g_refuse_mask)
		return (int)0x80020000;
	g_entry = entry;
	g_thread_cpu = cpu;
	g_thread_prio = prio;
	g_cur_prio = prio;
	return 0x300;
}
int ksceKernelStartThread(SceUID th, SceSize args, void *argp) { (void)th; (void)args; (void)argp; return pthread_create(&g_pt, NULL, thread_tramp, NULL); }
int ksceKernelWaitThreadEnd(SceUID th, int *res, SceUInt *timeout) { (void)th; (void)res; (void)timeout; pthread_join(g_pt, NULL); return 0; }
int ksceKernelDeleteThread(SceUID th) { (void)th; return 0; }
int ksceKernelDelayThread(SceUInt us) { usleep(us); return 0; }
int ksceKernelCpuId(void) { return 3; }
uint32_t ksceKernelGetSystemTimeLow(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u);
}
uint64_t ksceKernelGetSystemTimeWide(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
static struct { const char *name; int prio; } g_threads[8] = { { "SceBtWorker", 0x12 }, { "SceAVConfigDataSend", 0x12 }, { "SceShellMain", 0x40 } };
static int g_nthreads = 3;
int ksceKernelGetThreadIdList(SceUID pid, SceUID *ids, int n, int *copy_count)
{
	int room = n >> 2;

	if (pid == KERNEL_PID)
		return (int)0x80020005;
	CHECK(pid == 0x10021, "the system-wide thread list");
	if (g_list_fail)
		return (int)0x80020001;
	for (int i = 0; i < g_nthreads && i < room; i++)
		ids[i] = 0x1000 + i;
	*copy_count = g_nthreads < room ? g_nthreads : room;
	return g_nthreads;
}
int ksceKernelGetThreadInfo(SceUID thid, SceKernelThreadInfo *info)
{
	CHECK(info->size == sizeof(*info), "the information's size is set");
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
	g_cur_prio = priority;
	return 0;
}
int ksceKernelGetThreadCurrentPriority(void) { return g_cur_prio; }

/* ---- helpers ------------------------------------------------------------------------------------------------ */

static void fs_set(const char *session_test_count)   /* "SKn" style: session, test, count digit; NULL = no file */
{
	g_file_len = 0;
	g_file_exists = 0;
	if (!session_test_count)
		return;
	memcpy(g_file, "BLG1", 4);
	g_file[4] = (uint8_t)session_test_count[0];
	g_file[5] = (uint8_t)session_test_count[1];
	g_file[6] = (uint8_t)session_test_count[2];
	g_file[7] = '\n';
	g_file_len = 8;
	g_file_exists = 1;
}
static void fs_expect(char session, char test, int count)
{
	CHECK(g_file_len == 8 && !memcmp(g_file, "BLG1", 4) && g_file[4] == (uint8_t)session && g_file[5] == (uint8_t)test && g_file[6] == (uint8_t)('0' + count),
	      "the record is %.8s, expected %c %c %d", g_file, session, test, count);
}
static void wait_for(int (*cond)(void), const char *what)
{
	for (int i = 0; i < 6000 && !cond(); i++)
		usleep(1000);
	CHECK(cond(), "timed out waiting for: %s", what);
}
static int c_selftest_done(void) { return g_selftest != BTLDAC_ST_NOTRUN; }
static int c_running(void) { return g_state == BTLDAC_S_RUNNING; }
static int c_failed(void) { return g_state == BTLDAC_S_FAILED; }
static int c_idle(void) { return g_state == BTLDAC_S_IDLE; }
static BtLdacInfo info(void) { BtLdacInfo i; btldac_get_info(&i); return i; }
static void engine_up(void) { btldac_init(); CHECK(g_th >= 0, "the thread was made"); }
static void run_selftest(void) { btldac_sync(1); wait_for(c_selftest_done, "the self-test"); }
static void engine_down(void) { btldac_exit(); }

/* a block of the test signal, `n` frames from frame `first` */
static void sig_block(int16_t *pcm, uint32_t first, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		btldac_sig(first + i, &pcm[2 * i], &pcm[2 * i + 1]);
}
/* one second's worth of the console timing runs' noise: loud, hard on the encoder */
static void noise_block(int16_t *pcm, uint32_t first, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		pcm[2 * i] = (int16_t)((int16_t)(btldac_hash32((first + i) * 2u) >> 16) >> 2);
		pcm[2 * i + 1] = (int16_t)((int16_t)(btldac_hash32((first + i) * 2u + 1u) >> 16) >> 2);
	}
}

/* ---- the scenarios ------------------------------------------------------------------------------------------ */

static void t_packet(void)
{
	uint8_t pkt[BTLDAC_PKT_MAX + 8], fr[BTLDAC_FRAMES_BYTES];
	uint32_t n;

	for (int i = 0; i < (int)sizeof(fr); i++)
		fr[i] = (uint8_t)(i * 7 + 1);
	n = btldac_packet(pkt, sizeof(pkt), fr, 660, 6, 0x1234, 0xA1B2C3D4u, 0x56544101u);
	CHECK(n == 673 && pkt[0] == 0x80 && pkt[1] == 96 && pkt[2] == 0x12 && pkt[3] == 0x34 && pkt[4] == 0xA1 && pkt[5] == 0xB2 && pkt[6] == 0xC3 && pkt[7] == 0xD4 &&
	      pkt[8] == 0x56 && pkt[9] == 0x54 && pkt[10] == 0x41 && pkt[11] == 0x01 && pkt[12] == 0x06 && !memcmp(pkt + 13, fr, 660),
	      "RTP (version 2, type 96, no marker), the frame count in the low bits of the payload header, the frames as made: %u bytes", (unsigned)n);
	n = btldac_packet(pkt, sizeof(pkt), fr, 660, 3, 0xFFFF, 0, 0);
	CHECK(n == 673 && pkt[12] == 0x03 && !(pkt[12] & 0xE0) && pkt[2] == 0xFF && pkt[3] == 0xFF, "3 frames; the fragmentation, start and last bits are clear");
	CHECK(btldac_packet(pkt, sizeof(pkt), fr, 661, 12, 1, 2, 3) == 674 && pkt[12] == 12, "the longest packet: 661 bytes of frames, 12 frames");
	CHECK(btldac_packet(pkt, sizeof(pkt), fr, 660, 0, 1, 2, 3) == 0 && btldac_packet(pkt, sizeof(pkt), fr, 660, 16, 1, 2, 3) == 0, "no frames, or more than four bits count: refused");
	CHECK(btldac_packet(pkt, sizeof(pkt), fr, 0, 6, 1, 2, 3) == 0 && btldac_packet(pkt, sizeof(pkt), fr, 662, 6, 1, 2, 3) == 0, "no bytes, or more than libldac makes: refused");
	CHECK(btldac_packet(pkt, 673, fr, 660, 6, 1, 2, 3) == 673 && btldac_packet(pkt, 672, fr, 660, 6, 1, 2, 3) == 0, "the caller's room is the buffer's: 673 bytes take the packet, 672 do not");
	CHECK(btldac_packet(pkt, 600, fr, 660, 6, 1, 2, 3) == 0, "a buffer too small: refused");
	CHECK(btldac_ts_step(6) == 768 && btldac_ts_step(3) == 384 && btldac_ts_step(12) == 1536, "128 samples a frame");
	CHECK(btldac_mtu_ok(679) && btldac_mtu_ok(1021) && !btldac_mtu_ok(678) && !btldac_mtu_ok(672) && !btldac_mtu_ok(0), "libldac needs a limit of 679");
	CHECK(btldac_keep(0, BTLDAC_MODE_330) == 8 && btldac_keep(1, BTLDAC_MODE_330) == 4 && btldac_keep(2, BTLDAC_MODE_330) == 3 &&
	      btldac_keep(0, BTLDAC_MODE_660) == 16 && btldac_keep(1, BTLDAC_MODE_660) == 8 && btldac_keep(2, BTLDAC_MODE_660) == 6 && btldac_keep(9, BTLDAC_MODE_330) == 8 &&
	      btldac_keep(0, BTLDAC_MODE_ABR) == 16 && btldac_keep(0, BTLDAC_MODE_660) <= BTLDAC_PKT_RING_SLOTS, "the latency bound in packets of sound time, within the ring");
	CHECK(btldac_abr_clamp(0) == BTLDAC_EQMID_SQ && btldac_abr_clamp(1) == -1 && btldac_abr_clamp(2) == -1 && btldac_abr_clamp(-1) == -1, "the adaptive code never reaches the top quality");
	CHECK(btldac_frames_per_pkt(BTLDAC_MODE_330) == 6 && btldac_frames_per_pkt(BTLDAC_MODE_660) == 3 && btldac_eqmid_of(BTLDAC_MODE_330) == 2 && btldac_eqmid_of(BTLDAC_MODE_660) == 1 &&
	      btldac_kbps_of(BTLDAC_MODE_330) == 330 && btldac_kbps_of(BTLDAC_MODE_660) == 660, "the modes");
	CHECK(btldac_bench_bytes(330, 3750) == 412500 && btldac_bench_bytes(660, 3750) == 825000 && btldac_bench_bytes(990, 3750) == 1237500 && btldac_bench_bytes(330, 5) == 0 &&
	      btldac_bench_bytes(330, 6) == 660 && btldac_bench_bytes(660, 2) == 0 && btldac_bench_bytes(660, 3) == 660 && btldac_bench_bytes(990, 2) == 660, "the bench's totals: the 10 s figures");
}

static void t_guard(void)
{
	BtLdacGuard g;
	BtLdacBoot b;
	uint8_t bytes[BTLDAC_G_BYTES];

	btldac_guard_default(&g);
	g.session = 'S'; g.test = 't'; g.count = 2;
	btldac_guard_encode(&g, bytes);
	CHECK(!memcmp(bytes, "BLG1St2\n", 8), "the record's bytes: %.8s", bytes);
	memset(&g, 0, sizeof(g));
	CHECK(btldac_guard_decode(&g, bytes, 8) == 1 && g.session == 'S' && g.test == 't' && g.count == 2, "read back");
	static const char *bad[] = { "BLG2KK0\n", "bLG1KK0\n", "BLG1XK0\n", "BLG1KX0\n", "BLG1KK:\n", "BLG1KK/\n", "BAG1KK0\n", "BLG1ZK0\n", "BLG1Kn0\n" };
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		g.session = 'S'; g.test = 'B'; g.count = 5;
		CHECK(btldac_guard_decode(&g, (const uint8_t *)bad[i], 8) == 0 && g.session == 'K' && g.test == 'K' && g.count == 0, "bad record %u", i);
	}
	CHECK(btldac_guard_decode(&g, bytes, 6) == 0 && btldac_guard_decode(&g, bytes, 0) == 0, "a short or missing file starts clean");
	g.count = 200;
	btldac_guard_encode(&g, bytes);
	CHECK(bytes[6] == '9', "the count's digit stops at 9");
	btldac_guard_default(&g);
	b = btldac_guard_boot(&g);
	CHECK(!b.hold && !b.hold_now && !b.banned && g.count == 0, "clean: nothing held");
	btldac_guard_default(&g); g.session = 'S';
	b = btldac_guard_boot(&g);
	CHECK(b.hold && b.hold_now && !b.banned && g.count == 1 && g.session == 'K', "a session left running: held this start");
	b = btldac_guard_boot(&g);
	CHECK(!b.hold && g.count == 1, "the start after it may use LDAC again");
	btldac_guard_default(&g); g.test = 't';
	b = btldac_guard_boot(&g);
	CHECK(b.hold && b.banned && !b.hold_now && g.test == 'B', "a self-test left running: the encoder is banned");
	b = btldac_guard_boot(&g);
	CHECK(b.hold && b.banned && g.test == 'B', "and at every start after it");
	btldac_guard_default(&g);
	for (int i = 1; i <= 3; i++) {
		g.session = 'S';
		b = btldac_guard_boot(&g);
		CHECK(g.count == i && b.hold, "unclean session %d in a row", i);
	}
	b = btldac_guard_boot(&g);
	CHECK(b.hold && !b.hold_now && g.count == 3, "three in a row: held from then on, whatever follows");
	btldac_guard_session_begin(&g);
	CHECK(g.session == 'S', "a session begun is marked");
	btldac_guard_session_end(&g);
	CHECK(g.session == 'K' && g.count == 0, "a clean end clears the count");
}

/* the host build of the library, the self-test's hashes and the stream's figures */
static void t_selftest_encode(void)
{
	BtLdacRun r;

	CHECK(btldac_selftest_encode(BTLDAC_MODE_330, &r) == 0 && r.ok, "the run at 330");
	CHECK(r.pkts == BTLDAC_ST_PKTS_330 && r.bytes == BTLDAC_ST_BYTES_330 && r.pkts == 32 && r.bytes == 21120, "32 packets of 660 bytes");
#ifndef BAD_HASH_BUILD
	CHECK(r.hash == BTLDAC_ST_HASH_330 && btldac_run_pass(&r, BTLDAC_MODE_330), "the hash is the one in btldac_core.h: %016llx", (unsigned long long)r.hash);
	CHECK(btldac_selftest_encode(BTLDAC_MODE_660, &r) == 0 && r.ok && r.pkts == 64 && r.bytes == 42240 && r.hash == BTLDAC_ST_HASH_660 && btldac_run_pass(&r, BTLDAC_MODE_660),
	      "660: 64 packets, the hash is the one in btldac_core.h: %016llx", (unsigned long long)r.hash);
	CHECK(!btldac_run_pass(&r, BTLDAC_MODE_330), "a 660 run is not a 330 one");
	BtLdacRun w = r;
	w.hash ^= 1;
	CHECK(!btldac_run_pass(&w, BTLDAC_MODE_660), "a hash off by one bit fails");
	w = r; w.bytes++;
	CHECK(!btldac_run_pass(&w, BTLDAC_MODE_660), "a byte more fails");
	w = r; w.ok = 0;
	CHECK(!btldac_run_pass(&w, BTLDAC_MODE_660), "a run that was not ok fails");
#else
	CHECK(!btldac_run_pass(&r, BTLDAC_MODE_330), "a wrong hash in the build: the run does not pass");
#endif
	/* the encoder's allocations: 7 of them in 32 KB, and none while it encodes */
	BtLdacEnc e;
	uint32_t used, peak, allocs, refused, used2, allocs2, refused2;
	int16_t pcm[BTLDAC_FRAME * 2];
	uint8_t out[BTLDAC_OUT_MAX];
	uint32_t len, nf;

	CHECK(btldac_enc_open(&e, BTLDAC_MODE_330, 679) == 0, "open");
	btldac_heap_stats(&used, &peak, &allocs, &refused);
	CHECK(allocs >= 5 && allocs <= 9 && used > 15000 && used < BTLDAC_HEAP_BYTES && refused == 0, "the encoder took %u allocations, %u bytes of the %u", (unsigned)allocs, (unsigned)used, (unsigned)BTLDAC_HEAP_BYTES);
	for (uint32_t c = 0; c < 3750; c++) {
		sig_block(pcm, c * BTLDAC_FRAME, BTLDAC_FRAME);
		CHECK(btldac_enc_call(&e, pcm, out, &len, &nf) == 0, "call %u", (unsigned)c);
	}
	btldac_heap_stats(&used2, &peak, &allocs2, &refused2);
	CHECK(used2 == used && allocs2 == allocs && refused2 == 0, "no allocation while encoding (3,750 calls)");
	btldac_enc_close(&e);
}

static void t_stream_shape(void)
{
	static int16_t pcm[BTLDAC_FRAME * 2];
	static uint8_t out[BTLDAC_OUT_MAX];
	static const uint32_t want_total[2] = { 412500, 825000 };

	for (uint32_t mode = 0; mode < 2; mode++) {
		BtLdacEnc e;
		uint32_t len, nf, total = 0, pkts = 0, first = 0;

		CHECK(btldac_enc_open(&e, mode, 679) == 0, "open mode %u", (unsigned)mode);
		for (uint32_t c = 0; c < 3750; c++) {   /* 10 s of the console timing runs' noise */
			noise_block(pcm, c * BTLDAC_FRAME, BTLDAC_FRAME);
			CHECK(btldac_enc_call(&e, pcm, out, &len, &nf) == 0, "call %u", (unsigned)c);
			if (len) {
				if (!pkts)
					first = c;
				CHECK(len == 660 && nf == btldac_frames_per_pkt(mode), "mode %u: packet %u is %u bytes of %u frames", (unsigned)mode, (unsigned)pkts, (unsigned)len, (unsigned)nf);
				for (uint32_t f = 0; f < nf; f++)
					CHECK(out[f * (660 / nf)] == 0xAA, "mode %u: frame %u starts with the sync byte", (unsigned)mode, (unsigned)f);
				CHECK(e.kbps == (int)btldac_kbps_of(mode), "the library's own bit rate: %d", e.kbps);
				total += len;
				pkts++;
			}
		}
		CHECK(first == (mode == 0 ? 5u : 2u), "the first packet comes out at call %u (zero based)", (unsigned)first);
		CHECK(total == want_total[mode] && total == btldac_bench_bytes(btldac_kbps_of(mode), 3750), "mode %u: %u bytes for 10 s", (unsigned)mode, (unsigned)total);
		btldac_enc_close(&e);
	}
	/* the third figure: 990 kb/s (index 0, which no setting offers) makes 1,237,500 bytes for 10 s, 2 frames to a packet (the bench measures it) */
	{
		BtLdacEnc h;
		uint32_t total = 0, l, f;

		CHECK(btldac_enc_open(&h, BTLDAC_MODE_330, 679) == 0 && btldac_enc_set_eqmid(&h, BTLDAC_EQMID_HQ) == 0 && h.eqmid == BTLDAC_EQMID_HQ, "990 kb/s");
		for (uint32_t c = 0; c < 3750; c++) {
			noise_block(pcm, c * BTLDAC_FRAME, BTLDAC_FRAME);
			CHECK(btldac_enc_call(&h, pcm, out, &l, &f) == 0, "call");
			if (l) {
				CHECK(l == 660 && f == 2 && out[0] == 0xAA, "990: 2 frames of 330");
				total += l;
			}
		}
		CHECK(total == 1237500 && total == btldac_bench_bytes(990, 3750), "990 kb/s: %u bytes for 10 s", (unsigned)total);
		btldac_enc_close(&h);
	}
	/* the limit: under 679 refused; 679 and 1021 make the same packets (libldac sizes its packets for 679) */
	BtLdacEnc e;
	uint32_t len, nf, a = 0, b = 0;
	uint8_t o1[BTLDAC_OUT_MAX], o2[BTLDAC_OUT_MAX];

	CHECK(btldac_enc_open(&e, BTLDAC_MODE_330, 672) == BTLDAC_E_MTU && !e.open, "a limit of 672: refused");
	CHECK(btldac_enc_open(&e, BTLDAC_MODE_330, 678) == BTLDAC_E_MTU && btldac_enc_open(&e, BTLDAC_MODE_330, 0) == BTLDAC_E_MTU, "678 and 0 too");
	for (int k = 0; k < 2; k++) {
		CHECK(btldac_enc_open(&e, BTLDAC_MODE_330, k ? 1021 : 679) == 0, "open");
		for (uint32_t c = 0; c < 12; c++) {
			noise_block((int16_t *)pcm, c * BTLDAC_FRAME, BTLDAC_FRAME);
			CHECK(btldac_enc_call(&e, pcm, k ? o2 : o1, &len, &nf) == 0, "call");
			if (c == 5)
				k ? (b = len) : (a = len);
		}
		btldac_enc_close(&e);
	}
	CHECK(a == 660 && b == 660 && !memcmp(o1, o2, 660), "679 and 1021 make the same packet");
	/* a change of mode in mid-stream goes on with the new frame size, and back */
	CHECK(btldac_enc_open(&e, BTLDAC_MODE_330, 679) == 0, "open");
	uint32_t seen330 = 0, seen660 = 0, c = 0;
	for (; c < 60; c++) {
		noise_block(pcm, c * BTLDAC_FRAME, BTLDAC_FRAME);
		CHECK(btldac_enc_call(&e, pcm, out, &len, &nf) == 0, "call");
		seen330 += len && nf == 6;
	}
	CHECK(btldac_enc_set_mode(&e, BTLDAC_MODE_660) == 0 && e.eqmid == BTLDAC_EQMID_SQ, "660 asked for");
	for (; c < 160; c++) {
		noise_block(pcm, c * BTLDAC_FRAME, BTLDAC_FRAME);
		CHECK(btldac_enc_call(&e, pcm, out, &len, &nf) == 0, "call after the change");
		if (len) {
			CHECK(len <= BTLDAC_FRAMES_BYTES && out[0] == 0xAA, "a packet of %u bytes after the change", (unsigned)len);
			seen660 += nf == 3;
		}
	}
	CHECK(seen330 >= 9 && seen660 >= 25, "330 packets then 660 packets (%u, %u)", (unsigned)seen330, (unsigned)seen660);
	CHECK(btldac_enc_set_mode(&e, BTLDAC_MODE_330) == 0 && btldac_enc_set_mode(&e, 9) != 0, "back to 330; a mode out of range is refused");
	btldac_enc_close(&e);
}

static void t_abr(void)
{
	static int16_t pcm[BTLDAC_FRAME * 2];
	static uint8_t out[BTLDAC_OUT_MAX];
	BtLdacEnc e;
	uint32_t len, nf, c = 0;
	int lowest = 99, highest = -1;

	CHECK(btldac_enc_open(&e, BTLDAC_MODE_ABR, 679) == 0 && e.eqmid == BTLDAC_EQMID_MQ, "the adaptive mode starts on 330 kb/s (index %d)", e.eqmid);
	CHECK(btldac_enc_abr(&e, 0) >= 0, "adaptive: the step answers with an index");
	/* a quiet queue for a long time: the code climbs, and is held at 660 */
	for (; c < 4000; c++) {
		noise_block(pcm, c * BTLDAC_FRAME, BTLDAC_FRAME);
		CHECK(btldac_enc_call(&e, pcm, out, &len, &nf) == 0, "call");
		if (c % 7 == 0) {
			int q = btldac_enc_abr(&e, 0);

			CHECK(q >= BTLDAC_EQMID_SQ, "index %d with an empty queue: never the top quality (index 0)", q);
			if (q < lowest) lowest = q;
			if (q > highest) highest = q;
		}
		if (len)
			CHECK(len <= BTLDAC_FRAMES_BYTES && e.kbps <= 660 && e.kbps >= 330, "a packet of %u bytes at %d kb/s: the rate is never above 660", (unsigned)len, e.kbps);
	}
	CHECK(lowest == BTLDAC_EQMID_SQ, "with an empty queue the code does climb to 660 kb/s (lowest index %d)", lowest);
	CHECK(e.abr_steps_up >= 1, "a step up was counted");
	/* a deep queue: down to 330 */
	for (int k = 0; k < 400; k++) {
		noise_block(pcm, (c + (uint32_t)k) * BTLDAC_FRAME, BTLDAC_FRAME);
		CHECK(btldac_enc_call(&e, pcm, out, &len, &nf) == 0, "call");
		btldac_enc_abr(&e, 9);
	}
	CHECK(e.eqmid >= BTLDAC_EQMID_MQ && e.kbps <= 330 && e.abr_steps_down >= 1, "a deep queue brings it down to 330 kb/s or below (index %d, %d kb/s, %u steps down)", e.eqmid, e.kbps, (unsigned)e.abr_steps_down);
	/* a fixed mode ignores the adaptive code */
	CHECK(btldac_enc_set_mode(&e, BTLDAC_MODE_660) == 0, "660");
	CHECK(btldac_enc_abr(&e, 9) == -1 && e.eqmid == BTLDAC_EQMID_SQ, "fixed: the adaptive step does nothing");
	btldac_enc_close(&e);
	CHECK(btldac_enc_abr(&e, 0) == -1, "closed: nothing");
}

/* ---- the rings ---------------------------------------------------------------------------------------------- */

static void t_rings(void)
{
	static int16_t blk[512 * 2], got[512 * 2];
	uint8_t frames[BTLDAC_FRAMES_BYTES], out[BTLDAC_FRAMES_BYTES];
	uint32_t nf, seq;

	CHECK(btldac_push_pcm(blk, 512) == 0 && btldac_pop_pkt(out, sizeof(out), &nf, &seq) == 0, "no session: nothing taken, nothing given");
	btldac_ring_open();
	/* whole blocks, in order, across many wraps */
	for (uint32_t k = 0; k < 200; k++) {
		for (int i = 0; i < 1024; i++)
			blk[i] = (int16_t)(k * 1024 + (uint32_t)i);
		CHECK(btldac_push_pcm(blk, 512) == 512, "block %u", (unsigned)k);
		CHECK(btldac_pcm_avail() == 512, "512 frames waiting");
		btldac_pcm_take(got, 512);
		CHECK(!memcmp(blk, got, sizeof(blk)), "block %u out in order", (unsigned)k);
	}
	uint32_t n = 0;
	while (btldac_push_pcm(blk, 512) == 512)
		n++;
	CHECK(n == BTLDAC_PCM_RING_FRAMES / 512 && btldac_pcm_avail() == BTLDAC_PCM_RING_FRAMES, "a full ring: %u blocks", (unsigned)n);
	{
		uint32_t ov, fi, po, dr, ou;

		btldac_ring_info(&ov, &fi, &po, &dr, &ou);
		CHECK(ov == 1 && fi == 200u * 512u + BTLDAC_PCM_RING_FRAMES, "the refused block is counted (%u), the frames taken too (%u)", (unsigned)ov, (unsigned)fi);
	}
	btldac_pcm_take(got, 512);
	CHECK(btldac_push_pcm(blk, 512) == 512 && btldac_push_pcm(blk, 0) == 0 && btldac_push_pcm(NULL, 512) == 0, "room again; no frames or no buffer: nothing");
	/* packets: in order with their numbers and frame counts, refuse when full, small buffer drops */
	btldac_ring_open();
	CHECK(btldac_pkt_depth() == 0 && btldac_pkt_room(), "a new session: empty");
	for (uint32_t k = 0; k < BTLDAC_PKT_RING_SLOTS; k++) {
		memset(frames, (int)k, sizeof(frames));
		CHECK(btldac_pkt_put(frames, 600u + k, 6) == 1, "packet %u in", (unsigned)k);
	}
	CHECK(!btldac_pkt_room() && btldac_pkt_put(frames, 660, 6) == 0 && btldac_pkt_depth() == BTLDAC_PKT_RING_SLOTS, "full: refused");
	CHECK(btldac_pkt_put(frames, 700, 6) == 0, "a packet longer than libldac makes is refused");
	for (uint32_t k = 0; k < 5; k++) {
		uint32_t len = btldac_pop_pkt(out, sizeof(out), &nf, &seq);

		CHECK(len == 600u + k && nf == 6 && seq == k && out[0] == (uint8_t)k && out[len - 1] == (uint8_t)k, "packet %u out: %u bytes, number %u", (unsigned)k, (unsigned)len, (unsigned)seq);
	}
	CHECK(btldac_pkt_trim(4) == BTLDAC_PKT_RING_SLOTS - 5 - 4 && btldac_pkt_depth() == 4, "a trim to 4 drops the oldest");
	CHECK(btldac_pop_pkt(out, sizeof(out), &nf, &seq) == 600u + BTLDAC_PKT_RING_SLOTS - 4 && seq == BTLDAC_PKT_RING_SLOTS - 4, "the oldest left is the one after the trimmed ones");
	CHECK(btldac_pop_pkt(out, 100, &nf, &seq) == 0, "a buffer too small: the packet is dropped and counted");
	{
		uint32_t ov, fi, po, dr, ou;

		btldac_ring_info(&ov, &fi, &po, &dr, &ou);
		CHECK(dr == 1, "one drop counted");
	}
	btldac_ring_open();   /* a new session: what is left is dead */
	CHECK(btldac_pkt_depth() == 0 && btldac_pop_pkt(out, sizeof(out), &nf, &seq) == 0, "the earlier session's packets are skipped");
	btldac_pkt_put(frames, 660, 6);
	CHECK(btldac_pkt_depth() == 1 && btldac_pop_pkt(out, sizeof(out), &nf, &seq) == 660 && seq == 0, "numbers start from 0 again");
	btldac_ring_close();
	CHECK(btldac_push_pcm(blk, 512) == 0, "closed: nothing taken");
}

static volatile int g_prod_done;
static void *producer(void *a)
{
	static int16_t b[512 * 2];

	(void)a;
	for (uint32_t k = 0; k < 20000; k++) {
		for (int i = 0; i < 1024; i++)
			b[i] = (int16_t)(k + (uint32_t)i);
		while (btldac_push_pcm(b, 512) != 512)
			;
	}
	g_prod_done = 1;
	return NULL;
}
static void t_rings_threads(void)
{
	static int16_t got[512 * 2];
	pthread_t t;
	uint32_t k = 0;

	btldac_ring_open();
	pthread_create(&t, NULL, producer, NULL);
	while (k < 20000) {
		if (btldac_pcm_avail() >= 512) {
			btldac_pcm_take(got, 512);
			for (int i = 0; i < 1024; i++)
				CHECK(got[i] == (int16_t)(k + (uint32_t)i), "block %u sample %d", (unsigned)k, i);
			k++;
		}
	}
	pthread_join(t, NULL);
	CHECK(g_prod_done && btldac_pcm_avail() == 0, "20,000 blocks, none lost or out of order");
}

/* ---- the engine in its thread ------------------------------------------------------------------------------ */

static void t_engine_selftest(void)
{
	fs_set(NULL);
	engine_up();
	CHECK(g_thread_cpu == 0x80000 && info().cpu_mask == 0x80000 && log_has("engine ready"), "the thread is on the system core");
	fs_expect('K', 'K', 0);
	CHECK(info().selftest == BTLDAC_ST_NOTRUN && btldac_start(BTLDAC_MODE_330, 679) == BTLDAC_ERR_NOTEST, "before the self-test: a session is refused");
	g_nwrites = 0;
	run_selftest();
#ifndef BAD_HASH_BUILD
	BtLdacInfo i = info();

	CHECK(i.selftest == BTLDAC_ST_PASS && i.st_pkts[0] == 32 && i.st_pkts[1] == 64 && i.st_bytes[0] == 21120 && i.st_bytes[1] == 42240, "the self-test passed: %u", (unsigned)i.selftest);
	CHECK(i.st_hash[0] == (uint32_t)(BTLDAC_ST_HASH_330 >> 32) && i.st_hash[1] == (uint32_t)BTLDAC_ST_HASH_330 && i.st_hash[2] == (uint32_t)(BTLDAC_ST_HASH_660 >> 32), "the hashes are published");
	CHECK(g_nwrites == 2 && g_writes[0][5] == 't' && g_writes[1][5] == 'K', "the record says 't' while it runs and 'K' after");
	CHECK(log_has("self-test passed"), "logged");
	fs_expect('K', 'K', 0);
	btldac_sync(1);
	usleep(20000);
	CHECK(g_nwrites == 2, "the self-test is asked for once");
#else
	CHECK(info().selftest == BTLDAC_ST_FAIL && log_has("self-test FAILED") && btldac_start(BTLDAC_MODE_330, 679) == BTLDAC_ERR_NOTEST, "a wrong hash: the self-test fails, no session");
	fs_expect('K', 'K', 0);
#endif
	engine_down();
}

static void t_engine_session(void)
{
	static int16_t pcm[512 * 2];
	uint8_t out[BTLDAC_FRAMES_BYTES];
	uint32_t nf, seq, got = 0, blk = 0;

	fs_set(NULL);
	engine_up();
	run_selftest();
#ifdef BAD_HASH_BUILD
	return;
#endif
	CHECK(info().selftest == BTLDAC_ST_PASS, "tested");
	CHECK(btldac_start(BTLDAC_MODE_330, 672) == BTLDAC_ERR_MTU && btldac_start(BTLDAC_MODE_330, 0) == BTLDAC_ERR_MTU && c_idle(), "a limit under 679: refused, nothing started");
	CHECK(btldac_start(9, 679) == BTLDAC_ERR_ARG, "a mode that does not exist");
	CHECK(btldac_start(BTLDAC_MODE_330, 679) == BTLDAC_OK, "a session asked for");
	CHECK(btldac_start(BTLDAC_MODE_330, 679) == BTLDAC_ERR_BUSY, "a second one while the first starts");
	wait_for(c_running, "the session");
	fs_expect('S', 'K', 0);
	CHECK(info().prio == 0x13 && g_prio_last == 0x13 && log_has("one below the sender's"), "the thread runs one below the audio sender's (0x12): 0x%x", (unsigned)info().prio);
	/* feed blocks the way the sender does (every 10.67 ms); the packets come out */
	for (; blk < 120; blk++) {
		sig_block(pcm, blk * 512, 512);
		CHECK(btldac_push_pcm(pcm, 512) == 512, "block %u pushed", (unsigned)blk);
		usleep(3000);
		uint32_t len;

		while ((len = btldac_pop_pkt(out, sizeof(out), &nf, &seq)) > 0) {
			CHECK(len == 660 && nf == 6 && seq == got && out[0] == 0xAA, "packet %u: %u bytes, %u frames", (unsigned)got, (unsigned)len, (unsigned)nf);
			got++;
		}
	}
	usleep(30000);
	while (btldac_pop_pkt(out, sizeof(out), &nf, &seq) > 0)
		got++;
	CHECK(got >= 76 && got <= 80, "120 blocks of 512 frames are 480 calls: 80 packets of 6 frames at 330 kb/s, less the encoder's delay: %u", (unsigned)got);
	CHECK(info().pkts_out == got && info().state == BTLDAC_S_RUNNING && info().kbps == 330, "counted; the library's bit rate is published");
	/* the setting changes while it runs: the new frame size follows */
	btldac_set_mode(BTLDAC_MODE_660);
	uint32_t n3 = 0;
	for (int k = 0; k < 60; k++) {
		sig_block(pcm, (blk + (uint32_t)k) * 512, 512);
		btldac_push_pcm(pcm, 512);
		usleep(3000);
		uint32_t len;

		while ((len = btldac_pop_pkt(out, sizeof(out), &nf, &seq)) > 0)
			n3 += nf == 3;
	}
	CHECK(n3 >= 40 && info().mode == BTLDAC_MODE_660 && info().kbps == 660 && info().eqmid == BTLDAC_EQMID_SQ, "660 kb/s reached the running session: %u packets of 3 frames", (unsigned)n3);
	CHECK(info().enc_max_us > 0 && info().heap_peak > 15000 && info().heap_refused == 0, "timing and memory published");
	btldac_stop();
	wait_for(c_idle, "the session to end");
	fs_expect('K', 'K', 0);
	CHECK(btldac_push_pcm(pcm, 512) == 0, "after the end the ring takes nothing");
	/* module stop ends a running session cleanly */
	CHECK(btldac_start(BTLDAC_MODE_ABR, 1021) == BTLDAC_OK, "a session in the adaptive mode");
	wait_for(c_running, "the second session");
	CHECK(info().mode == BTLDAC_MODE_ABR && info().eqmid == BTLDAC_EQMID_MQ, "it starts on 330");
	engine_down();
	fs_expect('K', 'K', 0);
	CHECK(g_state == BTLDAC_S_IDLE && g_prio_last == 0x70, "module stop: the session ended cleanly, the thread's own priority restored");
}

static void t_engine_boot(void)
{
	/* a session left running: held this start, the count up */
	fs_set("SK0");
	engine_up();
	run_selftest();
#ifdef BAD_HASH_BUILD
	return;
#endif
	CHECK(info().hold == 1 && info().crashes == 1 && info().selftest == BTLDAC_ST_PASS && log_has("did not end cleanly"), "held: the last session did not end cleanly");
	CHECK(btldac_start(BTLDAC_MODE_330, 679) == BTLDAC_ERR_HELD, "a session is refused while held");
	fs_expect('K', 'K', 1);
	engine_down();
}

static void t_engine_banned(void)
{
	fs_set("Kt0");
	engine_up();
	CHECK(info().banned == 1 && info().hold == 1, "a self-test left running: the encoder is banned");
	fs_expect('K', 'B', 0);
	run_selftest();
	CHECK(info().selftest == BTLDAC_ST_FAIL && log_has("self-test FAILED") && btldac_start(BTLDAC_MODE_330, 679) == BTLDAC_ERR_HELD, "the self-test does not run it: no LDAC");
	fs_expect('K', 'B', 0);
	engine_down();
}

static void t_engine_record(void)
{
	/* a record that cannot be written: no encoder is run at all */
	fs_set(NULL);
	g_file_write_fails = 1;
	engine_up();
	CHECK(info().hold == 1 && info().guard_ok == 0 && log_has("cannot be written"), "held");
	run_selftest();
	CHECK(info().selftest == BTLDAC_ST_NORUN && btldac_start(BTLDAC_MODE_330, 679) == BTLDAC_ERR_HELD, "no self-test, no session");
	engine_down();
}

static void t_engine_mask_refused(void)
{
	fs_set(NULL);
	g_refuse_mask = 1;
	engine_up();
	CHECK(g_thread_cpu == 0 && info().cpu_mask == 0 && info().cpu_mask_err == 0x80020000u && log_has("mask was refused"), "the default placement when the kernel refuses the mask, said in the log");
	engine_down();
}

static void t_engine_prio_fallback(void)
{
	fs_set(NULL);
	g_list_fail = 1;
	engine_up();
	run_selftest();
#ifdef BAD_HASH_BUILD
	return;
#endif
	CHECK(btldac_start(BTLDAC_MODE_330, 679) == BTLDAC_OK, "start");
	wait_for(c_running, "the session");
	CHECK(info().prio == 0x70 && info().prio_src == BTAAC_PRIO_SRC_FALLBACK && log_has("sender's priority was not found"), "the thread list unreadable: the module's own priority");
	engine_down();
}

typedef struct { const char *name; void (*fn)(void); } Scn;
static const Scn k_scn[] = {
	{ "the packet, the limit, the latency bound, the adaptive clamp, the bench totals", t_packet },
	{ "the crash record and what a start does with it", t_guard },
	{ "the encoder: the self-test's hashes, 7 allocations and none while encoding", t_selftest_encode },
	{ "the encoder: 330 and 660 kb/s packets, 10 s totals, the limit, a change of mode", t_stream_shape },
	{ "the adaptive mode: starts on 330, held at 660, comes down on a deep queue", t_abr },
	{ "the rings", t_rings },
	{ "the rings: a producer and a consumer flat out", t_rings_threads },
	{ "engine: the self-test through the thread, and its record", t_engine_selftest },
	{ "engine: a session, a change of mode, stop, module stop, priority", t_engine_session },
	{ "engine: a session left running holds LDAC off", t_engine_boot },
	{ "engine: a self-test left running bans the encoder", t_engine_banned },
	{ "engine: a record that cannot be written", t_engine_record },
	{ "engine: the system core's mask refused", t_engine_mask_refused },
	{ "engine: the sender's priority not found", t_engine_prio_fallback },
};

int main(void)
{
	int failed = 0;

	for (unsigned i = 0; i < sizeof(k_scn) / sizeof(k_scn[0]); i++) {
		pid_t pid;
		int st;

		fflush(stdout);
		pid = fork();
		if (pid == 0) {
			k_scn[i].fn();
			_exit(0);
		}
		waitpid(pid, &st, 0);
		if (!WIFEXITED(st) || WEXITSTATUS(st)) {
			printf("  FAIL  %s\n", k_scn[i].name);
			failed++;
		} else {
			printf("ok    %s\n", k_scn[i].name);
		}
	}
	printf("test_btldac: %d scenarios, %d failed\n", (int)(sizeof(k_scn) / sizeof(k_scn[0])), failed);
	return failed ? 1 : 0;
}
