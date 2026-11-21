/*
 * btreconn_hook.h - the hook body that notes the headphones' address, and the standby handler that notes a
 * wake-up (btreconn_hook.c). Installing them, and the reconnecting itself, is btreconn.c.
 */
#ifndef BTT_BTRECONN_HOOK_H
#define BTT_BTRECONN_HOOK_H

#include <stdint.h>
#include <taihen.h>

extern volatile tai_hook_ref_t btreconn_ref;   /* set by taiHEN just after the hook is live        */
extern volatile uint32_t btreconn_in;          /* 1 while a call is inside the hook (module stop)  */
extern volatile uint32_t btreconn_rec;         /* 1 = note the address of the headphones           */
extern volatile uint32_t btreconn_armed;       /* 1 = count the wake-ups                           */
extern volatile uint32_t btreconn_mac0, btreconn_mac1;   /* the address of the last A2DP device    */
extern volatile uint32_t btreconn_recorded;    /* how many addresses were noted (0 = none yet)     */
extern volatile uint32_t btreconn_wakes;       /* wake-up events seen while armed                  */

/* The replacement for ksceBtStartAudio as SceAVConfig calls it: (address word 0, address word 1, flags,
 * the fourth argument). It runs the original and, when the call started A2DP (flag bit 3) and succeeded,
 * notes the address. Nothing else: the reconnecting is done by the worker. */
int btreconn_hook(uint32_t mac0, uint32_t mac1, uint32_t flags, uint32_t d);

/* The system event handler (called around standby and resume): counts the resume events while armed.
 * It does nothing else and always returns 0 (it never holds up a suspend). */
int btreconn_sysevent(int resume, int eventid, void *args, void *opt);

#endif
