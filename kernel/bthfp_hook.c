/*
 * bthfp_hook.c - the hook body of the headset battery answers, and nothing else: this object file is checked whole by
 * tools/check_bteq_leaf.sh. It runs in the Bluetooth code's own thread, once per line a headset sends over its voice
 * (headset) channel: no logging, no locks, no waits, no allocation, no floating point, and no call but the microsecond clock,
 * the parser (bthfp.c) and the original routine's.
 *
 * With the mode off (or the console's reply routine not recognised) it calls the original with every argument as it came,
 * so the console answers exactly as before. With it on it hands the line to bthfp.c, sends that file's answers with the
 * console's own reply routine (the one the original uses for "OK" and "ERROR", called through its address: the one call
 * here that tools/check_bteq_leaf.sh cannot see, since it has no relocation, and the same thread and call the original
 * itself makes) and calls the original only for what was not answered.
 */
#include <psp2kern/kernel/threadmgr.h>
#include <taihen.h>

#include "bthfp_hook.h"

#define REF_SPINS      200000u   /* loads, no system call: about a millisecond at most */
#define HFP_LINE_CMDS  8u        /* commands answered out of one received line, at most */

volatile tai_hook_ref_t bthfp_ref;
volatile uint32_t bthfp_in;
volatile uint32_t bthfp_mode;
volatile uintptr_t bthfp_reply;
volatile uint32_t bthfp_seq, bthfp_level, bthfp_source, bthfp_t_us;
volatile uint32_t bthfp_reports, bthfp_answered, bthfp_errors, bthfp_steps, bthfp_brsf, bthfp_vendor;

/* The console's reply routine: the connection, a format, then what the format takes. */
typedef void (*reply_fn)(void *ctx, const char *fmt, ...);

static void hfp_publish(uint32_t level, uint32_t source)
{
	uint32_t s = bthfp_seq;

	__atomic_store_n(&bthfp_seq, s + 1, __ATOMIC_RELAXED);          /* odd: being written */
	__atomic_thread_fence(__ATOMIC_RELEASE);                        /* ... and before the fields */
	bthfp_level = level;
	bthfp_source = source;
	bthfp_t_us = ksceKernelGetSystemTimeLow();
	__atomic_store_n(&bthfp_seq, s + 2, __ATOMIC_RELEASE);          /* even: whole */
	__atomic_fetch_add(&bthfp_reports, 1, __ATOMIC_RELAXED);
}

/* Answers the commands at the start of the line; returns the bytes used up. */
static uint32_t hfp_take(void *ctx, const uint8_t *line, uint32_t len)
{
	reply_fn reply = (reply_fn)bthfp_reply;
	uint32_t off = 0, n;

	for (n = 0; n < HFP_LINE_CMDS && off < len; n++) {
		BtHfpAnswer a;
		uint32_t used = bthfp_answer(line + off, len - off, &a);

		if (!used)
			break;
		reply(ctx, "%s", a.reply);                /* "%s": the text is never read as a format */
		off += used;
		__atomic_fetch_add(&bthfp_answered, 1, __ATOMIC_RELAXED);
		if (a.is_error)
			__atomic_fetch_add(&bthfp_errors, 1, __ATOMIC_RELAXED);
		if (a.step)
			__atomic_fetch_or(&bthfp_steps, a.step, __ATOMIC_RELAXED);
		if (a.step & BTHFP_STEP_BRSF)
			bthfp_brsf = a.hf_brsf + 1;
		if (a.step & BTHFP_STEP_VENDOR)
			bthfp_vendor = a.vendor + 1;
		if (a.battery >= 0)
			hfp_publish((uint32_t)a.battery, a.source);
	}
	return off;
}

int bthfp_hook(void *ctx, const uint8_t *line, uint32_t len, uint32_t end)
{
	tai_hook_ref_t ref;
	uint32_t spins = REF_SPINS;
	int ret;

	/* The hook goes live a moment before taiHEN stores the reference. */
	while ((ref = bthfp_ref) == 0 && --spins)
		;
	if (!ref)
		return 0;   /* never set: the original is skipped for this one call, in the first millisecond after install only */

	bthfp_in = 1;
	if (bthfp_mode && bthfp_reply && line) {
		uint32_t off = hfp_take(ctx, line, len);

		if (off && off >= len) {           /* all of it answered here */
			bthfp_in = 0;
			return 0;
		}
		line += off;                       /* what is left goes to the console's own code, as ever */
		len -= off;
	}
	ret = TAI_CONTINUE(int, ref, ctx, line, len, end);
	bthfp_in = 0;
	return ret;
}
