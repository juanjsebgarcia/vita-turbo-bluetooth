/*
 * bteq_run.c - the audio-thread side of the Bluetooth sound filter. See bteq_dsp.h.
 *
 * This runs inside the audio sender's own thread, once per 10.67 ms block: a leaf (no calls,
 * no locks, no waits, no logging), integers only (the kernel does not keep a module's
 * floating-point or vector registers). tools/check_bteq_leaf.sh checks the object file.
 *
 * Each band is run over the whole block in turn, so one band's two memory words and its
 * five coefficients live in registers for the length of the loop. The signal between bands
 * is the 16-bit sample times 256 (a 32-bit integer); every state word is a 64-bit sum of
 * coefficient times sample, so the filter memory is never rounded, only each band's output
 * (to 1/256 of a 16-bit step). The result is rounded and clipped to 16 bits at the end.
 */
#include "bteq_dsp.h"
#include "bteq_table.h"

#define SIG_MAX  0x3FFFFFFFLL    /* far above any real level (full scale is 2^23 here) */

static void run_band(int32_t *x, const BqCoef *c, BqState *s)
{
	int64_t z1 = s->z1, z2 = s->z2;
	const int32_t b0 = c->b0, b1 = c->b1, b2 = c->b2, a1 = c->a1, a2 = c->a2;
	int n;

	for (n = 0; n < BQ_FRAMES; n++) {
		const int32_t in = x[n];
		int64_t acc = (int64_t)b0 * in + z1;
		int64_t t = (acc + (1LL << (BQ_QBITS - 1))) >> BQ_QBITS;
		if (t > SIG_MAX)
			t = SIG_MAX;
		else if (t < -SIG_MAX)
			t = -SIG_MAX;
		const int32_t y = (int32_t)t;
		z1 = (int64_t)b1 * in - (int64_t)a1 * y + z2;
		z2 = (int64_t)b2 * in - (int64_t)a2 * y;
		x[n] = y;
	}
	s->z1 = z1;
	s->z2 = z2;
}

/* The channel stage, on 16-bit frames: mono (the two sides averaged, rounded down), swap, then the
 * balance gains in Q15. Nothing here can leave the 16-bit range, so there is no clipping. `src` and
 * `dst` may be the same block. */
static void mix_block(const BqSet *s, const int16_t *src, int16_t *dst)
{
	const uint32_t mode = s->mix_mode;
	const int32_t gl = s->gain_l, gr = s->gain_r;
	uint32_t n;

	for (n = 0; n < BQ_FRAMES; n++) {
		int32_t l = src[2 * n], r = src[2 * n + 1];
		if (mode == BQ_MIX_MONO) {
			l = r = (l + r) >> 1;
		} else if (mode == BQ_MIX_SWAP) {
			const int32_t t = l;
			l = r;
			r = t;
		}
		dst[2 * n] = (int16_t)((l * gl + (1 << 14)) >> 15);
		dst[2 * n + 1] = (int16_t)((r * gr + (1 << 14)) >> 15);
	}
}

const int16_t *bq_process(BqEq *e, const int16_t *in, uint32_t now_us)
{
	uint32_t i, b, ch, n;

	/* Say which set is being read, then make sure it is still the current one. */
	do {
		i = e->idx;
		e->in_use = i + 1;
		__sync_synchronize();
	} while (i != e->idx);
	const BqSet *s = &e->set[i];

	if (!e->started || (uint32_t)(now_us - e->last_us) > BQ_GAP_US) {
		for (ch = 0; ch < 2; ch++)
			for (b = 0; b < BQ_BANDS; b++)
				e->st[ch][b].z1 = e->st[ch][b].z2 = 0;
		e->started = 1;
		e->n_resets++;
	}
	e->last_us = now_us;

	if (s->gen != e->seen_gen) {
		/* A new set: bands that stopped running start from nothing when they run again;
		 * bands that keep running keep their memory, so a change of gain has no click. */
		for (ch = 0; ch < 2; ch++)
			for (b = 0; b < BQ_BANDS; b++)
				if (!(s->active & (1u << b)))
					e->st[ch][b].z1 = e->st[ch][b].z2 = 0;
		e->seen_gen = s->gen;
	}
	if (!s->active && !s->mix_on) {
		e->n_bypassed++;
		__sync_synchronize();
		e->in_use = 0;
		return 0;
	}

	if (s->active) {
		for (ch = 0; ch < 2; ch++) {
			for (n = 0; n < BQ_FRAMES; n++)
				e->tmp[n] = (int32_t)in[2 * n + ch] * (1 << BQ_SIG_FRAC);
			for (b = 0; b < BQ_BANDS; b++)
				if (s->active & (1u << b))
					run_band(e->tmp, &s->c[b], &e->st[ch][b]);
			for (n = 0; n < BQ_FRAMES; n++) {
				int32_t v = (e->tmp[n] + (1 << (BQ_SIG_FRAC - 1))) >> BQ_SIG_FRAC;
				e->out[2 * n + ch] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
			}
		}
	}
	if (s->mix_on)
		mix_block(s, s->active ? e->out : in, e->out);
	e->n_filtered++;
	__sync_synchronize();
	e->in_use = 0;
	return e->out;
}
