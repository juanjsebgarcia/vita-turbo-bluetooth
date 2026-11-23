/*
 * bteq_hook.c - the hook body of the Bluetooth sound filter, and nothing else: this object
 * file is checked whole by tools/check_bteq_leaf.sh. It runs in the audio sender's thread,
 * once per 10.67 ms block, so it is a leaf: no logging, no locks, no waits, no allocation,
 * no floating point, no vector instructions. The one call it makes besides the original
 * routine is the cheap microsecond clock (a timer read).
 *
 * With AAC streaming on (btaac_hook.h) the block is also handed, after the filter, to the AAC engine's ring (a leaf call through the AAC module's table).
 *
 * The block's memory is read after only a null and alignment check: it is the audio sender's own
 * buffer, which the original routine reads at the same point, so no further page check is made.
 *
 * Only a block of exactly 0x800 bytes (512 frames, s16 stereo, 48 kHz: what the sender hands
 * over for A2DP) is touched. Anything else - the 0xF0-byte blocks of a phone call, a block
 * of another size, an unaligned or null pointer - goes to the original routine as it came.
 *
 * The block is read from the sender's buffer and the result is written to the module's own
 * buffer (bteq_eq.out), which the original routine then reads: the sender's buffer is the
 * target of the audio hardware's copy and is never written by the module, whatever its caching.
 */
#include <psp2kern/kernel/threadmgr.h>
#include <taihen.h>

#include "bteq_hook.h"
#include "btaac_hook.h"

#define REF_SPINS   200000u   /* loads, no system call: about a millisecond at most */
#define BAD_LENGTH  0x802F0D07   /* what the original returns for a block it does not accept */

volatile tai_hook_ref_t bteq_ref;
volatile uint32_t bteq_inside;
volatile uint32_t bteq_calls;
BqEq bteq_eq;

int bteq_hook(uint32_t addr_lo, uint32_t addr_hi, const void *buf, uint32_t len)
{
	tai_hook_ref_t ref;
	uint32_t spins = REF_SPINS;

	/* The hook goes live a moment before taiHEN stores the reference. */
	while ((ref = bteq_ref) == 0 && --spins)
		;
	if (!ref)
		return (int)BAD_LENGTH;   /* never set: no call through it, no work of ours */

	bteq_calls++;
	bteq_inside = 1;
	if (len == BQ_BLOCK_BYTES && buf && !((uintptr_t)buf & 3)) {
		const int16_t *out = bq_process(&bteq_eq, (const int16_t *)buf, ksceKernelGetSystemTimeLow());
		if (out)
			buf = out;
		btaac_pcm_block((const int16_t *)buf);   /* AAC streaming: the block, as it goes on, to the engine (only while the stream is AAC and running) */
	}
	int ret = TAI_CONTINUE(int, ref, addr_lo, addr_hi, buf, len);
	bteq_inside = 0;
	return ret;
}
