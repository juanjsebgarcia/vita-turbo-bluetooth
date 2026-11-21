/*
 * btname.c - the Bluetooth name other devices see.
 *
 * The Bluetooth code has the console's name once, as the text "PlayStation Vita" and its NUL in read-only
 * code. Two things read it, both when Bluetooth is powered up (switched on, or after standby): the command
 * that sets the local name (it measures the text up to its NUL, copies it and pads to 248 bytes) and the
 * extended inquiry response (which copies exactly 16 bytes of it, with a length byte of 17 and no NUL
 * of its own). A text of up to 16 printable characters, written over those 17 bytes and padded with NULs,
 * is therefore a valid name for both: a shorter one leaves NULs inside the response's name element, which
 * hosts cut at the first NUL (to be confirmed on the console).
 *
 * One 17-byte data inject, checked first against the stock text and the 4 bytes before it and the 11 after
 * (the format texts around it), identical on every firmware 3.60-3.74. It goes in only while a name is set
 * and Bluetooth Turbo is on, is exchanged for another when the name changes, and comes out with the empty
 * name, the master switch or module stop. If the bytes are not the stock text (another plugin renamed
 * the console, or another build) nothing is written.
 *
 * The inject does not touch a Bluetooth that is up: the name is read at its next power-up.
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "btname.h"
#include "klog.h"

int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr);

#define OFF_NAME_WIN   0x1EFA4u   /* the window: the format text before the name, the name, what follows it */
#define OFF_NAME       0x1EFA8u   /* "PlayStation Vita" and its NUL                                           */
#define NAME_BYTES     17

/* "N\0\0\0", "PlayStation Vita\0", three pad bytes, "11 2222\0" */
static const uint8_t k_name_win[32] = {
	0x4E, 0x00, 0x00, 0x00, 0x50, 0x6C, 0x61, 0x79, 0x53, 0x74, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x20, 0x56, 0x69, 0x74,
	0x61, 0x00, 0x00, 0x00, 0x00, 0x31, 0x31, 0x20, 0x32, 0x32, 0x32, 0x32, 0x00 };

static SceUID gn_mod = -1;
static const volatile uint8_t *gn_text;
static int gn_state = -1;                    /* -1 not looked at yet, 0 unusable, 1 stock text found */
static SceUID gn_inj = -1;
static uint8_t gn_in[NAME_BYTES];            /* the text written, valid while gn_inj >= 0 */
static uint8_t gn_failed[NAME_BYTES];        /* the text whose write was refused (not tried again until it changes) */
static int gn_failed_set;

static int name_find(void)
{
	tai_module_info_t ti;
	uintptr_t base = 0;

	if (gn_state >= 0)
		return gn_state;
	gn_state = 0;
	memset(&ti, 0, sizeof(ti));
	ti.size = sizeof(ti);
	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceBt", &ti) < 0 ||
	    module_get_offset(KERNEL_PID, ti.modid, 0, 0, &base) < 0 || !base) {
		BTT_KLOG("bluetooth name: Bluetooth code not found - no change");
		return 0;
	}
	gn_mod = ti.modid;
	gn_text = (const volatile uint8_t *)base;
	for (unsigned i = 0; i < sizeof(k_name_win); i++)
		if (gn_text[OFF_NAME_WIN + i] != k_name_win[i]) {
			BTT_KLOG("bluetooth name: the name text is not the stock text - no change");
			return 0;
		}
	gn_state = 1;
	return 1;
}

/* The 17 bytes for `name`: the characters, then NULs. */
static void name_bytes(const char *name, uint8_t *out)
{
	int n = 0;

	memset(out, 0, NAME_BYTES);
	for (; name && name[n] && n < BTT_NAME_MAX; n++)
		out[n] = (uint8_t)name[n];
}

static void name_release(void)
{
	if (gn_inj >= 0) {
		taiInjectReleaseForKernel(gn_inj);
		gn_inj = -1;
	}
}

void btt_btname_sync(const char *name)
{
	uint8_t want[NAME_BYTES];

	if (!name || !name[0] || !btt_name_valid(name)) {
		gn_failed_set = 0;
		if (gn_inj >= 0) {
			name_release();
			BTT_KLOG("bluetooth name: the console's own (from the next Bluetooth power-up)");
		}
		return;
	}
	name_bytes(name, want);
	if (gn_inj >= 0 && !memcmp(gn_in, want, NAME_BYTES))
		return;
	if (gn_failed_set && !memcmp(gn_failed, want, NAME_BYTES))
		return;
	gn_failed_set = 0;
	if (!name_find()) {
		memcpy(gn_failed, want, NAME_BYTES);
		gn_failed_set = 1;
		return;
	}
	name_release();   /* the old text out first */
	SceUID r = taiInjectDataForKernel(KERNEL_PID, gn_mod, 0, OFF_NAME, want, NAME_BYTES);
	if (r >= 0) {
		gn_inj = r;
		memcpy(gn_in, want, NAME_BYTES);
		BTT_KLOG("bluetooth name: \"%s\" (from the next Bluetooth power-up)", name);
	} else {
		memcpy(gn_failed, want, NAME_BYTES);
		gn_failed_set = 1;
		BTT_KLOG("bluetooth name: write refused 0x%08x", (unsigned)r);
	}
}

void btt_btname_restore(void)
{
	name_release();
}
