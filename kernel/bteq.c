/*
 * bteq.c - Bluetooth sound: the sound preset, and the channel mode and balance, applied by the CPU
 * to the audio on its way to Bluetooth headphones.
 *
 * The audio sender (SceAVConfig) calls SceBt's ksceBtSendAudio once per block of 512 frames.
 * This hooks SceAVConfig's own import of that function - one import slot, no code moved - with
 * bteq_hook.c, which filters a block of the right size and passes it on. Present once in
 * SceAVConfig, under the same library and function numbers, on every firmware 3.60-3.74.
 *
 * The hook goes in only while it is wanted (Bluetooth Turbo on, the preset not flat or the channel
 * mode or balance changed), on the same terms as the other Bluetooth changes (see bt_pass in
 * main.c). When it stops being wanted the filter is made flat at once and the hook stays in,
 * passing every block through untouched: a hook in the sender's path is only released at module
 * stop.
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "bteq.h"
#include "bteq_hook.h"
#include "btaac_stream.h"
#include "hpgains.h"
#include "klog.h"

#define LIB_BT_FOR_DRIVER  0xD48CA62D   /* SceBtForDriver, as SceAVConfig imports it */
#define NID_BT_SEND_AUDIO  0x47F19727   /* ksceBtSendAudio                           */

static SceUID g_hook = -1;
static int g_failed;              /* tried and failed: not retried until it is switched off and on */
static int g_have;                /* a set has been published for g_gain             */
static int g_gain[BQ_BANDS];
static int g_mode, g_bal = BQ_BAL_CENTRE;   /* the channel stage that was published                */
static int g_logged_block;        /* the first filtered block of this install is in the log */

/* Take the hook out. A call may still be inside it (the sender's thread, at most a block's
 * time): wait for it to leave before the module can go. The reference is cleared last. */
static void uninstall(void)
{
	if (g_hook < 0)
		return;
	taiHookReleaseForKernel(g_hook, bteq_ref);
	g_hook = -1;
	for (int i = 0; i < 100 && bteq_inside; i++)
		ksceKernelDelayThread(1000);
	bteq_ref = 0;
	BTT_KLOG("bluetooth sound: filter off (%u blocks filtered, %u passed on flat, %u memory resets)",
	        (unsigned)bteq_eq.n_filtered, (unsigned)bteq_eq.n_bypassed, (unsigned)bteq_eq.n_resets);
	g_have = 0;
	g_mode = BQ_MIX_STEREO;
	g_bal = BQ_BAL_CENTRE;
	g_logged_block = 0;
}

/* Make `gain` the filter, if it is not already. Returns 0 if it is in place, -1 if the sender's
 * thread was still on the old set (try again on the next pass). */
static int apply(const int *gain, int mode, int bal)
{
	if (g_have && !memcmp(gain, g_gain, sizeof(g_gain)) && mode == g_mode && bal == g_bal)
		return 0;
	BqSet s;
	int n = bq_plan(gain, &s);
	bq_plan_mix(&s, mode, bal);
	if (bq_publish(&bteq_eq, &s) < 0)
		return -1;
	memcpy(g_gain, gain, sizeof(g_gain));
	g_mode = mode;
	g_bal = bal;
	g_have = 1;
	char pre[32];
	if (s.pre_half_db)
		snprintf(pre, sizeof(pre), "-%d.%d dB", (int)(s.pre_half_db / 2), (int)(s.pre_half_db & 1) * 5);
	else
		snprintf(pre, sizeof(pre), "0 dB");   /* a flat filter: never "-0.0 dB" */
	BTT_KLOG("bluetooth sound: bands [%d %d %d %d %d] dB, %d running, pre-gain %s, channels %d, balance %d",
	        gain[0], gain[1], gain[2], gain[3], gain[4], n, pre, mode, bal);
	return 0;
}

void btt_bteq_sync(const int *settings)
{
	int gain[BQ_BANDS], mode, bal;
	int want = btt_hp_gains(settings, gain);   /* zeros when Bluetooth Turbo is off */

	want |= btt_hp_mix(settings, &mode, &bal); /* stereo, centre when Bluetooth Turbo is off */
	want |= btt_aac_stream_wants_pcm_hook();   /* AAC streaming hands the engine every block from this hook (a flat filter passes them on untouched) */
	if (!want) {
		g_failed = 0;   /* a failed install may be tried again after off and on */
		/* Flat, and the hook stays: a hook in the sender's path is never released while
		 * the module runs (it passes every block straight through instead). */
		if (g_hook >= 0)
			apply(gain, mode, bal);
		return;
	}
	if (g_failed)
		return;

	if (g_hook < 0) {
		/* Nothing is running in the hook yet: the sets and the filter memory start clean. */
		bq_init(&bteq_eq);
		g_have = 0;
	}
	if (apply(gain, mode, bal) < 0)
		return;
	if (g_hook < 0) {
		bteq_ref = 0;
		g_hook = taiHookFunctionImportForKernel(KERNEL_PID, (tai_hook_ref_t *)&bteq_ref,
		                                        "SceAVConfig", LIB_BT_FOR_DRIVER, NID_BT_SEND_AUDIO,
		                                        bteq_hook);
		if (g_hook < 0) {
			BTT_KLOG("bluetooth sound: hook failed 0x%08x - no filter", (unsigned)g_hook);
			g_hook = -1;
			bteq_ref = 0;
			g_failed = 1;
			g_have = 0;
			return;
		}
		BTT_KLOG("bluetooth sound: filter on (uid=0x%08x)", (unsigned)g_hook);
	}
	if (!g_logged_block && bteq_eq.n_filtered) {
		g_logged_block = 1;
		BTT_KLOG("bluetooth sound: first block filtered");
	}
}

int btt_bteq_hook_in(void)
{
	return g_hook >= 0;
}

void btt_bteq_restore(void)
{
	/* Module stop: flat first, so the last blocks go through untouched, then out. */
	if (g_hook >= 0) {
		static const int flat[BQ_BANDS];
		apply(flat, BQ_MIX_STEREO, BQ_BAL_CENTRE);
	}
	uninstall();
}
