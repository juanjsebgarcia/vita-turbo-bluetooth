/*
 * btqual_hook.h - the hook body of the Bluetooth connection quality setting (btqual_hook.c). Installing
 * it, and the rest of the module's side, is btqual.c.
 */
#ifndef BTT_BTQUAL_HOOK_H
#define BTT_BTQUAL_HOOK_H

#include <stdint.h>
#include <taihen.h>

extern volatile tai_hook_ref_t btqual_ref;   /* set by taiHEN just after the hook is live         */
extern volatile uint32_t btqual_in;          /* 1 while a call is inside the hook (module stop)   */
extern volatile uint32_t btqual_cap;         /* the highest bitpool allowed; 0 = no cap (the console's own) */

/* The replacement for the Bluetooth code's routine that reads an A2DP codec configuration into a
 * connection: (connection, flags, the configuration bytes, their length). It runs the original first.
 * When the call applies a configuration (flag bit 3) and a cap is set, the connection's highest bitpool,
 * which the encoder reads when a stream starts, is then lowered to the cap: it takes effect from the next
 * connection of the headphones. */
int btqual_hook(void *conn, uint32_t flags, const void *caps, uint32_t len);

/* The two settings that send the channels separately (bthq.c): 0 = none; BTHQ_HIGH = dual channel capped at bitpool 38;
 * BTHQ_HIGHEST = dual channel at the headphones' highest. With either on, a configuration that applies (flag bit 3)
 * first has the joint and stereo channel modes taken out of the capabilities the headphones offered (when they offer
 * dual channel), so the console's own order picks dual channel, and afterwards the connection's highest bitpool is
 * capped for it. */
#define BTHQ_HIGH     1u
#define BTHQ_HIGHEST  2u
extern volatile uint32_t btqual_hq;

#endif
