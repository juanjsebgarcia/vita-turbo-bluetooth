/*
 * btdisc.c - what the console does when Bluetooth headphones disconnect.
 *
 * Stock: the audio policy thread (SceAVConfig) moves the sound to the speaker, mutes the console (the mute
 * flag, the volume saved and the codec volume set to 0) and tells the registered callbacks that Bluetooth
 * audio is gone, which pauses the system's background music. Two one-instruction changes in that code:
 *
 *   - Pause, Keep Volume: the branch that skips the mute when an external-output flag is set is made
 *     unconditional, so the mute, the saved volume and the codec volume are all skipped, but the
 *     notification to the callbacks stays;
 *   - Play on Speaker: the branch to the whole "Bluetooth audio was lost" code is made a no-op: no mute and
 *     no notification, so what was playing goes on, on the speaker.
 *
 * Both are four-byte data injects into the text (no hook, no code is moved), checked against the stock bytes
 * and the bytes around them, identical on every firmware 3.60-3.74, before they go in; the two are never in
 * at the same time. Another plugin that already made the Play on Speaker change leaves the bytes as ours
 * would be: then the change is found already made and left alone, never written and never removed.
 *
 * They go in only while they are wanted, on the same terms as the other Bluetooth changes (bt_pass in
 * main.c), come out again with the setting, the master switch or module stop, and are heard from the next
 * disconnect: nothing here runs at the moment of it.
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "btdisc.h"
#include "klog.h"

int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr);

#define OFF_SPK_WIN    0xF68u    /* the 16 bytes around the Play on Speaker site                    */
#define OFF_SPK        0xF70u    /* the branch to the "Bluetooth audio was lost" code               */
#define OFF_PAUSE_WIN  0x12DCu   /* the 34 bytes around the Pause, Keep Volume site                  */
#define OFF_PAUSE      0x12E2u   /* the branch over the mute, taken for an external output          */

/* The window of stock code around each site, and the site's replacement. */
static const uint8_t k_spk_win[16] = {
	0x10, 0x9C, 0xDB, 0xF8, 0xE0, 0x00, 0x00, 0x2C, 0x40, 0xF0, 0xB2, 0x80, 0x17, 0x9D, 0x25, 0xB9 };
static const uint8_t k_spk_new[4] = { 0xAF, 0xF3, 0x00, 0x80 };       /* do nothing: the branch is not taken */
static const uint8_t k_pause_win[34] = {
	0xD9, 0xF8, 0x68, 0x30, 0x01, 0x2B, 0x3F, 0xF4, 0x47, 0xAE, 0x09, 0x9C, 0x01, 0x21, 0xD9, 0xF8, 0x78,
	0x20, 0x23, 0x68, 0xC9, 0xE9, 0x38, 0x12, 0x0B, 0xB1, 0x01, 0xF0, 0x32, 0xEF, 0xDB, 0xF8, 0xEC, 0x30 };
static const uint8_t k_pause_new[4] = { 0x0A, 0xE0, 0x00, 0xBF };     /* always skip the mute, on to the notification */

static SceUID gd_mod = -1;
static const volatile uint8_t *gd_text;
static int gd_state = -1;            /* -1 not looked at yet, 0 unusable, 1 ready                      */
static int gd_other_spk;             /* Play on Speaker already made by someone else                    */
static int gd_spk_ok, gd_pause_ok;    /* the site holds the stock bytes (or, for Speaker, the same patch) */
static SceUID gd_inj[BTT_DISC_COUNT] = { -1, -1, -1 };
static int gd_in;                    /* the mode written (0 = none)                                     */
static int gd_failed;                /* the mode whose write was refused or whose code is not recognised */

/* The window `win` (stock) with `site` bytes at `at` replaced by `repl`, compared with the code. */
static int disc_window(const volatile uint8_t *p, const uint8_t *win, int n, int at, const uint8_t *repl)
{
	for (int i = 0; i < n; i++) {
		uint8_t want = i >= at && i < at + 4 && repl ? repl[i - at] : win[i];
		if (p[i] != want)
			return 0;
	}
	return 1;
}

