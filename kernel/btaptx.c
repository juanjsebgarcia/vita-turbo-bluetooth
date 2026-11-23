/*
 * btaptx.c - the aptX add-on's engine (see btaptx.h): the encoders' state (two static buffers, one for each mode), the self-test, and the crash record. No thread:
 * the encoder is called inline by the audio sender (btaptx_leaf.c).
 *
 * The encoders are the vendored classic and HD ones (third_party/aptxenc, Apache-2.0), built in objects of their own (CMakeLists.txt). They take the memory of their
 * state from the caller: the two buffers below.
 */
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/io/stat.h>

#include "../include/bluetoothturbo.h"
#include "../third_party/aptxenc/classic/include/aptXbtenc.h"
#include "../third_party/aptxenc/hd/include/aptXHDbtenc.h"
#include "btaptx.h"
#include "klog.h"

#define BTAPTX_GUARD_PATH  BTT_DIR "/aptx.guard"

/* One state for each mode (classic aptX, aptX HD): the self-test and a session never overlap, but a session of either can follow the other. The encoders' records are
 * 4,588 and 4,840 bytes on the console (5,008 and 5,256 on a 64-bit host); btaptx_init refuses a build whose record does not fit. */
static uint8_t g_state_mem[2][BTAPTX_STATE_MAX] __attribute__((aligned(16)));

static BtAptxGuard g_guard;
static BtAptxBoot g_boot;
static uint32_t g_guard_ok, g_selftest = BTAPTX_ST_NOTRUN, g_selftest_hd = BTAPTX_ST_NOTRUN, g_st_runs, g_st_us, g_st_bytes, g_st_hd_bytes, g_sessions;
static uint64_t g_st_hash, g_st_hd_hash;
static int g_inited;

static int guard_write(void)
{
	uint8_t b[BTAPTX_G_BYTES];
	SceUID fd;
	int ok;

	btaptx_guard_encode(&g_guard, b);
	ksceIoMkdir(BTT_DIR, 0777);
	fd = ksceIoOpen(BTAPTX_GUARD_PATH, SCE_O_WRONLY | SCE_O_CREAT, 0666);
	if (fd < 0)
		return 0;
	ok = ksceIoWrite(fd, b, sizeof(b)) == (int)sizeof(b);
	ksceIoClose(fd);
	return ok;
}

static void guard_load(void)
{
	uint8_t b[BTAPTX_G_BYTES];
	SceUID fd = ksceIoOpen(BTAPTX_GUARD_PATH, SCE_O_RDONLY, 0);
	int n = 0;

	if (fd >= 0) {
		n = ksceIoRead(fd, b, sizeof(b));
		ksceIoClose(fd);
	}
	if (!btaptx_guard_decode(&g_guard, b, n) && n > 0)
		BTT_KLOG("aptx: the crash record is damaged (%d bytes): starting it afresh", n);
	g_boot = btaptx_guard_boot(&g_guard);
	g_guard_ok = (uint32_t)guard_write();   /* a record that cannot be written protects nothing: then no encoder is run */
	if (g_boot.hold_now)
		BTT_KLOG("aptx: the last aptX session did not end cleanly (%u in a row): no aptX this start", (unsigned)g_guard.count);
	else if (g_boot.hold)
		BTT_KLOG("aptx: held off by the crash record (%u in a row, test byte '%c')", (unsigned)g_guard.count, g_guard.test);
	if (!g_guard_ok)
		BTT_KLOG("aptx: the crash record cannot be written: no aptX this start");
}

/* An encoder's state made new (the first call makes it, every later one resets it: the encoders' init clears the whole record). 0 when it cannot be made. */
static int state_reset(int hd)
{
	void *m = g_state_mem[hd ? 1 : 0];

	/* endian 0: the codewords come back in host order; the leaf packs their bytes (the encoder's own byte swap is wrong for negative codewords) */
	return (hd ? aptxhdbtenc_init(m, 0) : aptxbtenc_init(m, 0)) == 0;
}

