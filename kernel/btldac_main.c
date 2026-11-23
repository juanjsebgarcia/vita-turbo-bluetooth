/*
 * btldac_main.c - the LDAC module's entry (bluetoothturbo_ldac.skprx, listed under *KERNEL after the main module; installing it is the opt-in).
 *
 * It starts the engine's thread and exports one function, btldacGetApi, which the main kernel module looks up once its start-up period is
 * over (kernel/btldac_link.c). It touches nothing of the Bluetooth code and nothing of the main module: with the main module absent or
 * standing aside (its safe start) the engine's thread just sleeps, and nothing runs the self-test.
 *
 * module_stop ends the engine (a running session ends cleanly, the thread is joined, the memory freed) and then asks not to be unloaded:
 * the main module keeps the table's pointers for good, and a module that vanished under them would take the console down. The engine
 * stays safe to call once stopped (a start is refused, the leaves take and give nothing).
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysclib.h>

#include "../include/bluetoothturbo.h"
#include "btldac_api.h"
#include "klog.h"

#ifndef SCE_KERNEL_STOP_CANCEL
#define SCE_KERNEL_STOP_CANCEL 1        /* the module stays loaded */
#endif

static const BtLdacApi g_api = {
	.size = sizeof(BtLdacApi),
	.version = BTLDAC_API_VERSION,
	.start = btldac_start,
	.stop = btldac_stop,
	.sync = btldac_sync,
	.set_mode = btldac_set_mode,
	.get_info = btldac_get_info,
	.push_pcm = btldac_push_pcm,
	.pop_pkt = btldac_pop_pkt,
	.pkt_trim = btldac_pkt_trim,
	.pkt_depth = btldac_pkt_depth,
};

/* The module's one export (bluetoothturbo_ldac.yml). */
const BtLdacApi *btldacGetApi(void)
{
	return &g_api;
}

#ifndef BTT_HOST_TEST
void _start() __attribute__ ((weak, alias("module_start")));
#endif

int module_start(SceSize args, void *argp)
{
	btt_klog_init();
	BTT_KLOG("Bluetooth Turbo LDAC module " BTT_VERSION " starting");
	btldac_init();
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp)
{
	btldac_exit();
	BTT_KLOG("ldac: module stop: the engine is stopped, the module stays loaded");
	return SCE_KERNEL_STOP_CANCEL;
}
