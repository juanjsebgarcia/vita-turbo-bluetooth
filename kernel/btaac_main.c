/*
 * btaac_main.c - the AAC module's entry (bluetoothturbo_aac.skprx, listed under *KERNEL after the main module; installing it is the opt-in).
 *
 * It starts the engine's thread and exports one function, btaacGetApi, which the main kernel module looks up once its start-up period is
 * over (kernel/btaac_link.c). It touches nothing of the Bluetooth code and nothing of the main module: with the main module absent or
 * standing aside (its safe start) the engine's thread just sleeps, and nothing runs the self-test.
 *
 * module_stop ends the engine (a running session ends cleanly, the thread is joined, the memory freed) and then asks not to be unloaded:
 * the main module keeps the table's pointers for good, and a module that vanished under them would take the console down. The engine
 * stays safe to call once stopped (a start is refused, the leaves take and give nothing).
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysclib.h>

#include "../include/bluetoothturbo.h"
#include "btaac_api.h"
#include "klog.h"

#ifndef SCE_KERNEL_STOP_CANCEL
#define SCE_KERNEL_STOP_CANCEL 1        /* the module stays loaded */
#endif

static const BtAacApi g_api = {
	.size = sizeof(BtAacApi),
	.version = BTAAC_API_VERSION,
	.start = btaac_start,
	.stop = btaac_stop,
	.sync = btaac_sync,
	.get_info = btaac_get_info,
	.push_pcm = btaac_push_pcm,
	.pop_au = btaac_pop_au,
	.au_trim = btaac_au_trim,
	.au_depth = btaac_au_depth,
};

/* The module's one export (bluetoothturbo_aac.yml). */
const BtAacApi *btaacGetApi(void)
{
	return &g_api;
}

#ifndef BTT_HOST_TEST
void _start() __attribute__ ((weak, alias("module_start")));
#endif

int module_start(SceSize args, void *argp)
{
	btt_klog_init();
	BTT_KLOG("Bluetooth Turbo AAC module " BTT_VERSION " starting");
	btaac_init();
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp)
{
	btaac_exit();
	BTT_KLOG("aac: module stop: the engine is stopped, the module stays loaded");
	return SCE_KERNEL_STOP_CANCEL;
}