void btaptx_init(void)
{
	int sz = SizeofAptxbtenc(), sz_hd = SizeofAptxhdbtenc();

	guard_load();
	btaptx_ctx = 0;
	btaptx_ctx_hd = 0;
	if (sz > (int)BTAPTX_STATE_MAX || !state_reset(0))
		BTT_KLOG("aptx: the encoder's state could not be made (%d bytes, room for %u): no aptX", sz, (unsigned)BTAPTX_STATE_MAX);
	else
		btaptx_ctx = g_state_mem[0];
	if (sz_hd > (int)BTAPTX_STATE_MAX || !state_reset(1))
		BTT_KLOG("aptx: the HD encoder's state could not be made (%d bytes, room for %u): no aptX HD", sz_hd, (unsigned)BTAPTX_STATE_MAX);
	else
		btaptx_ctx_hd = g_state_mem[1];
	g_inited = 1;
	BTT_KLOG("aptx: engine ready (encoder %s, state %d + %d bytes of %u)", aptxbtenc_version(), sz, sz_hd, (unsigned)BTAPTX_STATE_MAX);
}

void btaptx_exit(void)
{
	btaptx_stop();
}

/* One pass of the test signal through an encoder (hd: the HD one): its bytes into `out`, how many, and the time. */
static uint32_t st_encode(int hd, uint8_t *out, uint32_t *us)
{
	static int16_t pcm[BTAPTX_BLOCK_FRAMES * 2u];
	uint32_t t0, blk, n = 0, b, got, want = hd ? BTAPTX_HD_BLOCK_BYTES : BTAPTX_BLOCK_BYTES;

	state_reset(hd);
	btaptx_running = hd ? 2u : 1u;
	t0 = (uint32_t)ksceKernelGetSystemTimeLow();
	for (blk = 0; blk < BTAPTX_ST_FRAMES / BTAPTX_BLOCK_FRAMES; blk++) {
		for (b = 0; b < BTAPTX_BLOCK_FRAMES; b++)
			btaptx_sig(blk * BTAPTX_BLOCK_FRAMES + b, &pcm[2u * b], &pcm[2u * b + 1u]);
		got = hd ? btaptx_encode_hd(pcm, out + n) : btaptx_encode(pcm, out + n);
		if (got != want)
			break;
		n += want;
	}
	btaptx_running = 0;
	*us = (uint32_t)ksceKernelGetSystemTimeLow() - t0;
	state_reset(hd);
	return n;
}

/* Encodes the test signal and compares it with the hash a correct encoder gives; when the classic encoder is right, the same for aptX HD (its own verdict: HD is
 * offered only if both pass). Never while a session runs (the states are shared). */
int btaptx_selftest_run(void)
{
	static uint8_t out[BTAPTX_ST_HD_BYTES];
	int c = btaptx_ctx != 0, chd = btaptx_ctx_hd != 0;
	uint64_t h;
	uint32_t n, hd_us;

	if (btaptx_running)
		return BTAPTX_ST_NORUN;   /* busy: the verdict of the earlier run stands */
	g_st_runs++;
	if (!g_inited || !c || !g_guard_ok || g_boot.banned || g_boot.hold) {
		g_selftest = BTAPTX_ST_NORUN;
		g_selftest_hd = BTAPTX_ST_NORUN;
		return BTAPTX_ST_NORUN;
	}
	g_guard.test = 't';
	if (!guard_write()) {
		g_guard.test = 'K';
		g_selftest = BTAPTX_ST_NORUN;
		g_selftest_hd = BTAPTX_ST_NORUN;
		return BTAPTX_ST_NORUN;
	}
	n = st_encode(0, out, &g_st_us);
	g_st_bytes = n;
	h = btaptx_fnv(BTAPTX_FNV_BASIS, out, n);
	g_st_hash = h;
	g_selftest = (n == BTAPTX_ST_BYTES && h == BTAPTX_ST_HASH) ? BTAPTX_ST_PASS : BTAPTX_ST_FAIL;
	g_selftest_hd = BTAPTX_ST_NORUN;
	if (g_selftest == BTAPTX_ST_PASS && chd) {
		g_st_hd_bytes = st_encode(1, out, &hd_us);
		g_st_hd_hash = btaptx_fnv(BTAPTX_FNV_BASIS, out, g_st_hd_bytes);
		g_selftest_hd = (g_st_hd_bytes == BTAPTX_ST_HD_BYTES && g_st_hd_hash == BTAPTX_ST_HASH_HD) ? BTAPTX_ST_PASS : BTAPTX_ST_FAIL;
		if (g_selftest_hd == BTAPTX_ST_PASS)
			BTT_KLOG("aptx: HD self-test passed: %u us for %u bytes (hash %08x%08x)", (unsigned)hd_us, (unsigned)g_st_hd_bytes, (unsigned)(g_st_hd_hash >> 32), (unsigned)g_st_hd_hash);
		else
			BTT_KLOG("aptx: HD self-test FAILED: %u bytes (want %u), hash %08x%08x (want %08x%08x): no aptX HD", (unsigned)g_st_hd_bytes, (unsigned)BTAPTX_ST_HD_BYTES,
			         (unsigned)(g_st_hd_hash >> 32), (unsigned)g_st_hd_hash, (unsigned)(BTAPTX_ST_HASH_HD >> 32), (unsigned)BTAPTX_ST_HASH_HD);
	}
	g_guard.test = 'K';
	guard_write();
	btaptx_n_blocks = 0;   /* the self-test's blocks are not the session's */
	if (g_selftest == BTAPTX_ST_PASS)
		BTT_KLOG("aptx: self-test passed: %u us for %u bytes (hash %08x%08x)", (unsigned)g_st_us, (unsigned)n, (unsigned)(h >> 32), (unsigned)h);
	else
		BTT_KLOG("aptx: self-test FAILED: %u bytes (want %u), hash %08x%08x (want %08x%08x): no aptX", (unsigned)n, (unsigned)BTAPTX_ST_BYTES, (unsigned)(h >> 32),
		         (unsigned)h, (unsigned)(BTAPTX_ST_HASH >> 32), (unsigned)BTAPTX_ST_HASH);
	return (int)g_selftest;
}

