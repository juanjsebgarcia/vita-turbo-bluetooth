#ifndef BTT_BTLINK_H
#define BTT_BTLINK_H

#include <stdint.h>

#include "../include/bluetoothturbo.h"

/* The state of the audio link, kept for bttGetLinkInfo. The worker is the one writer (btt_link_pass, once a pass after the start-up period);
 * any caller reads without a lock: a sequence word before and after the fields tells a reader that its copy is whole (the same pattern as
 * the headset battery, btbatt.c). A reader that never gets a whole copy says "nothing known" rather than a mixed one. */

/* Reads what the Bluetooth code says about the A2DP link (its data is only read once its code is recognised, btfix.c) and what the AAC stream
 * hooks know, and publishes it. `on` is the master switch, `latency` the Bluetooth latency setting. */
void btt_link_pass(int on, int latency);

/* What else the worker saw of the audio link on its last btt_link_pass (the worker's own reading: no lock, not for another thread): the address of the
 * headphones, whether the hands-free voice link has the connection, and the AAC stream's rate in force and the headphones' highest (b/s), and whether
 * the engine's session is running. All 0 when no link. */
typedef struct BtLinkExtra {
	int mac_ok;
	uint32_t mac_lo, mac_hi;   /* the address as the Bluetooth code's calls take it */
	int voice;
	uint32_t bps, max_bps;
	int aac_running;
} BtLinkExtra;
void btt_link_extra(BtLinkExtra *out);

/* The snapshot as the syscall gives it (size and version not touched); all zero while nothing is known. */
void btt_link_read(BttLinkInfo *out);

#endif
