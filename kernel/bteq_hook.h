/*
 * bteq_hook.h - the hook body for the Bluetooth sound filter (bteq_hook.c). Installing it,
 * and the rest of the module's side, is bteq.c.
 */
#ifndef BTT_BTEQ_HOOK_H
#define BTT_BTEQ_HOOK_H

#include <stdint.h>
#include <taihen.h>

#include "bteq_dsp.h"

extern volatile tai_hook_ref_t bteq_ref;   /* set by taiHEN just after the hook is live      */
extern volatile uint32_t bteq_inside;      /* 1 while a call is inside the hook (module stop) */
extern volatile uint32_t bteq_calls;       /* every call that came through, of any length     */
extern BqEq bteq_eq;                       /* the filter: sets, memory and counters          */

/* The replacement for SceBt's ksceBtSendAudio as SceAVConfig calls it. */
int bteq_hook(uint32_t addr_lo, uint32_t addr_hi, const void *buf, uint32_t len);

#endif
