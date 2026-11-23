/*
 * btaac_gate.h - the check the AAC stream hooks make on the Bluetooth code's bytes before they hook anything. Pure: no system calls, so the
 * host test (test/test_btaac_stream.c) runs it too. The bytes are in btaac_gate_data.h, the same on the
 * twelve builds 3.60-3.74.
 */
#ifndef BTAAC_GATE_H
#define BTAAC_GATE_H

#include <stdint.h>

#include "btaac_gate_data.h"

typedef struct BtAacWin {
	uint32_t off;
	const uint8_t *want, *mask;      /* mask NULL: every bit is compared */
	uint32_t n;
} BtAacWin;
#define BTAAC_NWIN 10u   /* windows compared byte for byte; BTAAC_NHWIN more are compared by a hash (below); the gate numbers the first kind from 1, the second after them */

/* The windows, in the order the gate numbers them (from 1): the signalling receive routine's head, its four windows, the media send routine's two, the frame sender's
 * head and window, the format. The three heads' and the dispatcher's fourth window's address loads (movw/movt) are masked: the loader writes them. */
static const BtAacWin k_btaac_wins[BTAAC_NWIN] = {
	{ BTAAC_OFF_DISP,    k_disp_head, k_disp_head_mask, sizeof(k_disp_head) },
	{ BTAAC_OFF_DISP_W1, k_disp_w1,   0,                sizeof(k_disp_w1) },
	{ BTAAC_OFF_DISP_W2, k_disp_w2,   0,                sizeof(k_disp_w2) },
	{ BTAAC_OFF_DISP_W3, k_disp_w3,   0,                sizeof(k_disp_w3) },
	{ BTAAC_OFF_DISP_W4, k_disp_w4,   k_disp_w4_mask,   sizeof(k_disp_w4) },
	{ BTAAC_OFF_SEND,    k_send_a,    0,                sizeof(k_send_a) },
	{ BTAAC_OFF_SEND_B,  k_send_b,    0,                sizeof(k_send_b) },
	{ BTAAC_OFF_L2,      k_l2_head,   k_l2_head_mask,   sizeof(k_l2_head) },
	{ BTAAC_OFF_L2_W,    k_l2_w,      0,                sizeof(k_l2_w) },
	{ BTAAC_OFF_FMT,     k_fmt,       0,                sizeof(k_fmt) },
};

/* FNV-1a (32 bits) over the `n` bytes at `p`, with the four bytes of each address load whose offset is in `words` (ascending, nw of them) masked to what the loader
 * leaves alone (a movw/movt keeps its opcode and register). */
static inline uint32_t btaac_masked_hash(const volatile uint8_t *p, uint32_t n, const uint16_t *words, uint32_t nw)
{
	static const uint8_t keep[4] = { 0xF0, 0xFB, 0x00, 0x0F };
	uint32_t h = 2166136261u, k = 0;

	for (uint32_t i = 0; i < n; i++) {
		uint8_t b = p[i];

		while (k < nw && words[k] + 4u <= i)
			k++;
		if (k < nw && i >= words[k])
			b &= keep[i - words[k]];
		h = (h ^ b) * 16777619u;
	}
	return h;
}

/* Hashed window `w` (0-based) of the live code: 1 if its hash is the header's. */
static inline int btaac_hwin_ok(const volatile uint8_t *text, unsigned w)
{
	return btaac_masked_hash(text + k_hw_off[w], k_hw_len[w], k_hw_loader + k_hw_first[w], (uint32_t)(k_hw_first[w + 1] - k_hw_first[w])) == k_hw_hash[w];
}

/* The live hash of hashed window `w` (for the log and the development port). */
static inline uint32_t btaac_hwin_live(const volatile uint8_t *text, unsigned w)
{
	return btaac_masked_hash(text + k_hw_off[w], k_hw_len[w], k_hw_loader + k_hw_first[w], (uint32_t)(k_hw_first[w + 1] - k_hw_first[w]));
}

/* The index (from 0) of the first byte of window `w` (0-based) that differs from the source, masked; -1 if the window matches. */
static inline int btaac_win_diff(const volatile uint8_t *text, unsigned w)
{
	const BtAacWin *k = &k_btaac_wins[w];

	for (uint32_t i = 0; i < k->n; i++) {
		uint8_t m = k->mask ? k->mask[i] : 0xFFu;

		if ((text[k->off + i] & m) != (k->want[i] & m))
			return (int)i;
	}
	return -1;
}

/* `text` is the start of the Bluetooth code's text segment. Returns 0 when every window matches, else the number (from 1) of the first window that does not
 * (BTAAC_NWIN + 1 and up: a hashed window). */
static inline int btaac_gate(const volatile uint8_t *text)
{
	for (unsigned w = 0; w < BTAAC_NWIN; w++)
		if (btaac_win_diff(text, w) >= 0)
			return (int)w + 1;
	for (unsigned w = 0; w < BTAAC_NHWIN; w++)
		if (!btaac_hwin_ok(text, w))
			return (int)(BTAAC_NWIN + w) + 1;
	return 0;
}

#endif /* BTAAC_GATE_H */
