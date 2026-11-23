/*
 * btldac_enc.h - a thin wrapper over libldac (third_party/libldac) and its adaptive code: one encoder at a time, opened from the add-on's static block (btldac_mem.h),
 * fed one call of 128 frames at a time. No system call, no thread, no file: the engine (btldac.c) and the host test (test/test_btldac.c) both use exactly this.
 */
#ifndef BTLDAC_ENC_H
#define BTLDAC_ENC_H

#include <stdint.h>

#include "btldac_core.h"

#define BTLDAC_OUT_MAX   1024u   /* the library's own bound for what one call writes (LDACBT_MAX_NBYTES) */

typedef struct BtLdacEnc {
	void    *h;              /* the library's handle                                      */
	void    *abr;            /* the adaptive code's handle (made with every encoder)      */
	uint32_t mode;           /* BTLDAC_MODE_*                                             */
	int      open;
	int      err;            /* the library's error code for the last failure              */
	int      eqmid;          /* the quality index in force (what the library says)         */
	int      kbps;           /* the bit rate of the packet last made, as the library says  */
	uint32_t abr_calls, abr_steps_down, abr_steps_up;
} BtLdacEnc;

/* Opens an encoder for 48 kHz stereo (16-bit input) for a media channel limit of `mtu` (the library refuses below 679) in `mode`. Clears the add-on's block first.
 * 0 on success, else a BTLDAC_E_* code (the block is released on a failure). */
enum { BTLDAC_E_NONE = 0, BTLDAC_E_MTU = 1, BTLDAC_E_MEMORY = 2, BTLDAC_E_INIT = 3, BTLDAC_E_ENCODE = 4, BTLDAC_E_TOOBIG = 5, BTLDAC_E_REFUSED = 6, BTLDAC_E_WAIT = 7, BTLDAC_E_RECORD = 8 };
int  btldac_enc_open(BtLdacEnc *e, uint32_t mode, uint32_t mtu);
void btldac_enc_close(BtLdacEnc *e);

/* One call: 128 frames of interleaved 16-bit stereo in; `*len` bytes of whole frames out (0 when no packet came out of this call) and `*nframes` of them. `out` takes
 * BTLDAC_OUT_MAX bytes. 0, or a BTLDAC_E_* code. */
int  btldac_enc_call(BtLdacEnc *e, const int16_t *pcm, uint8_t *out, uint32_t *len, uint32_t *nframes);

/* Changes the mode of a running encoder (the quality index only: the configuration the sink was given does not change). 0 or a BTLDAC_E_* code. */
int  btldac_enc_set_mode(BtLdacEnc *e, uint32_t mode);

/* The quality index by hand (the development bench measures 990 kb/s, index 0, which no setting offers). 0 or a BTLDAC_E_* code. */
int  btldac_enc_set_eqmid(BtLdacEnc *e, int eqmid);

/* The adaptive step (every BTLDAC_ABR_MS in the adaptive mode): `depth` packets waiting. Never above 660 kb/s (btldac_abr_clamp). Returns the quality index in force, or -1. */
int  btldac_enc_abr(BtLdacEnc *e, uint32_t depth);

/* The self-test's run of one fixed mode into `run` (the signal of btldac_sig, BTLDAC_ST_CALLS calls). Opens and closes an encoder of its own. 0 or a BTLDAC_E_* code. */
int  btldac_selftest_encode(uint32_t mode, BtLdacRun *run);

#endif
