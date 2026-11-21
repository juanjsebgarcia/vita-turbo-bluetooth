/* lowbatt.c - see lowbatt.h. */
#include "lowbatt.h"

static const int k_thr[LOWBATT_N] = { LOWBATT_T0, LOWBATT_T1, LOWBATT_T2 };

int lowbatt_step(LowBatt *s, int level, int connected, int enabled)
{
	unsigned fresh = 0;

	if (!connected) {
		s->fired = 0;
		return -1;
	}
	if (level < 0 || level > 100)
		return -1;                       /* connected but nothing reported (yet): nothing to learn */
	for (int i = 0; i < LOWBATT_N; i++) {
		if (level > k_thr[i])
			s->fired &= ~(1u << i);          /* risen above it: armed again */
		else if (!(s->fired & (1u << i)))
			fresh |= 1u << i;
	}
	s->fired |= fresh;
	return fresh && enabled ? level : -1;
}
