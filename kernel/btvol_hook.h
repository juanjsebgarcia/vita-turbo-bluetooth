/*
 * btvol_hook.h - the hook bodies of the Bluetooth volume mapping (btvol_hook.c). Installing them,
 * and the rest of the module's side, is btvol.c.
 */
#ifndef BTT_BTVOL_HOOK_H
#define BTT_BTVOL_HOOK_H

#include <stdint.h>
#include <taihen.h>

extern volatile tai_hook_ref_t btvol_send_ref;   /* set by taiHEN just after each hook is live      */
extern volatile tai_hook_ref_t btvol_read_ref;
extern volatile uint32_t btvol_in_send;          /* 1 while a call is inside the hook (module stop) */
extern volatile uint32_t btvol_in_read;
extern volatile uint32_t btvol_sel;              /* the table in use (btvol_map.h); 0 = the console's own */

/* The replacements for SceBt's ksceBtAvrcpSendVolume and ksceBtAvrcpReadVolume as SceAVConfig calls
 * them. The first three arguments of the send are the headphones' address (two words) and the volume
 * number; the read takes the address and returns the volume number, or a negative error. */
int btvol_send_hook(uint32_t a, uint32_t b, uint32_t vol, uint32_t d);
int btvol_read_hook(uint32_t a, uint32_t b, uint32_t c, uint32_t d);

#endif
