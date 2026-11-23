/*
 * btaac_lat.h - Bluetooth Latency on an AAC stream. Pure, shared by the send queue's limit (btfix.c) and the engine's output bound (btaac_hook.c).
 *
 * The code counts packets in flight and backs off over a limit: 23 / 11 / 7 for Standard / Low / Lowest (btfix.c). One AAC packet is one access unit,
 * 1024 samples, 21.3 ms of sound where an SBC packet is 10.7 ms, so the same counts would mean twice the time. On an AAC stream the limits are 11 / 5 / 3
 * units (about 235 / 107 / 64 ms, against SBC's 245 / 117 / 75), and the hook that sends the units also keeps no more than that many waiting in the
 * engine's output, which the code's own count cannot see.
 */
#ifndef BTAAC_LAT_H
#define BTAAC_LAT_H

#include <stdint.h>

/* `level` is a BttLatency (0 Standard, 1 Low, 2 Lowest); anything else is Standard. */
static inline uint32_t btaac_lat_limit(unsigned level)
{
	return level == 2u ? 3u : level == 1u ? 5u : 11u;
}

#endif /* BTAAC_LAT_H */
