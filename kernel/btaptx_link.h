/*
 * btaptx_link.h - the main kernel module's side of the aptX add-on (kernel/btaptx_api.h), the counterpart of btaac_link.h: finding it, asking for its
 * self-test, saying whether aptX is on offer, the session calls, and the one leaf the audio sender's hook calls. Everything is safe with the add-on absent:
 * that is the normal case (installing the module is the opt-in), and the main module then behaves exactly as without any of this.
 */
#ifndef BTAPTX_LINK_H
#define BTAPTX_LINK_H

#include <stdint.h>

/* Looks the add-on's export up, once (called every pass after the early gate; the first call looks, the rest do nothing). */
void btt_aptx_link_find(void);

/* From the early gate (12 s, main.c's self-test pass): asks for the self-test (once); when the Bluetooth Codec key is aptX and the add-on is not there, says so once in the log. */
void btt_aptx_link_sync(int on, int codec);

/* 1 when there is nothing to wait for: no add-on, or its verdict is in (any verdict; the aptX test runs inside btt_aptx_link_sync). */
int btt_aptx_selftest_done(void);

/* What the Settings page may offer: the BTT_APTX_AVAIL_* bits that bttGet(BTT_KEY_APTX_AVAIL) returns; 0 while the add-on is not found. */
int btt_aptx_avail(void);

/* The codec to send: the key's value, except that aptX is only honoured while the add-on is there, its self-test passed and the crash record does not hold
 * it off; AAC as btt_aac_codec_effective says; otherwise SBC. */
int btt_codec_effective(int codec);
/* 1 when aptX HD may be offered: the add-on is there, both self-tests passed and the record does not hold it off. */
int btt_aptx_hd_usable(void);

/* ... for aptX alone (SBC for any other value): what the apply-at-once decisions ask. */
int btt_aptx_codec_effective(int codec);

/* The session (the worker's thread, or the standby handler for the stop): through the table; BTAPTX_ERR_NOCTX with no add-on. */
int btt_aptx_session_start(int hd);   /* aptX HD when hd */
void btt_aptx_session_stop(void);

/* The leaf (kernel/btaptx_link_leaf.c): the add-on's encoder through its table. No system call, no lock; 0 with no add-on. */
uint32_t btt_aptx_encode(const int16_t *pcm, uint8_t *out);
uint32_t btt_aptx_encode_hd(const int16_t *pcm, uint8_t *out);   /* 768 bytes of aptX HD */

#endif /* BTAPTX_LINK_H */
