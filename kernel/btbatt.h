#ifndef BTT_BTBATT_H
#define BTT_BTBATT_H

#include <stdint.h>

#include "../include/bluetoothturbo.h"

/* The last battery level a headset reported, kept for bttGetHeadsetBattery. Written by one thread (the worker),
 * read by any caller without a lock: a sequence word before and after tells a reader that the copy is whole. */

/* The snapshot for the time `now_us` (microseconds on the console's wide clock) as the syscall gives it: the level
 * and source and the age of the report, `connected` as last noted. Unknown (level BTT_BATT_UNKNOWN, source none,
 * age 0) when nothing was reported, or when the headset's link has been down longer than BTT_BATT_KEEP_S.
 * The `size` and `version` fields are not touched. */
void btt_batt_read(BttHeadsetBattery *out, uint64_t now_us);

/* A level (0..100) from `source` (BTT_BATT_SRC_*) that arrived at `arrived_us`. */
void btt_batt_report(int level, unsigned source, uint64_t arrived_us);

/* The headset's link is up (1) or not (0) at `now_us`. A level stays for BTT_BATT_KEEP_S after the link goes down. */
void btt_batt_link(int connected, uint64_t now_us);

/* Forget the level and the link (the headset battery code is switched off). */
void btt_batt_reset(void);

#endif
