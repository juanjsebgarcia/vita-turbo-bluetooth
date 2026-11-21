/*
 * lowbatt.h - when the headset's battery has run low enough to say so: 20 %, 10 % and 5 %, each once per drain.
 * Pure code: no system calls (tested on a computer).
 */
#ifndef BTT_LOWBATT_H
#define BTT_LOWBATT_H

#define LOWBATT_N 3
#define LOWBATT_T0 20
#define LOWBATT_T1 10
#define LOWBATT_T2 5

typedef struct {
	unsigned fired;       /* bit i set: threshold i has been announced in this drain (or passed while the notices were off) */
} LowBatt;

/* One look at the headset: `level` 0..100 (negative: no level reported), `connected` 1 while its link is up, `enabled` the
 * Settings switch. Returns the level to announce (0..100) when it has just dropped to or below a threshold not yet announced, else -1.
 * One notice at most per call, whatever number of thresholds it has passed. A threshold is armed again when the level rises above it, and
 * every threshold is armed again when the link is down (a new connection). While `enabled` is 0 the thresholds are still tracked,
 * so switching the notices on at a low level does not announce what was already there. */
int lowbatt_step(LowBatt *s, int level, int connected, int enabled);

#endif
