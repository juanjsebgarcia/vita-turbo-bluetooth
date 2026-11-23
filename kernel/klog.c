/*
 * klog.c - see klog.h. A mutex serialises the file appends.
 */
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/io/stat.h>

#include "../include/bluetoothturbo.h"
#include "klog.h"

static SceUID g_mtx = -1;
static int    g_bytes;
static int    g_disabled;

void btt_klog_init(void)
{
	ksceIoMkdir("ur0:data", 0777);
	ksceIoMkdir(BTT_DIR, 0777);
	/* Keep the previous start's log: after a failed start, it's the one that
	 * says what happened. */
	ksceIoRemove(BTT_DIR "/bluetoothturbo_kernel.prev.log");
	ksceIoRename(BTT_KLOG_PATH, BTT_DIR "/bluetoothturbo_kernel.prev.log");
	g_mtx = ksceKernelCreateMutex("BluetoothTurboLog", 0, 0, NULL);
}

void btt_klog_line(const char *line)
{
	if (g_disabled || g_mtx < 0)
		return;
	ksceKernelLockMutex(g_mtx, 1, NULL);
	int n = strnlen(line, 250);
	if (g_bytes + n + 1 > BTT_KLOG_MAX_BYTES) {
		g_disabled = 1;
	} else {
		SceUID fd = ksceIoOpen(BTT_KLOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
		if (fd >= 0) {
			ksceIoWrite(fd, line, n);
			ksceIoWrite(fd, "\n", 1);
			ksceIoClose(fd);
			g_bytes += n + 1;
		}
	}
	ksceKernelUnlockMutex(g_mtx, 1);
}