void btaptx_sync(int on)
{
	static int done;

	if (!on || done)
		return;
	done = 1;
	btaptx_selftest_run();
}

int btaptx_start(int hd)
{
	void *c = hd ? btaptx_ctx_hd : btaptx_ctx;

	if (btaptx_running)
		return BTAPTX_ERR_BUSY;
	if (!g_inited || !c)
		return BTAPTX_ERR_NOCTX;   /* (the mode's own context) */
	if (g_boot.hold || g_boot.banned)
		return BTAPTX_ERR_HELD;
	if (g_selftest != BTAPTX_ST_PASS || (hd && g_selftest_hd != BTAPTX_ST_PASS))
		return BTAPTX_ERR_NOTEST;
	btaptx_guard_session_begin(&g_guard);
	if (!guard_write()) {
		g_guard.session = 'K';
		return BTAPTX_ERR_RECORD;
	}
	state_reset(hd);
	btaptx_n_blocks = 0;
	btaptx_n_errors = 0;
	g_sessions++;
	__atomic_store_n(&btaptx_running, hd ? 2u : 1u, __ATOMIC_RELEASE);
	return BTAPTX_OK;
}

void btaptx_stop(void)
{
	if (!__atomic_exchange_n(&btaptx_running, 0u, __ATOMIC_ACQ_REL))
		return;
	btaptx_guard_session_end(&g_guard);
	guard_write();
}

void btaptx_get_info(BtAptxInfo *o)
{
	memset(o, 0, sizeof(*o));
	o->state = btaptx_running ? BTAPTX_S_RUNNING : BTAPTX_S_IDLE;
	o->selftest = g_selftest;
	o->hold = (uint32_t)g_boot.hold;
	o->banned = (uint32_t)g_boot.banned;
	o->crashes = g_guard.count;
	o->guard_ok = g_guard_ok;
	o->ctx_ok = btaptx_ctx != 0;
	o->st_us = g_st_us;
	o->st_bytes = g_st_bytes;
	o->hash_lo = (uint32_t)g_st_hash;
	o->hash_hi = (uint32_t)(g_st_hash >> 32);
	o->st_runs = g_st_runs;
	o->sessions = g_sessions;
	o->blocks = btaptx_n_blocks;
	o->errors = btaptx_n_errors;
	o->selftest_hd = g_selftest_hd;
	o->hd_ctx_ok = btaptx_ctx_hd != 0;
	o->hd_bytes = g_st_hd_bytes;
	o->hd_hash_lo = (uint32_t)g_st_hd_hash;
	o->hd_hash_hi = (uint32_t)(g_st_hd_hash >> 32);
	o->hd_session = btaptx_running == 2u;
}
