/*
 * btldac_log.c - the LDAC module's own log, ur0:data/bluetoothturbo/bluetoothturbo_ldac.log (the main module's logger belongs to the main
 * module: it renames its file at start). Same interface as klog.h, small and capped; a mutex serialises the appends.
 */
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/io/stat.h>

#include "../include/bluetoothturbo.h"
#include "klog.h"

#define LDAC_LOG_PATH   BTT_DIR "/bluetoothturbo_ldac.log"
#define LDAC_LOG_PREV   BTT_DIR "/bluetoothturbo_ldac.prev.log"
#define LDAC_LOG_MAX    (16 * 1024)

static SceUID g_mtx = -1;
static int    g_bytes;
static int    g_disabled;

void btt_klog_init(void)
{
	ksceIoMkdir("ur0:data", 0777);
	ksceIoMkdir(BTT_DIR, 0777);
	ksceIoRemove(LDAC_LOG_PREV);
	ksceIoRename(LDAC_LOG_PATH, LDAC_LOG_PREV);
	g_mtx = ksceKernelCreateMutex("BluetoothTurboLdacLog", 0, 0, NULL);
}

void btt_klog_line(const char *line)
{
	if (g_disabled || g_mtx < 0)
		return;
	ksceKernelLockMutex(g_mtx, 1, NULL);
	int n = strnlen(line, 190);
	if (g_bytes + n + 1 > LDAC_LOG_MAX) {
		g_disabled = 1;
	} else {
		SceUID fd = ksceIoOpen(LDAC_LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
		if (fd >= 0) {
			ksceIoWrite(fd, line, n);
			ksceIoWrite(fd, "\n", 1);
			ksceIoClose(fd);
			g_bytes += n + 1;
		}
	}
	ksceKernelUnlockMutex(g_mtx, 1);
}
