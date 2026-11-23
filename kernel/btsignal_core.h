/*
 * btsignal_core.h - the radio link's signal as a word (Excellent, Good, Fair, Weak), worked out from the controller's two readings, with no system call:
 * the host test compiles it as it is (test/test_btaac_stream.c, "the signal").
 *
 * What is read: the Bluetooth code's information call for a device (btcall_signal) gives the controller's link quality and its signal strength reading.
 *   - link quality: 0..255, higher is better (the controller's own measure of how many packets get through);
 *   - signal strength: a signed number of dB, NOT dBm. It is relative to the receiver's ideal range: 0 means the signal is inside it, a negative
 *     number says how far below the range it is, a positive one how far above.
 * The first reading after a connection is all zeros (the call only then asks the controller; the next one, a second or more later, has the answers), so
 * a reading of both zeros is "nothing yet", never a measurement.
 * The two are combined by taking the worse: a signal below the range and a link that loses packets both end in dropouts.
 */
#ifndef BTSIGNAL_CORE_H
#define BTSIGNAL_CORE_H

#include <stdint.h>

#include "../include/bluetoothturbo.h"

#define BTSIG_PERIOD_MS    3000u   /* one reading this often while a link is up                                                                           */
#define BTSIG_FAST_MS      1500u   /* until the first real reading: the call's own refresh is once a second, so ask again soon (at most BTSIG_FAST_TRIES)  */
#define BTSIG_FAST_TRIES   6u
#define BTSIG_FRAC         4       /* the averages keep this many fractional bits                                                                         */
#define BTSIG_SHIFT        2       /* each reading moves the average by a quarter of the difference (about 10 s to follow a change at a 3 s period)         */

/* The thresholds. Signal strength in dB below the ideal range (the reading is 0 or negative; positive is above the range and counts as inside it): the
 * receiver's usable range ends about 20 dB under the range's lower edge. Link quality out of 255. First numbers, to be tuned from the readings the module logs. */
#define BTSIG_RSSI_EXCELLENT   (-3)
#define BTSIG_RSSI_GOOD        (-10)
#define BTSIG_RSSI_FAIR        (-20)
#define BTSIG_LQ_EXCELLENT     240u
#define BTSIG_LQ_GOOD          200u
#define BTSIG_LQ_FAIR          128u

typedef struct BtSignal {
	int linked;                 /* the state below is for the device with this address (a link that is up)                    */
	uint32_t lo, hi;
	int have;                   /* at least one real reading since the link came up                                            */
	int32_t lq16, rssi16;       /* the averages, BTSIG_FRAC fractional bits                                                     */
	int due_now;                /* the first reading of a link is asked at once                                                  */
	uint64_t next_ms, asked_ms;
	uint32_t fast;              /* readings asked at the fast pace since the link came up                                        */
	uint32_t n_read, n_zero;    /* readings taken and readings that were all zeros, since the link came up                       */
} BtSignal;

static inline void btsig_reset(BtSignal *s)
{
	*s = (BtSignal){ 0 };
}

/* The level of a smoothed pair: BTT_LINK_SIGNAL_*, the worse of the two. */
static inline unsigned btsig_level_rssi(int rssi)
{
	return rssi >= BTSIG_RSSI_EXCELLENT ? BTT_LINK_SIGNAL_EXCELLENT : rssi >= BTSIG_RSSI_GOOD ? BTT_LINK_SIGNAL_GOOD :
	       rssi >= BTSIG_RSSI_FAIR ? BTT_LINK_SIGNAL_FAIR : BTT_LINK_SIGNAL_WEAK;
}

static inline unsigned btsig_level_lq(unsigned lq)
{
	return lq >= BTSIG_LQ_EXCELLENT ? BTT_LINK_SIGNAL_EXCELLENT : lq >= BTSIG_LQ_GOOD ? BTT_LINK_SIGNAL_GOOD :
	       lq >= BTSIG_LQ_FAIR ? BTT_LINK_SIGNAL_FAIR : BTT_LINK_SIGNAL_WEAK;
}

static inline unsigned btsig_level(unsigned lq, int rssi)
{
	unsigned a = btsig_level_rssi(rssi), b = btsig_level_lq(lq);

	return a < b ? a : b;
}

static inline const char *btsig_name(unsigned level)
{
	return level == BTT_LINK_SIGNAL_EXCELLENT ? "excellent" : level == BTT_LINK_SIGNAL_GOOD ? "good" : level == BTT_LINK_SIGNAL_FAIR ? "fair" :
	       level == BTT_LINK_SIGNAL_WEAK ? "weak" : "measuring";
}

/* One pass of the worker: 1 when a reading is to be taken now (the caller makes it, then calls btsig_feed, or nothing when the call failed). `linked` is
 * the A2DP link being up with this address. A link that goes, or another device, starts the average afresh. */
static inline int btsig_want(BtSignal *s, int linked, uint32_t lo, uint32_t hi, uint64_t now_ms)
{
	if (!linked) {
		if (s->linked)
			btsig_reset(s);
		return 0;
	}
	if (!s->linked || s->lo != lo || s->hi != hi) {
		btsig_reset(s);
		s->linked = 1;
		s->lo = lo;
		s->hi = hi;
		s->due_now = 1;
	}
	if (!s->due_now && now_ms < s->next_ms)
		return 0;
	s->due_now = 0;
	s->asked_ms = now_ms;
	if (!s->have && s->fast < BTSIG_FAST_TRIES) {
		s->fast++;
		s->next_ms = now_ms + BTSIG_FAST_MS;
	} else {
		s->next_ms = now_ms + BTSIG_PERIOD_MS;
	}
	return 1;
}

/* A reading the call gave (link quality 0..255, signal strength as a signed number): all zeros is nothing yet; else it joins the average (the first one starts it). */
static inline void btsig_feed(BtSignal *s, unsigned lq, int rssi)
{
	int32_t l = (int32_t)(lq > 255u ? 255u : lq) << BTSIG_FRAC, r = (int32_t)rssi << BTSIG_FRAC;

	s->n_read++;
	if (lq == 0 && rssi == 0) {
		s->n_zero++;
		return;
	}
	if (!s->have) {
		s->have = 1;
		s->lq16 = l;
		s->rssi16 = r;
		s->next_ms = s->asked_ms + BTSIG_PERIOD_MS;   /* the fast spacing was for the first real reading; this is it */
		return;
	}
	s->lq16 += (l - s->lq16) / (1 << BTSIG_SHIFT);       /* C division rounds towards zero: the average never overshoots the reading */
	s->rssi16 += (r - s->rssi16) / (1 << BTSIG_SHIFT);
}

static inline int32_t btsig_round(int32_t v16)
{
	int32_t h = 1 << (BTSIG_FRAC - 1);

	return v16 >= 0 ? (v16 + h) >> BTSIG_FRAC : -((-v16 + h) >> BTSIG_FRAC);
}

/* The smoothed readings and the word, for the link information: all 0 while nothing has been measured. */
static inline void btsig_out(const BtSignal *s, unsigned *lq, int *rssi, unsigned *level)
{
	*lq = 0;
	*rssi = 0;
	*level = BTT_LINK_SIGNAL_NONE;
	if (!s->linked || !s->have)
		return;
	*lq = (unsigned)btsig_round(s->lq16);
	*rssi = btsig_round(s->rssi16);
	*level = btsig_level(*lq, *rssi);
}

#endif
