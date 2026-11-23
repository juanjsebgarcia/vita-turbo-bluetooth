/*
 * btldac.h - the LDAC engine: one kernel thread that turns PCM into LDAC packets (libldac, integer build), and the two rings the audio sender talks to it through.
 * The same shape as the AAC engine (btaac.h), smaller: one build of the encoder, no bit reservoir to hold, a packet in the ring is what goes on the air.
 *
 * Who calls what:
 *   - module start and stop (kernel/btldac_main.c): btldac_init, btldac_exit
 *   - the worker's early pass (through the table): btldac_sync, from the early gate, one add-on's test at a time
 *   - a connection that is LDAC:                   btldac_start, btldac_stop, btldac_set_mode: they only post the request, the engine's thread does it
 *   - the audio sender's thread:                   btldac_push_pcm, btldac_pop_pkt, btldac_pkt_trim, btldac_pkt_depth: leaves, see btldac_leaf.c
 */
#ifndef BTLDAC_H
#define BTLDAC_H

#include <stdint.h>

#include "btldac_core.h"
#include "btldac_enc.h"

/* Errors of btldac_start (negative). */
#define BTLDAC_OK           0
#define BTLDAC_ERR_ARG      (-1)    /* a mode that does not exist                                                  */
#define BTLDAC_ERR_THREAD   (-2)    /* the engine's thread is not there                                            */
#define BTLDAC_ERR_BUSY     (-3)    /* a session is running or a request is waiting                                */
#define BTLDAC_ERR_HELD     (-4)    /* the crash record says no LDAC this start                                    */
#define BTLDAC_ERR_NOTEST   (-5)    /* the self-test has not passed                                                */
#define BTLDAC_ERR_MTU      (-6)    /* the media channel's limit is below what libldac needs (679)                 */

enum BtLdacState { BTLDAC_S_IDLE = 0, BTLDAC_S_STARTING, BTLDAC_S_RUNNING, BTLDAC_S_FAILED };

/* The engine's state and counters, for the log and the readings. */
typedef struct BtLdacInfo {
	uint32_t state;           /* BtLdacState                                                                       */
	int32_t  last_error;      /* what ended or refused the last session: 0, or a BTLDAC_E_* code                  */
	uint32_t selftest;        /* BtLdacSelfTest                                                                    */
	uint32_t hold;            /* the crash record holds LDAC off this start                                        */
	uint32_t banned;          /* the self-test took the console down once: the encoder is never run                */
	uint32_t guard_ok;        /* the crash record can be written                                                  */
	uint32_t crashes;         /* sessions in a row that did not end cleanly                                        */
	uint32_t mode;            /* the session's BTLDAC_MODE_*                                                       */
	uint32_t eqmid;           /* the quality index in force (adaptive modes move it)                               */
	uint32_t kbps;            /* the bit rate of the packet last made                                              */
	uint32_t mtu;             /* the limit the session was made for                                                */
	uint32_t pcm_overruns;    /* blocks the PCM ring had no room for                                               */
	uint32_t pcm_frames_in;   /* frames accepted from the sender                                                   */
	uint32_t pkts_out;        /* packets produced                                                                  */
	uint32_t pkts_popped;     /* ... taken by the sender                                                           */
	uint32_t pkt_drops;       /* packets dropped (a buffer too small)                                              */
	uint32_t slow_calls;      /* encode calls longer than one call's time (2.667 ms)                               */
	uint32_t enc_max_us;      /* the longest encode call of the session                                            */
	uint32_t enc_mean_us;     /* the mean                                                                          */
	uint32_t thread_err;      /* the engine thread's creation error (0 = made)                                     */
	uint32_t cpu_mask;        /* the mask the thread has (0 = the system's choice)                                 */
	uint32_t cpu_mask_err;
	uint32_t prio;            /* the engine thread's priority while a session runs                                 */
	uint32_t prio_src;        /* BTAAC_PRIO_SRC_* (btaac_core.h's rule)                                            */
	uint32_t pcm_max_frames;  /* the most frames the PCM input ring held at once this session                      */
	uint32_t abr_calls, abr_down, abr_up;   /* the adaptive code: calls, steps to a lower rate, to a higher one    */
	uint32_t heap_peak, heap_refused;       /* the static block: the most used, allocations it refused             */
	uint32_t st_us[2];        /* the self-test's time at 330 and 660, microseconds                                  */
	uint32_t st_pkts[2], st_bytes[2];
	uint32_t st_hash[4];      /* the two runs' hashes, high word first, 330 then 660                                */
} BtLdacInfo;

/* Module start and stop: create the thread and its event flag (pinned to the system core), read the crash record; end a running session, stop the thread. May sleep. */
void btldac_init(void);
void btldac_exit(void);

/* The late pass: asks the engine's thread for the self-test, once. */
void btldac_sync(int on);

/* Asks for a session; returns at once with BTLDAC_OK (queued) or an error. `mode` is a BTLDAC_MODE_*, `mtu` the media channel's limit in bytes (the library needs 679).
 * The session is RUNNING once the engine's thread has made the encoder: btldac_get_info says. Any thread, no waiting. */
int  btldac_start(uint32_t mode, uint32_t mtu);
void btldac_stop(void);
/* Changes the bit rate setting of a running session (the next call of the engine's thread takes it); remembered for the next session too. */
void btldac_set_mode(uint32_t mode);
void btldac_get_info(BtLdacInfo *out);

/* ---- the leaves (kernel/btldac_leaf.c): the audio sender's thread calls these; no system call, no lock, no wait, no floating point or vector instruction. One caller each. ---- */

/* `frames` frames of interleaved 16-bit stereo into the PCM ring: all of them, or 0 when no session runs or the ring has no room for the whole block (counted). */
uint32_t btldac_push_pcm(const int16_t *pcm, uint32_t frames);
/* The oldest finished packet of the running session: its frames into `out` (`cap` at least BTLDAC_FRAMES_BYTES); returns their length, 0 when none is ready. `*nframes` is
 * the number of frames in it, `*seq` (if not NULL) its number in the session. */
uint32_t btldac_pop_pkt(uint8_t *out, uint32_t cap, uint32_t *nframes, uint32_t *seq);
/* Drops the oldest packets until at most `keep` wait; returns how many it dropped. */
uint32_t btldac_pkt_trim(uint32_t keep);
/* The packets of the running session waiting in the ring. */
uint32_t btldac_pkt_depth(void);

/* ---- the ring's other side, for the engine's thread (btldac.c) ---- */
void     btldac_ring_open(void);
void     btldac_ring_close(void);
uint32_t btldac_pcm_avail(void);
void     btldac_pcm_take(int16_t *dst, uint32_t frames);
int      btldac_pkt_put(const uint8_t *frames, uint32_t len, uint32_t nframes);   /* 1 when queued, 0 when the ring is full */
int      btldac_pkt_room(void);
void     btldac_ring_info(uint32_t *overruns, uint32_t *frames_in, uint32_t *popped, uint32_t *drops, uint32_t *out);

#endif /* BTLDAC_H */
