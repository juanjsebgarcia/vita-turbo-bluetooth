#ifndef BTT_BTFIX_H
#define BTT_BTFIX_H

#include <stdint.h>

/* Put the Bluetooth code's changes in place or take them out, to match the arguments: the sender's
 * timing fix (0/1), the send queue's limit for the latency level (BttLatency: 0 stock, 1 Low,
 * 2 Lowest) and the wireless controllers' response (1 = no low-power mode). Cheap when nothing
 * changes. */
void btt_bt_sync(int ts_fix, int latency, int ctrl_fast);

/* Everything back to stock (module stop). */
void btt_bt_restore(void);

/* The Bluetooth code's data segment (0 until its code is recognised): the headset battery code reads the link
 * table in it. */
uintptr_t btt_bt_seg1(void);

#endif