static int disc_find(void)
{
	tai_module_info_t ti;
	uintptr_t base = 0;

	if (gd_state >= 0)
		return gd_state;
	gd_state = 0;
	memset(&ti, 0, sizeof(ti));
	ti.size = sizeof(ti);
	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceAVConfig", &ti) < 0 ||
	    module_get_offset(KERNEL_PID, ti.modid, 0, 0, &base) < 0 || !base) {
		BTT_KLOG("bluetooth disconnect: audio policy code not found - no change");
		return 0;
	}
	gd_mod = ti.modid;
	gd_text = (const volatile uint8_t *)base;
	gd_spk_ok = disc_window(gd_text + OFF_SPK_WIN, k_spk_win, sizeof(k_spk_win), (int)(OFF_SPK - OFF_SPK_WIN), 0);
	gd_other_spk = !gd_spk_ok && disc_window(gd_text + OFF_SPK_WIN, k_spk_win, sizeof(k_spk_win),
	                                      (int)(OFF_SPK - OFF_SPK_WIN), k_spk_new);
	gd_pause_ok = disc_window(gd_text + OFF_PAUSE_WIN, k_pause_win, sizeof(k_pause_win),
	                         (int)(OFF_PAUSE - OFF_PAUSE_WIN), 0);
	BTT_KLOG("bluetooth disconnect: audio policy code%s%s%s", gd_spk_ok || gd_other_spk ? " (Play on Speaker site ok)" : " (Play on Speaker site NOT recognised)",
	         gd_other_spk ? " (already made by another plugin)" : "", gd_pause_ok ? " (Pause, Keep Volume site ok)" : " (Pause, Keep Volume site NOT recognised)");
	gd_state = 1;
	return 1;
}

static void disc_release(int mode)
{
	if (gd_inj[mode] >= 0) {
		taiInjectReleaseForKernel(gd_inj[mode]);
		gd_inj[mode] = -1;
	}
}

void btt_btdisc_sync(int mode)
{
	static const char *const name[BTT_DISC_COUNT] = { "mute", "pause, keep volume", "play on speaker" };

	if (mode < 0 || mode >= BTT_DISC_COUNT)
		mode = BTT_DISC_MUTE;
	if (mode != gd_failed)
		gd_failed = 0;                 /* a refused change may be tried again after the mode changes */
	if (gd_in == mode)
		return;
	if (gd_in) {
		disc_release(gd_in);
		BTT_KLOG("bluetooth disconnect: %s off", name[gd_in]);
		gd_in = 0;
	}
	if (!mode || mode == gd_failed)
		return;
	if (!disc_find() || (mode == BTT_DISC_PAUSE ? !gd_pause_ok : !(gd_spk_ok || gd_other_spk))) {
		gd_failed = mode;
		BTT_KLOG("bluetooth disconnect: %s not possible here (code not recognised)", name[mode]);
		return;
	}
	if (mode == BTT_DISC_SPEAKER && gd_other_spk) {
		gd_in = mode;                  /* in, by someone else: nothing of ours to release */
		BTT_KLOG("bluetooth disconnect: play on speaker already made by another plugin (left alone)");
		return;
	}
	gd_inj[mode] = taiInjectDataForKernel(KERNEL_PID, gd_mod, 0, mode == BTT_DISC_PAUSE ? OFF_PAUSE : OFF_SPK,
	                                     mode == BTT_DISC_PAUSE ? k_pause_new : k_spk_new, 4);
	if (gd_inj[mode] >= 0) {
		gd_in = mode;
		BTT_KLOG("bluetooth disconnect: %s on (from the next disconnect)", name[mode]);
	} else {
		gd_inj[mode] = -1;
		gd_failed = mode;
		BTT_KLOG("bluetooth disconnect: %s failed", name[mode]);
	}
}

void btt_btdisc_restore(void)
{
	for (int m = 0; m < BTT_DISC_COUNT; m++)
		disc_release(m);
	gd_in = 0;
}
