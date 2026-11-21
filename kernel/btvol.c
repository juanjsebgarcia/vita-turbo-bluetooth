/*
 * btvol.c - Bluetooth volume: the volume curve and the maximum, for Bluetooth headphones.
 *
 * The sound policy thread of SceAVConfig tells the headphones the volume with SceBt's
 * ksceBtAvrcpSendVolume (a number 0..127 for each of the console's 31 steps) and reads what the
 * headphones report with ksceBtAvrcpReadVolume. This hooks SceAVConfig's own imports of those two
 * functions - two import slots, no code moved - with btvol_hook.c, which replaces the number sent
 * by the table's entry for that step and turns the number reported back into the console's own, so
 * the headphones and the volume slider agree. Both are present once in SceAVConfig, under the same
 * library number as the sound hook's, on every firmware 3.60-3.74.
 *
 * Headphones that do not use absolute volume ignore the number sent, so for them the setting does
 * nothing. The headphones hear a new setting from the next volume change.
 *
 * The hooks go in only while they are wanted (Bluetooth Turbo on, the curve or the maximum not
 * Standard / 100 %), on the same terms as the other Bluetooth changes (see bt_pass in main.c). When
 * they stop being wanted the table is switched to the console's own at once and the hooks stay in,
 * passing every value through: a hook is only released at module stop.
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "btvol.h"
#include "btvol_hook.h"
#include "btvol_map.h"
#include "hpgains.h"
#include "klog.h"

#define LIB_BT_FOR_DRIVER    0xD48CA62D   /* SceBtForDriver, as SceAVConfig imports it */
#define NID_AVRCP_SEND_VOL   0x7689DA3D   /* ksceBtAvrcpSendVolume                     */
#define NID_AVRCP_READ_VOL   0xC9C70056   /* ksceBtAvrcpReadVolume                     */

static SceUID g_send = -1, g_read = -1;
static int g_vol_failed;              /* tried and failed: not retried until it is switched off and on */
static uint32_t g_sel;            /* the table that was published                                   */

/* Take both hooks out. A call may still be inside one (a few instructions' time): wait for it to
 * leave before the module can go. The references are cleared last. */
static void vol_uninstall(void)
{
	btvol_sel = 0;
	if (g_send >= 0) {
		taiHookReleaseForKernel(g_send, btvol_send_ref);
		g_send = -1;
	}
	if (g_read >= 0) {
		taiHookReleaseForKernel(g_read, btvol_read_ref);
		g_read = -1;
	}
	for (int i = 0; i < 100 && (btvol_in_send || btvol_in_read); i++)
		ksceKernelDelayThread(1000);
	btvol_send_ref = 0;
	btvol_read_ref = 0;
	g_sel = 0;
}

static int install_one(SceUID *uid, volatile tai_hook_ref_t *ref, uint32_t nid, const void *hook)
{
	if (*uid >= 0)
		return 0;
	*ref = 0;
	*uid = taiHookFunctionImportForKernel(KERNEL_PID, (tai_hook_ref_t *)ref, "SceAVConfig",
	                                      LIB_BT_FOR_DRIVER, nid, hook);
	if (*uid < 0) {
		BTT_KLOG("bluetooth volume: hook 0x%08x failed 0x%08x", (unsigned)nid, (unsigned)*uid);
		*uid = -1;
		*ref = 0;
		return -1;
	}
	return 0;
}

void btt_btvol_sync(const int *settings)
{
	uint32_t sel = btt_hp_volsel(settings);   /* 0 when Bluetooth Turbo is off */

	if (!sel) {
		g_vol_failed = 0;   /* a failed install may be tried again after off and on */
		if (g_sel) {
			btvol_sel = 0;   /* the console's own numbers, and the hooks stay in */
			g_sel = 0;
			BTT_KLOG("bluetooth volume: standard (hooks stay in, passing values through)");
		}
		return;
	}
	if (g_vol_failed)
		return;

	if (install_one(&g_send, &btvol_send_ref, NID_AVRCP_SEND_VOL, (const void *)btvol_send_hook) < 0 ||
	    install_one(&g_read, &btvol_read_ref, NID_AVRCP_READ_VOL, (const void *)btvol_read_hook) < 0) {
		/* Whichever went in stays in and passes through; the table stays the console's own, so
		 * the number sent and the one read back are never mapped by one hook alone. */
		g_vol_failed = 1;
		return;
	}
	if (sel != g_sel) {
		btvol_sel = sel;
		g_sel = sel;
		BTT_KLOG("bluetooth volume: curve %d, maximum %d %% (table %u)",
		         (int)(sel / BV_MAXES), BTT_VOLMAX_PCT((int)(sel % BV_MAXES)), (unsigned)sel);
	}
}

void btt_btvol_restore(void)
{
	vol_uninstall();
}
