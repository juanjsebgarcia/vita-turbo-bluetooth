/*
 * btaac_link.h - the main kernel module's side of the AAC module (kernel/btaac_api.h): finding it, asking its engine for the self-test,
 * saying whether AAC is on offer, and the two leaves the audio sender's hooks will call. Everything here is safe with the AAC module absent:
 * that is the normal case (installing the module is the opt-in), and the main module then behaves exactly as without any of this.
 */
#ifndef BTAAC_LINK_H
#define BTAAC_LINK_H

#include <stdint.h>

/* Looks the AAC module's export up, once (called every pass after the early gate; the first call looks, the rest do nothing). Every
 * *KERNEL module has loaded long before: the gate is 12 s of uptime with the Home screen up. */
void btt_aac_link_find(void);

/* From the early gate (12 s, main.c's self-test pass): asks the engine for its self-test (once), and, when the Bluetooth Codec key is AAC but the AAC module is not
 * there, says so once in the log. `on` is the master switch. */
void btt_aac_link_sync(int on, int codec);

/* 1 when the self-test needs no more waiting for: no AAC module, or its verdict is in (any verdict). The start-up passes ask it before the next module's test is started. */
int btt_aac_selftest_done(void);

/* What the Settings page may offer: the BTT_AAC_AVAIL_* bits that bttGet(BTT_KEY_AAC_AVAIL) returns; 0 while the AAC module is not found. */
int btt_aac_avail(void);

/* The codec to send: the Bluetooth Codec key's value, except that AAC is only honoured while the AAC module is there, its self-test has
 * passed for the C build at least, and the crash record does not hold AAC off; otherwise SBC. */
int btt_aac_codec_effective(int codec);

/* The leaves (kernel/btaac_link_leaf.c): btaac_push_pcm and btaac_pop_au of the AAC module, through its table. No system call, no lock, no
 * vector instruction, and with no AAC module they take and give nothing. The pointer calls are indirect, so the leaf check sees no call. */
uint32_t btt_aac_push_pcm(const int16_t *pcm, uint32_t frames);
uint32_t btt_aac_pop_au(uint8_t *out, uint32_t cap, uint32_t *seq);
uint32_t btt_aac_au_trim(uint32_t keep);
uint32_t btt_aac_au_depth(void);

#endif /* BTAAC_LINK_H */
