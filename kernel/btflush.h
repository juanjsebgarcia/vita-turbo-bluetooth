#ifndef BTT_BTFLUSH_H
#define BTT_BTFLUSH_H

/* The rest of Bluetooth latency Lowest, in every build. While an A2DP stream plays, the radio link to the
 * headphones discards a media packet that has waited longer than a timeout instead of sending it late;
 * and an event that reports completed packets for more than one link (headphones plus a controller) is
 * handled link by link, so the count of packets in flight cannot leak and mute the stream.
 * Call from the worker on every pass once the start-up period is over, with 1 when the Bluetooth
 * latency is Lowest and Bluetooth Turbo is on. Installs three hooks (stream start, stream stop and
 * the event handler in the Bluetooth code, each checked byte for byte first) the first time it is
 * wanted; they stay in, doing nothing, until module stop. The timeout is set when a stream starts and
 * cleared when it stops. */
void btt_btflush_sync(int lowest);

/* The event handler's hook alone, for the link log (btevlog.c): 1 if it is in after this call. With `want` set it goes in the first time (the site checked byte for byte
 * first; when the code cannot be read yet it is asked again on the next call, when it is not the code this module knows it is not tried again); it stays in, passing through,
 * until module stop. The same hook serves the Lowest guard: whichever is wanted first puts it in, the other finds it there. */
int btt_btflush_evt_sync(int want);

/* Module stop: the hooks out. */
void btt_btflush_restore(void);

#endif
