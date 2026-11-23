/*
 * btldac_link.h - the main kernel module's side of the LDAC add-on (kernel/btldac_api.h), the counterpart of btaac_link.h: finding it, asking for its self-test, saying
 * whether LDAC is on offer, the session calls, and the leaves the audio sender's hooks call. Everything is safe with the add-on absent: that is the normal case
 * (installing the module is the opt-in), and the main module then behaves exactly as without any of this.
 */
#ifndef BTLDAC_LINK_H
#define BTLDAC_LINK_H

#include <stdint.h>

/* Looks the add-on's export up, once (called every pass after the early gate; the first call looks, the rest do nothing). */
void btt_ldac_link_find(void);

/* From the early gate (12 s, main.c's self-test pass): asks for the self-test (once); when the Bluetooth Codec key is LDAC and the add-on is not there, says so once in the log. */
void btt_ldac_link_sync(int on, int codec);

/* 1 when the self-test needs no more waiting for: no add-on, or its verdict is in (any verdict). */
int btt_ldac_selftest_done(void);

/* What the Settings page may offer: the BTT_LDAC_AVAIL_* bits that bttGet(BTT_KEY_LDAC_AVAIL) returns; 0 while the add-on is not found. */
int btt_ldac_avail(void);

/* LDAC alone: the key's value, except that LDAC is only honoured while the add-on is there, its self-test passed and the crash record does not hold it off (SBC for any other value). */
int btt_ldac_codec_effective(int codec);

/* The session (the worker's thread): through the table; BTLDAC_ERR_THREAD with no add-on. */
int  btt_ldac_session_start(uint32_t mode, uint32_t mtu);
void btt_ldac_session_stop(void);
int  btt_ldac_session_running(void);           /* 1 while the add-on's session is running or starting */
void btt_ldac_set_mode(uint32_t mode);         /* the bit rate setting, applied to a running session at once */
int  btt_ldac_state(void);                     /* BtLdacState of the add-on's session (BTLDAC_S_IDLE with no add-on) */
int  btt_ldac_last_error(void);

/* The leaves (kernel/btldac_link_leaf.c): the add-on's ring routines through its table. No system call, no lock; nothing with no add-on. */
uint32_t btt_ldac_push_pcm(const int16_t *pcm, uint32_t frames);
uint32_t btt_ldac_pop_pkt(uint8_t *out, uint32_t cap, uint32_t *nframes, uint32_t *seq);
uint32_t btt_ldac_pkt_trim(uint32_t keep);
uint32_t btt_ldac_pkt_depth(void);

#endif /* BTLDAC_LINK_H */
