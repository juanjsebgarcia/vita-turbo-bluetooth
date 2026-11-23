/*
 * btfall.h - the codec fallback notification, the kernel module's side (rules and texts: btfall_core.h). The worker looks once a pass; the Home screen module
 * asks for what is waiting through bttGetNotice.
 */
#ifndef BTT_BTFALL_H
#define BTT_BTFALL_H

/* The worker's pass, after btt_link_pass: `on` is the master switch, `codec` the Bluetooth Codec key. Queues a notice when the chosen codec cannot be used for the
 * connection and SBC is used, once for each headset and codec. */
void btt_fall_pass(int on, int codec);

#endif
