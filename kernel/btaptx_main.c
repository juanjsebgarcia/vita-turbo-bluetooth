/*
 * btaptx_main.c - the aptX add-on's entry (bluetoothturbo_aptx.skprx, listed under *KERNEL after the main module; installing it is the opt-in).
 *
 * It sets up the encoder and the crash record and exports one function, btaptxGetApi, which the main kernel module looks up once its start-up period is
 * over (kernel/btaptx_link.c). It touches nothing of the Bluetooth code and nothing of the main module. module_stop ends a running session and asks not to
 * be unloaded: the main module keeps the table's pointers for good (the same rule as the AAC module's, kernel/btaac_main.c).
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysclib.h>

#include "../include/bluetoothturbo.h"
#include "btaptx_api.h"
#include "klog.h"

#ifndef SCE_KERNEL_STOP_CANCEL
#define SCE_KERNEL_STOP_CANCEL 1        /* the module stays loaded */
#endif

static const BtAptxApi g_api = {
	.size = sizeof(BtAptxApi),
	.version = BTAPTX_API_VERSION,
	.start = btaptx_start,
	.stop = btaptx_stop,
	.sync = btaptx_sync,
	.get_info = btaptx_get_info,
	.encode = btaptx_encode,
	.encode_hd = btaptx_encode_hd,
};

/* The module's one export (bluetoothturbo_aptx.yml). */
const BtAptxApi *btaptxGetApi(void)
{
	return &g_api;
}

#ifndef BTT_HOST_TEST
void _start() __attribute__ ((weak, alias("module_start")));
#endif

int module_start(SceSize args, void *argp)
{
	btt_klog_init();
	BTT_KLOG("Bluetooth Turbo aptX module " BTT_VERSION " starting");
	btaptx_init();
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp)
{
	btaptx_exit();
	BTT_KLOG("aptx: module stop: the session is ended, the module stays loaded");
	return SCE_KERNEL_STOP_CANCEL;
}
