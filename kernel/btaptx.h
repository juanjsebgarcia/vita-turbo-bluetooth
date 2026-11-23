/*
 * btaptx.h - the aptX add-on's engine (bluetoothturbo_aptx.skprx): the encoder's context, the self-test, the crash record, and the one leaf the audio
 * sender's thread calls (btaptx_encode, kernel/btaptx_leaf.c).
 *
 * Unlike the AAC engine there is no thread and no ring: the encoder is a few hundred integer operations a block, so it runs inline in the thread that
 * sends the packet, as the console's own SBC encoder does. What it calls there is a leaf (no system call, no lock, no vector instruction: checked on the
 * object files). The session (start, stop) and the self-test run in the main module's worker, never in a hook.
 */
#ifndef BTAPTX_H
#define BTAPTX_H

#include <stdint.h>

#include "btaptx_core.h"

/* Errors of btaptx_start (negative). */
#define BTAPTX_OK            0
#define BTAPTX_ERR_BUSY      (-1)   /* a session is running                                                             */
#define BTAPTX_ERR_HELD      (-2)   /* the crash record holds aptX off                                                  */
#define BTAPTX_ERR_NOTEST    (-3)   /* the self-test has not passed                                                     */
#define BTAPTX_ERR_RECORD    (-4)   /* the crash record cannot be written: no encoder is run that nobody could notice crashing */
#define BTAPTX_ERR_NOCTX     (-5)   /* the encoder's state could not be made                                            */

/* The room for each encoder's state (the records are 4,588 and 4,840 bytes on the console, 5,008 and 5,256 on a 64-bit host: the host test checks it). */
#define BTAPTX_STATE_MAX     5376u

enum BtAptxState { BTAPTX_S_IDLE = 0, BTAPTX_S_RUNNING };

typedef struct BtAptxInfo {
	uint32_t state;         /* BtAptxState                                                                  */
	uint32_t selftest;      /* BtAptxSelfTest                                                               */
	uint32_t hold;          /* the crash record holds aptX off this start                                   */
	uint32_t banned;        /* the self-test took the console down once: the encoder is never run           */
	uint32_t crashes;       /* sessions in a row that did not end cleanly                                   */
	uint32_t guard_ok;      /* the crash record could be written                                            */
	uint32_t ctx_ok;        /* the encoder's context was made                                               */
	uint32_t st_us;         /* the last self-test's time, microseconds                                       */
	uint32_t st_bytes;      /* ... the bytes it made                                                         */
	uint32_t hash_lo, hash_hi;   /* ... and their hash                                                       */
	uint32_t st_runs;       /* self-tests run since the module started                                      */
	uint32_t sessions;      /* sessions started                                                              */
	uint32_t blocks;        /* blocks encoded, ever                                                          */
	uint32_t errors;        /* blocks the encoder refused                                                    */
	uint32_t enc_max_us, enc_mean_us, enc_slow;   /* time of an encode call, and calls over 1 ms (measured in test builds only) */
	/* interface version 2: aptX HD */
	uint32_t selftest_hd;   /* BtAptxSelfTest of the HD encoder's self-test (it runs after the classic one passed)                       */
	uint32_t hd_ctx_ok;     /* the HD encoder's context was made                                                                         */
	uint32_t hd_bytes;      /* the HD self-test's bytes                                                                                  */
	uint32_t hd_hash_lo, hd_hash_hi;   /* ... and their hash                                                                             */
	uint32_t hd_session;    /* the running session is HD                                                                                  */
} BtAptxInfo;

void btaptx_init(void);
void btaptx_exit(void);
void btaptx_sync(int on);                 /* the self-test, once (the worker's early pass, in this call) */
int  btaptx_selftest_run(void);           /* the self-test now; returns the verdict (not while a session runs)            */
int  btaptx_start(int hd);                /* a session (aptX HD when hd): the encoder reset, the record marked (worker thread; writes a file) */
void btaptx_stop(void);                   /* ends it: the record cleared (worker thread or the standby handler)             */
void btaptx_get_info(BtAptxInfo *out);

/* The HD leaf: 512 frames of interleaved signed 16-bit stereo into 768 bytes of aptX HD (128 frames of 6 bytes). Returns the bytes made (768) or 0. */
uint32_t btaptx_encode_hd(const int16_t *pcm, uint8_t *out);

/* The leaf: 512 frames of interleaved signed 16-bit stereo into 512 bytes of aptX. Returns the bytes made (512) or 0 (no session, or the encoder refused). */
uint32_t btaptx_encode(const int16_t *pcm, uint8_t *out);

/* shared between btaptx.c and btaptx_leaf.c */
extern void *volatile btaptx_ctx;       /* the classic encoder's state (in btaptx.c), 0 when it could not be made                                */
extern void *volatile btaptx_ctx_hd;    /* the aptX HD encoder's state                                                                          */
extern volatile uint32_t btaptx_running;               /* 0 idle, 1 a classic session (or its self-test), 2 an HD one          */
extern volatile uint32_t btaptx_n_blocks, btaptx_n_errors;

#endif /* BTAPTX_H */
