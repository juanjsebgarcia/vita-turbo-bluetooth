/*
 * bteq_dsp.h - the sound filter for Bluetooth headphones: five biquads per channel (a low
 * shelf, three peaks and a high shelf), run on the 512-frame blocks that go to the Bluetooth encoder.
 *
 * After the filter, the block can also be mixed: mono, left and right swapped, and a balance (one
 * side lowered), all in integers on the 16-bit result. Stereo with the balance centred touches nothing.
 *
 * Two halves, in two files so that the half that runs inside the audio thread stays a leaf:
 *   bteq_plan.c  worker side: gains -> coefficient set (table lookups, integers only), and
 *                the double-buffered hand-over to the other side;
 *   bteq_run.c   audio-thread side: bq_process(), nothing but arithmetic on its arguments.
 * Plain C with no kernel headers, so the host test (test/test_bteq.c) compiles both as they are.
 */
#ifndef BTT_BTEQ_DSP_H
#define BTT_BTEQ_DSP_H

#include <stdint.h>

#define BQ_BANDS        5
#define BQ_FRAMES       512                   /* frames per block                      */
#define BQ_BLOCK_BYTES  (BQ_FRAMES * 4)       /* s16 stereo: the 0x800 bytes the sender hands over */
#define BQ_SIG_FRAC     8                     /* fraction bits of the signal between sections */
#define BQ_GAP_US       50000u                /* a longer pause between blocks = a new stream */

/* The channel stage's mode (BttChannels' values) and the balance's unity gain. */
#define BQ_MIX_STEREO   0
#define BQ_MIX_MONO     1
#define BQ_MIX_SWAP     2
#define BQ_UNITY        32768                 /* Q15 gain 1.0: a side that is not lowered */
#define BQ_BAL_CENTRE   4                     /* balance steps 0..8, 4 = centre           */

/* One band: y = b0 x + z1; z1 = b1 x - a1 y + z2; z2 = b2 x - a2 y (a0 divided out), Q3.29. */
typedef struct { int32_t b0, b1, b2, a1, a2; } BqCoef;

typedef struct {
	uint32_t gen;                 /* new for every published set                   */
	uint32_t active;              /* bit b: band b runs (bands at 0 dB are skipped) */
	uint32_t pre_half_db;         /* the pre-gain folded into the first active band, in 0.5 dB */
	BqCoef   c[BQ_BANDS];
	uint32_t mix_on;              /* the channel stage runs (not stereo, or the balance is off centre) */
	uint32_t mix_mode;            /* BQ_MIX_*                                       */
	int32_t  gain_l, gain_r;      /* Q15 gains of the two output channels           */
} BqSet;

typedef struct { int64_t z1, z2; } BqState;

typedef struct BqEq {
	/* shared: the worker writes the set that is not in use, then swaps idx */
	BqSet             set[2];
	volatile uint32_t idx;        /* the set the audio thread reads                */
	volatile uint32_t in_use;     /* 0, or 1 + the set the audio thread is reading */
	uint32_t          next_gen;   /* worker only                                   */
	/* audio thread only */
	uint32_t          seen_gen, last_us, started;
	BqState           st[2][BQ_BANDS];
	int32_t           tmp[BQ_FRAMES];
	int16_t           out[BQ_FRAMES * 2];
	/* counters: the audio thread writes, the worker reads */
	volatile uint32_t n_filtered, n_bypassed, n_resets;
} BqEq;

/* Everything off and cleared. Only while the audio thread is not inside bq_process. */
void bq_init(BqEq *e);

/* Forget the filter memory (a new stream) at the next block. Only while the audio thread
 * is not inside bq_process (the hook is not installed). */
void bq_restart(BqEq *e);

/* Worker side. Gains in dB per band (clamped to -12..+12). Returns the number of active
 * bands: 0 means flat, and the set then makes bq_process pass the block through. */
int bq_plan(const int gain[BQ_BANDS], BqSet *out);

/* Worker side. Sets the channel stage of `o` (after bq_plan filled it): `mode` is BQ_MIX_*, `balance`
 * 0..8 with BQ_BAL_CENTRE as the centre (below it the right side is lowered, above it the left). The
 * stage is off, and the block untouched, for BQ_MIX_STEREO at the centre. */
void bq_plan_mix(BqSet *o, int mode, int balance);

/* Worker side. Makes `s` the set in use. Returns 0, or -1 when the audio thread may still
 * be reading the set that would be overwritten (try again later; nothing changed). */
int bq_publish(BqEq *e, const BqSet *s);

/* Audio-thread side. Filters and mixes one block of BQ_FRAMES interleaved s16 stereo frames from `in`
 * into e->out and returns it; returns NULL when there is nothing to do (no band running and the
 * channel stage off: the caller then passes `in` on as it is). `now_us` is any microsecond clock, read by the caller. */
const int16_t *bq_process(BqEq *e, const int16_t *in, uint32_t now_us);

#endif
