/*
 * btaac_hook_fn.h - the two hook functions of AAC streaming (btaac_hook.c) and their taiHEN references, for the code that installs them
 * (btaac_stream.c). The negotiation state they share is in btaac_hook.h.
 */
#ifndef BTAAC_HOOK_FN_H
#define BTAAC_HOOK_FN_H

#include <stdint.h>
#include <taihen.h>

extern volatile tai_hook_ref_t btaac_disp_ref, btaac_send_ref;   /* set by taiHEN just after each hook is live      */
extern volatile uint32_t btaac_disp_in, btaac_send_in;           /* calls inside each hook now (module stop waits)  */

/* H1: the replacement for the signalling receive routine: (connection, channel, message, length). */
int btaac_disp_hook(void *conn, void *chan, const uint8_t *msg, int len);

/* H3: the replacement for the media packet send routine: (connection, packet, length, flag). */
int btaac_send_hook(void *conn, const void *buf, uint32_t len, uint32_t flag);

#endif
