/*
 * btaac_pace.h - when the AAC stream's media hook sends a unit, and how the sender's cadence is watched. Header only, plain integer C and no call: the media hook (kernel/btaac_hook.c,
 * a leaf) runs it, and the host test (test/test_btaac_pace.c) compiles exactly what the kernel module runs.
 *
 * The cadence: the audio sender hands over a block of 512 frames every 10.67 ms, and one access unit is 1024 frames, so a unit is due every two blocks. The media hook is called once
 * per block. Without pacing it sent whatever unit was ready on every call, so after the engine had been held off the CPU the backlog went out at twice the cadence (a burst) and, with
 * nothing ready at a due call, left a gap the headset plays as a stutter. Now:
 *   - pre-roll: the first unit of a session goes only when `preroll` units are ready (and again after the ring has run dry), so the headset gets a steady stream with some slack;
 *   - spacing: a unit goes at most once in two calls (never two on consecutive calls: no burst), and the next unit goes at least two calls after the one before (a backlog is never drained faster; the latency trim bounds it);
 *   - a ring found empty at a due call is an underrun: it is counted, and the stream waits for the pre-roll again before it sends (the ring refills; nothing is sent in a burst).
 * `preroll` 0 is the behaviour before this existed: whatever is ready goes, on every call; the counters still run.
 */
#ifndef BTAAC_PACE_H
#define BTAAC_PACE_H

#include <stdint.h>

#define BTAAC_PREROLL_MAX      6u        /* units (the settable range is 0..6)                                       */
#define BTAAC_PREROLL_DEFAULT  0u        /* none: no delay is added to the stream (measured: no late encode and no underrun at the engine's computed priority) */
#define BTAAC_SPACING_CALLS    2u        /* calls (blocks) between two units                                         */

enum { BTAAC_PACE_SEND = 0, BTAAC_PACE_WAIT_PREROLL, BTAAC_PACE_WAIT_SPACING, BTAAC_PACE_DRY };

typedef struct BtAacPace {
	uint32_t run;          /* 1 sending, 0 waiting for the pre-roll                                                       */
	uint32_t calls;        /* media hook calls this session while a unit could go                                          */
	uint32_t since;        /* calls since the last unit went, this one counted (saturates)                                  */
	uint32_t sent;         /* units sent                                                                                    */
	uint32_t first_call;   /* the call that sent the first unit (the cadence is counted from it)                            */
	uint32_t dry;          /* 1 while the ring has been found empty at a due call and no unit has gone since                */
	uint32_t miss_calls;   /* calls at which a unit was due and none was ready                                              */
	uint32_t underruns;    /* dry episodes                                                                                  */
	uint32_t prerolls;     /* pre-rolls entered after a dry ring (the first one, at the start, is not counted)               */
	uint32_t min_depth;    /* the fewest units waiting at a call where one was due, this session (UINT32_MAX = none yet)    */
	uint32_t max_depth;    /* the most                                                                                      */
} BtAacPace;

static inline void btaac_pace_reset(BtAacPace *p)
{
	p->run = 0;
	p->calls = 0;
	p->since = BTAAC_SPACING_CALLS;
	p->sent = 0;
	p->first_call = 0;
	p->dry = 0;
	p->miss_calls = 0;
	p->underruns = 0;
	p->prerolls = 0;
	p->min_depth = UINT32_MAX;
	p->max_depth = 0;
}

/* The units the cadence says should have gone by now: one for the first call that sent, one more for every two calls after it. 0 before anything was sent. */
static inline uint32_t btaac_pace_expected(const BtAacPace *p)
{
	return p->sent ? 1u + (p->calls - p->first_call) / BTAAC_SPACING_CALLS : 0u;
}

/* One call of the media hook with `depth` units waiting in the engine's output (those of this session). Says what to do; on BTAAC_PACE_SEND the caller pops one unit and,
 * if it got one, calls btaac_pace_sent. */
static inline int btaac_pace_step(BtAacPace *p, uint32_t preroll, uint32_t depth)
{
	int due;

	p->calls++;
	if (p->since < 0xFFFFu)
		p->since++;
	due = p->since >= BTAAC_SPACING_CALLS;
	if (due && p->sent) {                         /* the depth at the calls where a unit is due: the least it gets to is the jitter the stream had room for */
		if (depth < p->min_depth)
			p->min_depth = depth;
		if (depth > p->max_depth)
			p->max_depth = depth;
	}
	if (due && p->sent && !depth) {               /* a unit is due and none is ready: an underrun (the start's wait, before the first unit, is not one) */
		p->miss_calls++;
		if (!p->dry) {
			p->dry = 1;
			p->underruns++;
		}
	}
	if (!preroll)                                 /* before pacing: whatever is ready goes, on every call */
		return depth ? BTAAC_PACE_SEND : BTAAC_PACE_DRY;
	if (!p->run) {
		if (depth < preroll)
			return BTAAC_PACE_WAIT_PREROLL;
		p->run = 1;                               /* pre-roll done: this call sends */
		if (p->sent)
			p->prerolls++;
		return BTAAC_PACE_SEND;
	}
	if (!due)
		return BTAAC_PACE_WAIT_SPACING;
	if (!depth) {
		p->run = 0;                               /* dry: wait for the pre-roll again, send nothing in a burst when the ring refills */
		return BTAAC_PACE_DRY;
	}
	return BTAAC_PACE_SEND;
}

/* A unit went. */
static inline void btaac_pace_sent(BtAacPace *p)
{
	if (!p->sent)
		p->first_call = p->calls;
	p->sent++;
	p->since = 0;
	p->dry = 0;
}

/* The most units the engine's output may hold before the oldest are dropped (the Bluetooth Latency setting's limit, 0 = not bounded), never less than the pre-roll asks for. */
static inline uint32_t btaac_pace_keep(uint32_t keep, uint32_t preroll)
{
	return keep && keep < preroll ? preroll : keep;
}

#endif /* BTAAC_PACE_H */
