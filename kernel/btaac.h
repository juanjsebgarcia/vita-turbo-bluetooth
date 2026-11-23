/*
 * btaac.h - the AAC engine: one kernel thread that turns PCM into AAC access units, and the two rings the audio sender talks to it
 * through. Stage 1 of the AAC feature: nothing in the Bluetooth code calls it yet.
 *
 * Who calls what:
 *   - module start and stop (kernel/main.c):   btaac_init, btaac_exit
 *   - the worker's late pass (kernel/main.c):  btaac_sync, once the start-up period is over
 *   - a connection that wants AAC (later):     btaac_start, btaac_stop: they only post the request, the engine's thread does it
 *   - the audio sender's thread (later):       btaac_push_pcm, btaac_pop_au: leaves, see btaac_leaf.c
 *
 * The thread is made once, at module start: a thread cannot be made from inside a system call (it failed with -1 on the console),
 * and the encoder needs one of its own. It idles on an event flag while there is no session.
 */
#ifndef BTAAC_H
#define BTAAC_H

#include <stdint.h>

#include "btaac_core.h"

#define BTAAC_PCM_RING_FRAMES 16384u  /* the PCM input ring (btaac_leaf.c): 341 ms, to ride out a stall of the engine's thread */
#define BTAAC_AU_RING_SLOTS   16u     /* the output ring: 16 units, 341 ms                                                      */
#define BTAAC_AU_MAX       1536u    /* the most one access unit can be (the encoder's own ceiling for two channels) */

/* Errors of btaac_start (negative). */
#define BTAAC_OK           0
#define BTAAC_ERR_ARG      (-1)     /* a rate or channel count the engine does not encode                              */
#define BTAAC_ERR_THREAD   (-2)     /* the engine's thread is not there                                                */
#define BTAAC_ERR_BUSY     (-3)     /* a session is running or a request is waiting                                    */
#define BTAAC_ERR_HELD     (-4)     /* the crash record says no AAC this start (an unclean session, or a banned build) */
#define BTAAC_ERR_NOTEST   (-5)     /* the self-test has not passed (not run yet, or the C build failed it)            */
#define BTAAC_ERR_RATE     (-6)     /* the headphones' highest rate is below the lowest the encoder can make           */

/* Session states (BtAacInfo.state). */
enum BtAacState { BTAAC_S_IDLE = 0, BTAAC_S_STARTING, BTAAC_S_RUNNING, BTAAC_S_FAILED };

/* The engine's state and counters, for the log. */
typedef struct BtAacInfo {
	uint32_t state;           /* BtAacState                                                                          */
	int32_t  last_error;      /* what ended or refused the last session: 0, or one of BTAAC_E_*                      */
	uint32_t selftest;        /* BtAacSelfTest                                                                       */
	uint32_t neon_allowed;    /* the NEON build may be used (the self-test passed)                                   */
	uint32_t hold;            /* the crash record holds AAC off this start                                           */
	uint32_t crashes;         /* sessions in a row that did not end cleanly                                          */
	uint32_t neon_in_use;     /* the session's encoder is the NEON build                                             */
	uint32_t bps;             /* the session's bit rate, after the clamps                                            */
	uint32_t rate, channels;
	uint32_t au_cap;          /* the session's longest access unit, bytes (what one packet of the link takes); 0 = not bounded */
	uint32_t reservoir;       /* the bit reservoir the encoder was held to, bytes (0 = none asked for)                 */
	uint32_t cap_state;       /* BTAAC_CAP_*: whether the encoder was held to it                                      */
	uint32_t pcm_overruns;    /* blocks the PCM ring had no room for                                                 */
	uint32_t pcm_frames_in;   /* frames accepted from the sender                                                     */
	uint32_t aus_out;         /* access units produced                                                               */
	uint32_t aus_popped;      /* ... taken by the sender                                                             */
	uint32_t au_drops;        /* access units dropped (a buffer too small, or an encoder output that did not fit)    */
	uint32_t slow_frames;     /* encode calls longer than one frame's time (21.3 ms)                                 */
	uint32_t enc_max_us;      /* the longest encode call of the session                                              */
	uint32_t enc_mean_us;     /* the mean                                                                            */
	uint32_t thread_err;      /* the engine thread's creation error (0 = made)                                       */
	uint32_t cpu_mask;        /* the mask the thread has (0 = the system's choice)                                   */
	uint32_t cpu_mask_err;    /* the error the system core's mask gave when it was refused and the default used      */
	uint32_t assert_fails;    /* failed consistency checks of the encoder, ever                                      */
	uint32_t prio;            /* the engine thread's priority while a session runs (what it was asked for)            */
	uint32_t prio_src;        /* BTAAC_PRIO_SRC_*: computed from the sender's, a set value, the fallback    */
	uint32_t pcm_max_frames;  /* the most frames the PCM input ring held at once this session (a stall shows as this growing) */
} BtAacInfo;

