/*
 * bthfp_hook.h - the hook body that answers a headset's battery reports (bthfp_hook.c) and the state it shares with
 * the module side (bthfp_mod.c): the mode, the newest report under a sequence word, and the counters.
 */
#ifndef BTT_BTHFP_HOOK_H
#define BTT_BTHFP_HOOK_H

#include <stdint.h>
#include <taihen.h>

#include "bthfp.h"

extern volatile tai_hook_ref_t bthfp_ref;   /* set by taiHEN just after the hook is live        */
extern volatile uint32_t bthfp_in;          /* 1 while a call is inside the hook (module stop)  */

/* The hook has two states. With bthfp_mode at 0 it hands every line to the console's own code, unchanged. With it set (and
 * the console's own reply routine found) it answers the hands-free handshake and the battery commands itself (bthfp.c) and
 * hands anything else, and the rest of a line it did not take whole, to the original. */
extern volatile uint32_t bthfp_mode;        /* 0 = off, 1 = answer                                                              */
extern volatile uintptr_t bthfp_reply;      /* the console's reply routine (Thumb address), 0 = not checked or not recognised  */
/* The newest battery report, under a sequence word the writer (the hook) makes odd while it writes: a reader takes seq,
 * the fields and seq again and uses them only if seq was even and the same. The time is the microsecond clock's low word. */
extern volatile uint32_t bthfp_seq, bthfp_level, bthfp_source, bthfp_t_us;
extern volatile uint32_t bthfp_reports;     /* levels reported                                                                  */
extern volatile uint32_t bthfp_answered;    /* commands answered                                                                */
extern volatile uint32_t bthfp_errors;      /* ... of them, those answered ERROR (a malformed command of ours)                   */
extern volatile uint32_t bthfp_steps;       /* BTHFP_STEP_* bits reached since the module loaded                                 */
extern volatile uint32_t bthfp_brsf;        /* the headset's BRSF feature word + 1 (0 = none yet)                                 */
extern volatile uint32_t bthfp_vendor;      /* the feature word of the vendor handshake it announced + 1 (0 = none yet)          */

/* The replacement for the Bluetooth code's routine that takes one received headset line (context, the bytes,
 * their count, and the routine's fourth word, passed on as it came). */
int bthfp_hook(void *ctx, const uint8_t *line, uint32_t len, uint32_t end);

#endif
