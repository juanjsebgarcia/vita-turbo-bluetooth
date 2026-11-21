/*
 * btvol_hook.c - the hook bodies of the Bluetooth volume mapping, and nothing else: this object
 * file is checked whole by tools/check_bteq_leaf.sh. They run in the sound policy thread of
 * SceAVConfig, a few times per volume change, so they are leaves like the sound hook: no
 * logging, no locks, no waits, no allocation, no floating point; the only calls are to the two
 * lookup functions of btvol_map.c.
 *
 * What the volume numbers are, and how the console turns its 31 steps into them and back, is in
 * btvol_table.h and btvol_map.h. With the table 0 selected (Standard, 100 %) both hooks pass every
 * value through unchanged.
 */
#include "btvol_hook.h"
#include "btvol_map.h"

#define REF_SPINS   200000u      /* loads, no system call: about a millisecond at most */
#define BAD_STATE   0x802F1901   /* the error SceBt itself returns when it cannot do the call */

volatile tai_hook_ref_t btvol_send_ref, btvol_read_ref;
volatile uint32_t btvol_in_send, btvol_in_read;
volatile uint32_t btvol_sel;

int btvol_send_hook(uint32_t a, uint32_t b, uint32_t vol, uint32_t d)
{
	tai_hook_ref_t ref;
	uint32_t spins = REF_SPINS;

	/* The hook goes live a moment before taiHEN stores the reference. */
	while ((ref = btvol_send_ref) == 0 && --spins)
		;
	if (!ref)
		return (int)BAD_STATE;   /* never set: no call through it, no work of ours */

	btvol_in_send = 1;
	uint32_t out = bv_send_map(btvol_sel, vol);
	int ret = TAI_CONTINUE(int, ref, a, b, out, d);
	btvol_in_send = 0;
	return ret;
}

int btvol_read_hook(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	tai_hook_ref_t ref;
	uint32_t spins = REF_SPINS;

	while ((ref = btvol_read_ref) == 0 && --spins)
		;
	if (!ref)
		return (int)BAD_STATE;

	btvol_in_read = 1;
	int ret = TAI_CONTINUE(int, ref, a, b, c, d);
	/* A volume number is 0..127; anything else (a negative error) goes back as it came. */
	if (ret >= 0 && ret <= 127) {
		ret = (int)bv_read_map(btvol_sel, (uint32_t)ret);
	}
	btvol_in_read = 0;
	return ret;
}
