/*
 * btqual.c - Bluetooth audio quality, Stable and Balanced: a cap on the SBC bitpool, so a packet takes less air time.
 *
 * The Bluetooth code's encoder is set up when an A2DP stream starts, always with the highest bitpool the
 * headphones allow (up to 60 on a fast link, 53 on the common headphones), which it keeps in the
 * connection. Balanced caps it at 45 and Stable at 37 (the value the code itself uses for headphones that
 * can only do a slow link), which cuts the bit rate by about a fifth and a third. It does not go below the
 * lowest bitpool the headphones allow. Standard is the console's own, and then nothing is hooked at all (unless High or
 * Highest, bthq.c, share the hook).
 *
 * One hook (btqual_hook.c) on the routine that applies an A2DP codec configuration to a connection, at text
 * offset 0x732C: it runs the original and then lowers the connection's stored highest bitpool, the same
 * place the code itself lowers it to fit a packet. The first eight bytes (the whole span the hook overwrites:
 * a push and an address load, neither with a conditional instruction) and the bytes after them are checked
 * against the stock code before the hook goes in; the two address loads hold numbers the loader fills in,
 * so those are masked. The routine runs once when headphones are connected, so the hook costs nothing while
 * music plays. A change of level is heard from the next connection of the headphones (switch them off and
 * on): the stored value is read, and written, only when the configuration is applied.
 *
 * Installed only while a level that needs it is wanted, on the same terms as the other Bluetooth
 * changes (bt_pass in main.c). Switching back to Standard sets the cap to none at once and leaves the hook
 * in, writing nothing: a hook is only released at module stop.
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "btqual.h"
#include "btqual_hook.h"
#include "klog.h"

int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr);

#define OFF_CONFIG    0x732Cu    /* the routine that applies an A2DP codec configuration to a connection */
#define OFF_CFG_TAIL  0x7338u    /* its code after the address loads                      */
#define BP_BALANCED   45u
#define BP_STABLE     37u

/* push.w {r4-fp,lr}; movw r7,#imm; movt r7,#imm (the immediates are the loader's); then the code after them */
static const uint8_t k_cfg_head[12] = { 0x2D, 0xE9, 0xF0, 0x4F, 0x40, 0xF2, 0x00, 0x07, 0xC0, 0xF2, 0x00, 0x07 };
static const uint8_t k_cfg_mask[12] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFB, 0x00, 0x0F, 0xF0, 0xFB, 0x00, 0x0F };
static const uint8_t k_cfg_tail[14] = { 0x06, 0x46, 0x11, 0xF0, 0x08, 0x08, 0x8D, 0xB0, 0x38, 0x68, 0x14, 0x46, 0x1D, 0x46 };

static SceUID g_qual_uid = -1;
static int g_qual_failed;                 /* tried and refused: not tried again until it is switched off and on */

static int qual_same(const volatile uint8_t *p, const uint8_t *b, const uint8_t *mask, int n)
{
	for (int i = 0; i < n; i++)
		if ((p[i] & (mask ? mask[i] : 0xFF)) != (b[i] & (mask ? mask[i] : 0xFF)))
			return 0;
	return 1;
}

static int qual_install(void)
{
	tai_module_info_t ti;
	uintptr_t base = 0;
	SceUID uid;

	memset(&ti, 0, sizeof(ti));
	ti.size = sizeof(ti);
	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceBt", &ti) < 0 ||
	    module_get_offset(KERNEL_PID, ti.modid, 0, 0, &base) < 0 || !base) {
		BTT_KLOG("bluetooth quality: Bluetooth code not found - no cap");
		return -1;
	}
	if (!qual_same((const volatile uint8_t *)base + OFF_CONFIG, k_cfg_head, k_cfg_mask, sizeof(k_cfg_head)) ||
	    !qual_same((const volatile uint8_t *)base + OFF_CFG_TAIL, k_cfg_tail, 0, sizeof(k_cfg_tail))) {
		BTT_KLOG("bluetooth quality: configuration code not recognised - no cap");
		return -1;
	}
	btqual_ref = 0;
	uid = taiHookFunctionOffsetForKernel(KERNEL_PID, (tai_hook_ref_t *)&btqual_ref, ti.modid, 0,
	                                     OFF_CONFIG, 1, (const void *)btqual_hook);
	if (uid < 0) {
		BTT_KLOG("bluetooth quality: hook failed 0x%08x - no cap", (unsigned)uid);
		btqual_ref = 0;
		return -1;
	}
	g_qual_uid = uid;
	BTT_KLOG("bluetooth quality: hook on");
	return 0;
}

void btt_btqual_sync(int level)
{
	uint32_t cap = level == BTT_Q_BALANCED ? BP_BALANCED : level == BTT_Q_STABLE ? BP_STABLE : 0;

	if (!cap) {
		g_qual_failed = 0;   /* a failed install may be tried again after off and on */
		if (btqual_cap) {
			btqual_cap = 0;   /* the console's own bitpool; the hook stays in */
			BTT_KLOG("bluetooth quality: no cap (hook stays in, passing values through)");
		}
		return;
	}
	if (g_qual_failed)
		return;
	if (g_qual_uid < 0 && qual_install() < 0) {
		g_qual_failed = 1;
		return;
	}
	if (btqual_cap != cap) {
		btqual_cap = cap;
		BTT_KLOG("bluetooth quality: bitpool cap %u from the next connection", (unsigned)cap);
	}
}

int btt_btqual_hq_hook(void)
{
	if (g_qual_uid < 0 && qual_install() < 0)
		return -1;
	return 0;
}

void btt_btqual_hq_set(int mode)
{
	btqual_hq = (uint32_t)mode;
}

void btt_btqual_restore(void)
{
	btqual_cap = 0;
	btqual_hq = 0;
	if (g_qual_uid >= 0) {
		taiHookReleaseForKernel(g_qual_uid, btqual_ref);
		g_qual_uid = -1;
	}
	for (int i = 0; i < 100 && btqual_in; i++)
		ksceKernelDelayThread(1000);
	btqual_ref = 0;
}
