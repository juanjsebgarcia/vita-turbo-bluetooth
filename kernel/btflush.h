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

/* Module stop: the three hooks out. */
void btt_btflush_restore(void);

#endif
