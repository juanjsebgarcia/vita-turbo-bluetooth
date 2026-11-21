/*
 * bthq.c - Bluetooth audio quality, High and Highest: the two channels sent separately (SBC dual channel) at a higher bit
 * rate than the console's joint stereo.
 *
 * The console sends sound to headphones as joint-stereo SBC at the highest bitpool they allow, in packets
 * sized to fit one slot-limited radio packet: stream start allows the 3-slot types only, and a cap on the
 * packet size (per radio rate: 3 Mb/s, 2 Mb/s, basic rate) bounds the bitpool. Two kinds of change here:
 *
 *   - two data injects, each checked against the stock bytes and those around it, identical on every firmware
 *     3.60-3.74: the packet-type constant at stream start (so 5-slot types are allowed) and the 2 Mb/s
 *     packet cap (High only);
 *   - the hook on the routine that applies an A2DP configuration (shared with Stable and Balanced, btqual.c /
 *     btqual_hook.c): it takes joint and stereo out of the capabilities the headphones offered and caps the highest
 *     bitpool for dual channel afterwards.
 *
 * Headphones that do not offer dual channel are left on joint stereo by the hook; if they do and refuse it, or cannot
 * take the larger packets of Highest, there is no sound or a reconnect loop: switching back to Standard (and the
 * headphones off and on) undoes it. A change of level is heard from the next connection of the headphones.
 * Everything is put back byte for byte at any other level, with the master switch off and at module stop.
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "btqual.h"
#include "btqual_hook.h"
#include "bthq.h"
#include "klog.h"

int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr);

#define OFF_PTYPE   0x7050u   /* movw r1,#0x3c18: the packet types stream start allows on a 3 Mb/s or 2 Mb/s radio link (3-slot) */
#define OFF_CAP2M   0x7566u   /* movw r3,#0x15d: the packet cap of headphones limited to the 2 Mb/s rate (349)                    */
#define WIN_PTYPE   0x7048u   /* the 16 bytes around the packet types                                                         */
#define WIN_CAP2M   0x7560u   /* the 16 bytes around the cap                                                                   */

static const uint8_t k_ptype_win[16] = {
	0xDE, 0xF8, 0x4C, 0x30, 0x4C, 0xF6, 0x18, 0x40, 0x43, 0xF6, 0x18, 0x41, 0x02, 0xF0, 0x80, 0x05 };
static const uint8_t k_cap2m_win[16] = {
	0x08, 0x92, 0x00, 0x22, 0x09, 0x92, 0x40, 0xF2, 0x5D, 0x13, 0xDD, 0xE9, 0x08, 0x01, 0x01, 0x43 };

/* movw r1,#0x2c18 (2-DH5 allowed), movw r1,#0x0c18 (2-DH5 and 3-DH5 allowed), movw r3,#0x295 (661: one 2-DH5 packet) */
static const uint8_t k_ptype_2dh5[4] = { 0x42, 0xF6, 0x18, 0x41 };
static const uint8_t k_ptype_5slot[4] = { 0x40, 0xF6, 0x18, 0x41 };
static const uint8_t k_cap2m_raised[4] = { 0x40, 0xF2, 0x95, 0x23 };

static SceUID gh_mod = -1;
static const volatile uint8_t *gh_text;
static int gh_state = -1;                 /* -1 not looked at yet, 0 unusable, 1 stock bytes found */
static SceUID gh_inj_ptype = -1, gh_inj_cap = -1;
static int gh_mode;                       /* what is in place: 0 none, BTHQ_HIGH, BTHQ_HIGHEST     */
static int gh_failed;                     /* the mode whose change was refused (not tried again until the level changes) */

static int hq_window(const volatile uint8_t *p, const uint8_t *win)
{
	for (int i = 0; i < 16; i++)
		if (p[i] != win[i])
			return 0;
	return 1;
}

static int hq_find(void)
{
	tai_module_info_t ti;
	uintptr_t base = 0;

	if (gh_state >= 0)
		return gh_state;
	gh_state = 0;
	memset(&ti, 0, sizeof(ti));
	ti.size = sizeof(ti);
	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceBt", &ti) < 0 ||
	    module_get_offset(KERNEL_PID, ti.modid, 0, 0, &base) < 0 || !base) {
		BTT_KLOG("bluetooth audio quality: Bluetooth code not found - no change");
		return 0;
	}
	gh_mod = ti.modid;
	gh_text = (const volatile uint8_t *)base;
	if (!hq_window(gh_text + WIN_PTYPE, k_ptype_win) || !hq_window(gh_text + WIN_CAP2M, k_cap2m_win)) {
		BTT_KLOG("bluetooth audio quality: packet code not recognised - no change");
		return 0;
	}
	gh_state = 1;
	return 1;
}

static void hq_release(void)
{
	if (gh_inj_cap >= 0) {
		taiInjectReleaseForKernel(gh_inj_cap);
		gh_inj_cap = -1;
	}
	if (gh_inj_ptype >= 0) {
		taiInjectReleaseForKernel(gh_inj_ptype);
		gh_inj_ptype = -1;
	}
}

/* Put in what `mode` needs; all or nothing. */
static int hq_apply(int mode)
{
	if (!hq_find())
		return -1;
	gh_inj_ptype = taiInjectDataForKernel(KERNEL_PID, gh_mod, 0, OFF_PTYPE, mode == BTHQ_HIGHEST ? k_ptype_5slot : k_ptype_2dh5, 4);
	if (gh_inj_ptype < 0) {
		gh_inj_ptype = -1;
		return -1;
	}
	if (mode == BTHQ_HIGH) {
		gh_inj_cap = taiInjectDataForKernel(KERNEL_PID, gh_mod, 0, OFF_CAP2M, k_cap2m_raised, 4);
		if (gh_inj_cap < 0) {
			gh_inj_cap = -1;
			hq_release();
			return -1;
		}
	}
	if (btt_btqual_hq_hook() < 0) {
		hq_release();
		return -1;
	}
	return 0;
}

void btt_bthq_sync(int level)
{
	int mode = level == BTT_Q_HIGH ? (int)BTHQ_HIGH : level == BTT_Q_HIGHEST ? (int)BTHQ_HIGHEST : 0;

	if (mode != gh_failed)
		gh_failed = 0;
	if (mode == gh_mode)
		return;
	if (gh_mode) {
		btt_btqual_hq_set(0);       /* the hook (if in) passes through while the bytes change */
		hq_release();
		BTT_KLOG("bluetooth audio quality: %s off", gh_mode == (int)BTHQ_HIGH ? "high" : "highest");
		gh_mode = 0;
	}
	if (!mode || mode == gh_failed)
		return;
	if (hq_apply(mode) < 0) {
		gh_failed = mode;
		BTT_KLOG("bluetooth audio quality: %s refused - no change", mode == (int)BTHQ_HIGH ? "high" : "highest");
		return;
	}
	gh_mode = mode;
	btt_btqual_hq_set(mode);
	BTT_KLOG("bluetooth audio quality: %s on (from the next connection of the headphones)", mode == (int)BTHQ_HIGH ? "high" : "highest");
}

void btt_bthq_restore(void)
{
	btt_btqual_hq_set(0);
	hq_release();
	gh_mode = 0;
}
