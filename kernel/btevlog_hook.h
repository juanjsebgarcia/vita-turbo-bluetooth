/*
 * btevlog_hook.h - the one routine of the link log that runs inside the Bluetooth code (btevlog_hook.c), and the ring it writes. The worker side is btevlog.c.
 */
#ifndef BTT_BTEVLOG_HOOK_H
#define BTT_BTEVLOG_HOOK_H

#include <stdint.h>

#include "btevlog_core.h"

extern BtelRing btevlog_ring;           /* what the hook noted, for the worker                                     */
extern volatile uint32_t btevlog_on;    /* 1: note the events; 0: do nothing (the hook stays in, passing through)  */

/* Called by the event handler's hook (btflush_hook.c) for every event the controller reports, before the original runs: (the event, its length). A leaf: it reads the
 * event's bytes (never beyond `len`), and for a connection, disconnection, authentication, encryption or role event stores one small record in the ring. */
void btevlog_note_hci(const uint8_t *ev, int len);

#endif