/* Codes of cap_state. */
#define BTAAC_CAP_NONE     0        /* the session is not bounded (no packet limit given)                                     */
#define BTAAC_CAP_APPLIED  1        /* the encoder's reservoir was held to the cap                                            */
#define BTAAC_CAP_FAILED   2        /* it could not be (the encoder's record was not as expected): longer units are cut into pieces */

/* Codes of last_error. */
#define BTAAC_E_NONE       0
#define BTAAC_E_MEMORY     1        /* the encoder's memory block could not be had, or was too small                   */
#define BTAAC_E_INIT       2        /* the encoder refused to start                                                    */
#define BTAAC_E_ENCODE     3        /* an encode call failed                                                           */
#define BTAAC_E_ASSERT     4        /* the encoder's own consistency check failed                                      */
#define BTAAC_E_TOOBIG     5        /* an access unit larger than a ring slot                                          */
#define BTAAC_E_WAIT       6        /* the thread's wait failed                                                        */
#define BTAAC_E_REFUSED    7        /* the engine's thread was asked for a session the crash record or the self-test does not allow */

/* Module start and stop: create the thread and its event flag (pinned to the system core; the default placement if the kernel
 * refuses that, and the log says so), and read the crash record; end a running session, stop the thread. Both may sleep. */
void btaac_init(void);
void btaac_exit(void);

/* Called by the worker from the early gate (the Home screen is up, 12 s of uptime; one add-on's test at a time, kernel/main.c): asks the engine's thread for the self-test, once. */
void btaac_sync(int on);

/* Asks for a session; returns at once with BTAAC_OK (queued) or an error. The bit rate is `kbps` kb/s, brought into the encoder's range and
 * under `max_kbps` (the headphones' highest; 0 = none). `mtu` is the link's packet limit in bytes (0 = unknown or unbounded: no cap): the
 * encoder is then held to access units that go out in one packet (btaac_au_cap), and a bit rate whose average frame leaves it no room is brought down to the
 * highest that does. `accel` asks for the NEON build; it is used only if the self-test passed.
 * Any thread, no waiting. The session is RUNNING once the engine's thread has made the encoder: btaac_get_info says. */
int btaac_start(uint32_t kbps, uint32_t rate, uint32_t channels, uint32_t max_kbps, int accel, uint32_t mtu);

/* Asks for the session to end; the encoder is closed and the crash record cleared by the engine's thread. Any thread, no waiting. */
void btaac_stop(void);

void btaac_get_info(BtAacInfo *out);

/* ---- the leaves (kernel/btaac_leaf.c): the audio sender's thread calls these; no system call, no lock, no wait, no
 * floating point or vector instruction (tools/check_bteq_leaf.sh checks the object). One caller thread each. ---- */

/* `frames` frames of interleaved signed 16-bit samples (the session's channel count per frame) into the PCM ring. Returns the
 * frames taken: all of them, or 0 when no session is running or the ring has no room for the whole block (counted). */
uint32_t btaac_push_pcm(const int16_t *pcm, uint32_t frames);

/* The oldest finished access unit of the running session into `out` (`cap` bytes, at least BTAAC_AU_MAX). Returns its length, or
 * 0 when none is ready (or `cap` is too small for the one waiting: it is dropped and counted). `*seq`, if not NULL, is the unit's
 * number in the session, from 0: unit n covers frames n * 1024 .. n * 1024 + 1023 of the stream, less the encoder's delay. */
uint32_t btaac_pop_au(uint8_t *out, uint32_t cap, uint32_t *seq);

/* The sender's side again (a leaf): drops the oldest units until at most `keep` wait in the output ring; returns how many it dropped. The sender calls it
 * before each pop, so that what is queued in the engine never holds more than the Bluetooth latency setting allows. */
uint32_t btaac_au_trim(uint32_t keep);

/* The units of the running session waiting in the output ring (a leaf, one load per slot): what the sender's pre-roll counts. */
uint32_t btaac_au_depth(void);

/* ---- the ring's other side, for the engine's thread (btaac.c) ---- */
void     btaac_ring_open(uint32_t channels);   /* a new session's rings: PCM emptied, unit numbers from 0, the old units dead */
void     btaac_ring_close(void);               /* pushes are refused again */
uint32_t btaac_pcm_avail(void);                /* frames waiting */
void     btaac_pcm_take(int16_t *dst, uint32_t frames);
int      btaac_au_put(const uint8_t *au, uint32_t len);   /* 1 when queued, 0 when the ring is full */
int      btaac_au_room(void);
void     btaac_ring_info(uint32_t *overruns, uint32_t *frames_in, uint32_t *popped, uint32_t *drops, uint32_t *out);

#endif /* BTAAC_H */
