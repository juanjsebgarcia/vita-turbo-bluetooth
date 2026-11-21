/* bteq_plan.c - worker side of the Bluetooth sound filter. See bteq_dsp.h. */
#include <string.h>

#include "bteq_dsp.h"
#include "bteq_table.h"

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void bq_init(BqEq *e)
{
	memset(e, 0, sizeof(*e));
}

void bq_restart(BqEq *e)
{
	e->started = 0;
}

/* Gain of the side that is lowered, per step away from the centre: 0, -3, -6, -12 dB and off, in Q15
 * (32768 * 10^(-dB / 20), rounded; checked against that formula in test/test_bteq.c). */
static const int32_t k_bal[5] = { 32768, 23198, 16423, 8231, 0 };

void bq_plan_mix(BqSet *o, int mode, int balance)
{
	int step;

	balance = clampi(balance, 0, 2 * BQ_BAL_CENTRE);
	step = balance < BQ_BAL_CENTRE ? BQ_BAL_CENTRE - balance : balance - BQ_BAL_CENTRE;
	o->mix_mode = (uint32_t)(mode == BQ_MIX_MONO || mode == BQ_MIX_SWAP ? mode : BQ_MIX_STEREO);
	o->gain_l = balance > BQ_BAL_CENTRE ? k_bal[step] : BQ_UNITY;
	o->gain_r = balance < BQ_BAL_CENTRE ? k_bal[step] : BQ_UNITY;
	o->mix_on = o->mix_mode != BQ_MIX_STEREO || balance != BQ_BAL_CENTRE;
}

int bq_plan(const int gain[BQ_BANDS], BqSet *o)
{
	int gi[BQ_BANDS], b, p, n = 0, maxpos = 0, first = -1;

	memset(o, 0, sizeof(*o));
	for (b = 0; b < BQ_BANDS; b++) {
		int g = clampi(gain[b], BQ_GAIN_MIN, BQ_GAIN_MAX);
		gi[b] = g - BQ_GAIN_MIN;
		if (g) {
			o->active |= 1u << b;
			o->c[b] = (BqCoef){ BQ_COEF[b][gi[b]][0], BQ_COEF[b][gi[b]][1], BQ_COEF[b][gi[b]][2],
			                    BQ_COEF[b][gi[b]][3], BQ_COEF[b][gi[b]][4] };
			if (first < 0)
				first = b;
			if (g > maxpos)
				maxpos = g;
			n++;
		}
	}
	if (!n)
		return 0;

	/* Pre-gain: the largest peak of the five bands added up (in 1/8 dB), so a full-scale
	 * input cannot clip at any frequency, but never less than the largest positive band
	 * gain. In 0.5 dB steps, rounded up, with a
	 * quarter dB of room for the table's rounding. */
	int peak = -127 * BQ_BANDS;
	for (p = 0; p < BQ_N_PROBE; p++) {
		int sum = 0;
		for (b = 0; b < BQ_BANDS; b++)
			sum += BQ_RESP[b][gi[b]][p];
		if (sum > peak)
			peak = sum;
	}
	int pre = peak <= 1 ? 0 : (peak + 2 + 3) / 4;
	if (pre < 2 * maxpos)
		pre = 2 * maxpos;
	pre = clampi(pre, 0, BQ_PREGAIN_STEPS);
	o->pre_half_db = (uint32_t)pre;

	if (pre) {
		/* Folded into the numerator of the first running band: no extra multiplication. */
		int64_t k = BQ_PREGAIN[pre];
		int32_t *v[3] = { &o->c[first].b0, &o->c[first].b1, &o->c[first].b2 };
		for (b = 0; b < 3; b++)
			*v[b] = (int32_t)(((int64_t)*v[b] * k + (1 << 29)) >> 30);
	}
	return n;
}

int bq_publish(BqEq *e, const BqSet *s)
{
	uint32_t w = e->idx ^ 1u;

	/* The audio thread announces the set it reads in in_use (1 + index) before reading it
	 * and re-checks idx; so if in_use does not name w now, nothing is reading w, and
	 * nothing starts to after the swap below. */
	__sync_synchronize();
	if (e->in_use == w + 1)
		return -1;
	e->set[w] = *s;
	e->set[w].gen = ++e->next_gen;
	__sync_synchronize();
	e->idx = w;
	__sync_synchronize();
	return 0;
}
