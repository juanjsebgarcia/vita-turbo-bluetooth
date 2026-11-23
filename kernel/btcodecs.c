/*
 * btcodecs.c - the headphones' codec lists, kept (see btcdcfile.h). The reading is the signalling hook's (kernel/btaac_hook.c); this is the worker's side: the
 * file at start-up and each finished list after it. A file that is missing or does not hold together is an empty store, never an error.
 */
#include <psp2kern/io/fcntl.h>
#include <psp2kern/io/stat.h>
#include <psp2kern/kernel/sysclib.h>

#include "../include/bluetoothturbo.h"
#include "btaac_hook.h"
#include "btcdcfile.h"
#include "btcodecs.h"
#include "klog.h"

static BtCdcStore g_store;
static int g_loaded;
static uint32_t g_seen_seq;

static void store_load(void)
{
	uint8_t buf[BTCDC_FILE_MAX + 1u];
	SceUID fd = ksceIoOpen(BTT_CODECS_FILE, SCE_O_RDONLY, 0);
	int n;

	btcdc_store_clear(&g_store);
	if (fd < 0)
		return;
	n = ksceIoRead(fd, buf, sizeof(buf));
	ksceIoClose(fd);
	if (n > 0 && btcdc_store_load(&g_store, buf, (uint32_t)n))
		BTT_KLOG("codecs: %u headphones' lists read from the file", (unsigned)(btcdc_get32(buf + 8)));
	else if (n > 0)
		BTT_KLOG("codecs: the file (%d bytes) did not hold together: starting empty", n);
}

static void store_save(void)
{
	uint8_t buf[BTCDC_FILE_MAX];
	uint32_t n = btcdc_store_save(&g_store, buf);
	SceUID fd;

	ksceIoMkdir(BTT_DIR, 0777);
	fd = ksceIoOpen(BTT_CODECS_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0) {
		BTT_KLOG("codecs: the file could not be written (0x%08x)", (unsigned)fd);
		return;
	}
	if (ksceIoWrite(fd, buf, n) != (int)n)
		BTT_KLOG("codecs: the file was not written whole");
	ksceIoClose(fd);
}

void btt_codecs_pass(void)
{
	uint32_t seq, lo, hi, mask;

	if (!g_loaded) {
		g_loaded = 1;
		store_load();
	}
	/* the hook makes done_seq odd while it writes the fields and even when it is done: a copy is whole when the same even number is seen before and after
	 * (a list finished in between is taken at the next pass) */
	seq = __atomic_load_n(&btaac_cdc.done_seq, __ATOMIC_ACQUIRE);
	if (seq == g_seen_seq || (seq & 1u))
		return;
	lo = btaac_cdc.done_lo;
	hi = btaac_cdc.done_hi;
	mask = btaac_cdc.done_mask;
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	if (__atomic_load_n(&btaac_cdc.done_seq, __ATOMIC_RELAXED) != seq)
		return;
	g_seen_seq = seq;
	if (btcdc_store_put(&g_store, lo, hi, mask)) {
		BTT_KLOG("codecs: headphones %04x%08x list: codecs 0x%04x", (unsigned)hi, (unsigned)lo, (unsigned)mask);
		store_save();
	}
}

uint32_t btt_codecs_lookup(uint32_t lo, uint32_t hi)
{
	return btcdc_store_get(&g_store, lo, hi);
}
