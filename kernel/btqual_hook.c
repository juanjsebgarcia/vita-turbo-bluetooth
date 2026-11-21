/*
 * btqual_hook.c - the hook body of the Bluetooth connection quality setting, and nothing else: this object
 * file is checked whole by tools/check_bteq_leaf.sh. It runs in the Bluetooth code's own thread, once when
 * headphones are connected and configured: no logging, no locks, no waits, no allocation, no floating
 * point, and no call but the original routine's.
 *
 * The encoder always uses the highest bitpool the headphones allow, which the Bluetooth code keeps in
 * the connection (next to the lowest) when the configuration is applied. A lower one makes every frame,
 * and so every packet, smaller: less air time per packet. With a cap set, the stored highest bitpool is
 * lowered to the cap right after the configuration is applied, but never below the lowest one the
 * headphones allow, so the encoder's range stays valid. With no cap (Standard) and no dual channel setting nothing is
 * written: the hook only reads, and the connection is exactly the console's own.
 *
 * High and Highest (bthq.c) share the hook: for them the offer the headphones made is first changed so that the
 * console's own order of channel modes lands on dual channel, and the stored highest bitpool is then capped for it.
 */
#include "btqual_hook.h"

#define REF_SPINS  200000u       /* loads, no system call: about a millisecond at most */
#define BAD_STATE  0x802F1901    /* the error SceBt itself returns when it cannot do the call (as btvol_hook.c) */
#define FLAG_APPLY 0x8u          /* the call applies a configuration (the other call only reads the headphones' capabilities) */
#define C_MINBP    0x3BB8u       /* in the connection: the lowest bitpool, then the highest (+0x3BBC) */

volatile tai_hook_ref_t btqual_ref;
volatile uint32_t btqual_in;
volatile uint32_t btqual_cap;

#define C_CHMODE   0x3BC4u       /* in the connection: the channel mode chosen (3 joint, 2 stereo, 1 dual, 0 mono) */
#define CH_DUAL    1u
#define HQ_BP_MID  38u           /* High: dual channel at 38 fits one 2-DH5 packet (4 frames + 13 bytes: 669 of 679) */
#define HQ_BP_MAX  59u           /* Highest: dual channel at the headphones' highest, at most what one 3-DH5 packet carries */
#define SBC_CODEC_CAT  7u        /* AVDTP service category: media codec */
#define SBC_MIN_LOSC   6u        /* its length for SBC: media type, codec type, four bytes of capability */

volatile uint32_t btqual_hq;

/* Walk the capabilities the headphones offered (AVDTP: category, length, then the length's bytes, repeated) to
 * the media codec element and, if it offers dual channel (bit 2 of its first capability byte), take the joint and
 * stereo bits out of it so the console's own order (joint, stereo, dual, mono) lands on dual. Returns 1 changed,
 * 0 dual not offered, -1 no SBC media codec element found. Never reads or writes past `len`. */
static int hq_prefer_dual(uint8_t *p, uint32_t len)
{
	uint32_t i = 0;

	while (i + 2 <= len) {
		uint32_t losc = p[i + 1];

		if (i + 2 + losc > len)
			break;
		if (p[i] == SBC_CODEC_CAT && losc >= SBC_MIN_LOSC && p[i + 3] == 0) {   /* codec type 0 = SBC */
			if (!(p[i + 4] & 0x04))
				return 0;
			p[i + 4] &= (uint8_t)~0x03;
			return 1;
		}
		i += 2 + losc;
	}
	return -1;
}

/* max(lowest, cap), but never above the headphones' own highest; a range that is already upside down
 * is left as it is. */
static uint32_t qual_clamp(uint32_t cap, uint32_t minbp, uint32_t maxbp)
{
	uint32_t limit = cap > minbp ? cap : minbp;

	if (!cap)
		return maxbp;
	return limit < maxbp ? limit : maxbp;
}

int btqual_hook(void *conn, uint32_t flags, const void *caps, uint32_t len)
{
	tai_hook_ref_t ref;
	uint32_t spins = REF_SPINS;
	int ret;

	/* The hook goes live a moment before taiHEN stores the reference. */
	while ((ref = btqual_ref) == 0 && --spins)
		;
	if (!ref)
		return (int)BAD_STATE;   /* never set: no call through it; the caller copes with the error (first millisecond after install only) */

	btqual_in = 1;
	if (btqual_hq && conn && caps && (flags & FLAG_APPLY)) {
		int r = hq_prefer_dual((uint8_t *)caps, len);
		(void)r;
	}
	ret = TAI_CONTINUE(int, ref, conn, flags, caps, len);
	if (conn && (flags & FLAG_APPLY)) {
		volatile uint32_t *bp = (volatile uint32_t *)((uint8_t *)conn + C_MINBP);   /* [0] lowest, [1] highest */
		uint32_t cap = btqual_cap;
		uint32_t in = bp[1], out;

		if (btqual_hq) {
			uint32_t mode = *(volatile uint32_t *)((uint8_t *)conn + C_CHMODE), hq = btqual_hq == BTHQ_HIGH ? HQ_BP_MID : HQ_BP_MAX;
			if (mode == CH_DUAL && (!cap || hq < cap))
				cap = hq;   /* dual channel: the packet limit High and Highest aim at, below any cap of Stable and Balanced too */
		}
		out = qual_clamp(cap, bp[0], in);
		if (out != in)
			bp[1] = out;
	}
	btqual_in = 0;
	return ret;
}
