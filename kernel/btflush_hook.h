/*
 * btflush_hook.h - the hook bodies of the flush timeout and the completed-packets guard
 * (btflush_hook.c). Installing them, and the rest of the module's side, is btflush.c.
 */
#ifndef BTT_BTFLUSH_HOOK_H
#define BTT_BTFLUSH_HOOK_H

#include <stdint.h>
#include <taihen.h>

extern volatile tai_hook_ref_t btflush_start_ref;   /* set by taiHEN just after each hook is live      */
extern volatile tai_hook_ref_t btflush_stop_ref;
extern volatile tai_hook_ref_t btflush_evt_ref;
extern volatile uint32_t btflush_in_start;          /* 1 while a call is inside that hook (module stop) */
extern volatile uint32_t btflush_in_stop;
extern volatile uint32_t btflush_in_evt;
extern volatile uint32_t btflush_slots;             /* the timeout to set at the next stream start, in 0.625 ms slots; 0 = none */
extern volatile uint32_t btflush_armed;             /* 1 + the handle a timeout was set on, 0 = none    */
extern volatile uint32_t btflush_guard;             /* 1: the guard splits events that report several links */
extern void *volatile btflush_sender;               /* the Bluetooth code's command sender              */

/* The replacements for the Bluetooth code's stream start and stream stop routines: (connection, flag,
 * mask). Each runs the original first. */
void btflush_start_hook(int *conn, int flag, uint32_t mask);
void btflush_stop_hook(int *conn, int flag, uint32_t mask);

/* The replacement for its event handler: (context, event, length). An event is passed on untouched
 * unless it is a Number_Of_Completed_Packets event naming more than one link while the guard is on. */
void btflush_evt_hook(uint32_t *ctx, const uint8_t *ev, int len);

#endif
