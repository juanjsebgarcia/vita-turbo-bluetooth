#ifndef BTT_BTFIX_H
#define BTT_BTFIX_H

#include <stdint.h>

/* Put the Bluetooth code's changes in place or take them out, to match the arguments: the sender's
 * timing fix (0/1), the send queue's limit for the latency level (BttLatency: 0 stock, 1 Low,
 * 2 Lowest) and the wireless controllers' response (1 = no low-power mode). Cheap when nothing
 * changes. */
void btt_bt_sync(int ts_fix, int latency, int ctrl_fast);

/* Whether the stream is AAC (kernel/btaac_stream.h, btt_aac_stream_is_aac), called before btt_bt_sync: the send queue's limit for a latency level is then the AAC
 * one (btaac_lat.h) instead of the SBC one. The change is made by the next btt_bt_sync. */
void btt_bt_set_aac(int aac);

/* Everything back to stock (module stop). */
void btt_bt_restore(void);

/* The Bluetooth code's data segment (0 until its code is recognised): the headset battery code reads the link
 * table in it. */
uintptr_t btt_bt_seg1(void);

#endif
