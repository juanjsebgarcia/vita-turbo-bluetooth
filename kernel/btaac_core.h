/*
 * btaac_core.h - the decisions of the AAC engine that need no system call: the bit rate, the persisted crash record and what a
 * start does with it, the self-test's verdict, and the test signals. Header only, plain integer C, so the host test
 * (test/test_btaac.c) compiles exactly what the kernel module runs.
 */
#ifndef BTAAC_CORE_H
#define BTAAC_CORE_H

#include <stdint.h>

#define BTAAC_FRAME          1024u     /* samples per channel in one access unit */
#define BTAAC_MIN_BPS_CH     4000u     /* the encoder's own limits, per channel: below or above it takes its default instead */
#define BTAAC_MAX_BPS_CH     160000u
#define BTAAC_DEFAULT_KBPS   256u

/* ---- the bit rate ---------------------------------------------------------------------------------------- */

/* 1 for a rate and a channel count the engine encodes: the two rates A2DP uses, one or two channels. */
static inline int btaac_format_ok(uint32_t rate, uint32_t channels)
{
	return (rate == 44100u || rate == 48000u) && (channels == 1u || channels == 2u);
}

/* The bit rate (bits a second, all channels) the encoder is given. It silently replaces a rate it does not accept with its
 * own default, so every rate is brought into its range here: at most `max_bps` when that is not 0 (what the headphones accept),
 * at least 4,000 and at most 160,000 per channel (and 6 times the sample rate per channel, which is higher). */
static inline uint32_t btaac_clamp_bps(uint32_t bps, uint32_t rate, uint32_t channels, uint32_t max_bps)
{
	uint32_t per_ch_max = BTAAC_MAX_BPS_CH, lo, hi;

	if (6u * rate < per_ch_max)
		per_ch_max = 6u * rate;
	lo = BTAAC_MIN_BPS_CH * channels;
	hi = per_ch_max * channels;
	if (max_bps && bps > max_bps)
		bps = max_bps;
	if (bps > hi)
		bps = hi;
	if (bps < lo)
		bps = lo;
	return bps;
}

/* ---- the access unit cap -------------------------------------------------------------------------------------------- */

/* An access unit that does not fit one packet of the link is cut into pieces, and a headset can stutter on that. The encoder is therefore held to a size: it spends
 * from a bit reservoir (bits saved on easy frames, spent on hard ones), and a frame is at most the average frame (padded to a whole byte) plus what the reservoir
 * holds, so the reservoir is made no larger than what the packet leaves over (btaac_enc_limit, kernel/btaac_enclimit.c). The average does not change: what the reservoir
 * does not take is padding. Measured cost on a music-like test signal at 256 kb/s: a reservoir of 16 bytes instead of 853 lowers the signal-to-noise ratio by about 0.4 dB. */
#define BTAAC_CAP_MARGIN     4u        /* bytes kept between the bound and the packet's limit */
#define BTAAC_RES_MIN        8u        /* the smallest reservoir, bytes: a bit rate whose average frame leaves less room than this is brought down */

/* The longest frame the encoder writes at `bps` and `rate` without the reservoir: the average, padded up to a whole byte (its own arithmetic: bytes = 128 * bps / rate, plus
 * one on the frames that carry the padding). */
static inline uint32_t btaac_frame_bytes(uint32_t bps, uint32_t rate)
{
	return (bps / rate) * 128u + ((bps % rate) * 128u) / rate + 1u;
}

/* The highest bit rate whose frame, the margin and the smallest reservoir fit `cap` bytes (btaac_au_cap): 0 if none does. */
static inline uint32_t btaac_cap_max_bps(uint32_t cap, uint32_t rate)
{
	uint32_t keep = BTAAC_CAP_MARGIN + BTAAC_RES_MIN + 1u;   /* + the padding byte of btaac_frame_bytes */

	if (cap <= keep)
		return 0;
	return ((cap - keep + 1u) * rate - 1u) / 128u;           /* the largest bps with 128 * bps / rate <= cap - keep */
}

/* The reservoir to give the encoder, bytes, for a session at `bps` held to `cap`: what the cap leaves after the frame and the margin. 0 if it leaves none. */
static inline uint32_t btaac_cap_reservoir(uint32_t cap, uint32_t bps, uint32_t rate)
{
	uint32_t used = btaac_frame_bytes(bps, rate) + BTAAC_CAP_MARGIN;

	return cap > used ? cap - used : 0u;
}

/* ---- the engine thread's priority ----------------------------------------------------------------------------------------------- */

/* A lower number is a higher priority. The audio sender's thread (SceAVConfigDataSend, which runs the Bluetooth hand-off hooks) and the Bluetooth code's own threads are
 * at 0x12 (read from the thread manager at run time; the firmware gives them that priority). A session runs the engine's thread just below the sender's, so that a system sound or a
 * window opening on the system core cannot hold it off for a tenth of a second (the PCM ring then overflows and audio is lost); with the sender's priority not
 * readable, or not a plausible one, at the module's own priority (0x70). */
#define BTAAC_PRIO_FALLBACK       0x70u   /* the engine thread's priority when the sender's is unknown (and when no session runs) */
#define BTAAC_PRIO_SENDER_MIN     0x10u   /* a sender priority outside [min, fallback) is not believed */
#define BTAAC_PRIO_OVERRIDE_MIN   0x10u   /* the lowest priority a set value may take */
#define BTAAC_PRIO_OVERRIDE_MAX   0xBFu
#define BTAAC_SENDER_THREAD       "SceAVConfigDataSend"
#define BTAAC_BT_THREAD_PREFIX    "SceBt"

enum { BTAAC_PRIO_SRC_COMPUTED = 0, BTAAC_PRIO_SRC_OVERRIDE, BTAAC_PRIO_SRC_FALLBACK };

/* The priority a session's thread gets: the override when one is set (0 = none), else one below the sender's, else the fallback.
 * `*src` says which. */
static inline uint32_t btaac_prio_choose(uint32_t override_prio, int sender_prio, uint32_t *src)
{
	if (override_prio >= BTAAC_PRIO_OVERRIDE_MIN && override_prio <= BTAAC_PRIO_OVERRIDE_MAX) {
		*src = BTAAC_PRIO_SRC_OVERRIDE;
		return override_prio;
	}
	if (sender_prio >= (int)BTAAC_PRIO_SENDER_MIN && sender_prio < (int)BTAAC_PRIO_FALLBACK) {
		*src = BTAAC_PRIO_SRC_COMPUTED;
		return (uint32_t)sender_prio + 1u;
	}
	*src = BTAAC_PRIO_SRC_FALLBACK;
	return BTAAC_PRIO_FALLBACK;
}

/* One thread of the thread manager's list (its name and current priority) looked at: the sender's priority (`*sender`, -1 until it is seen) and the lowest priority (highest
 * number) among the Bluetooth code's own threads (`*bt`, -1 until one is seen), which the readings show next to it. Plain loops: the host test runs it. */
static inline int btaac_name_is(const char *name, const char *want)
{
	while (*want)
		if (*name++ != *want++)
			return 0;
	return *name == 0;
}
static inline int btaac_name_starts(const char *name, const char *prefix)
{
	while (*prefix)
		if (*name++ != *prefix++)
			return 0;
	return 1;
}
static inline void btaac_prio_note(const char *name, int prio, int *sender, int *bt)
{
	if (btaac_name_is(name, BTAAC_SENDER_THREAD))
		*sender = prio;
	else if (btaac_name_starts(name, BTAAC_BT_THREAD_PREFIX) && prio > *bt)
		*bt = prio;
}

/* ---- the persisted record: did the last AAC session, or the last self-test, end cleanly --------------------- */

/* Eight bytes in ur0:data/bluetoothturbo/aac.guard, overwritten in place (never truncated: a crash between a truncate and the
 * write would leave an empty file): "BAG1", the session byte, the self-test byte, the crash count as a digit, a newline.
 *   session: 'K' none running, 'S' a session with the plain C encoder is running, 'Z' one with the NEON build is
 *   test:    'K' nothing wrong, 'c' the C self-test is running, 'n' the NEON self-test (or a timing run) is running,
 *            'A' the C encoder crashed in a test: AAC is off for good, 'N' the NEON build crashed: it is off for good
 *   crashes: the sessions in a row that did not end cleanly (reset by a session that does)
 * A start that finds a session byte other than 'K' knows the console went down while a session ran. 'A' and 'N' stay until the
 * file is deleted: a build that took the console down once must not be run again by itself. */
#define BTAAC_G_BYTES        8
#define BTAAC_CRASH_LIMIT    3          /* sessions in a row that did not end cleanly: AAC stays off from the third */
#define BTAAC_CRASH_MAX      9          /* the digit's top */

typedef struct BtAacGuard {
	char session, test;
	uint8_t crashes;
} BtAacGuard;

typedef struct BtAacBoot {
	uint8_t unclean;       /* the last session did not end cleanly */
	uint8_t hold;          /* no AAC session this start: unclean, three in a row, or the C encoder banned */
	uint8_t neon_banned;   /* the NEON build is not to be run */
	uint8_t aac_banned;    /* the C encoder is not to be run either */
} BtAacBoot;

static inline void btaac_guard_default(BtAacGuard *g)
{
	g->session = 'K';
	g->test = 'K';
	g->crashes = 0;
}

/* Reads a record; returns 1 if it was one, else sets the defaults and returns 0 (a missing or damaged file starts clean). */
static inline int btaac_guard_decode(BtAacGuard *g, const uint8_t *b, int n)
{
	btaac_guard_default(g);
	if (n < BTAAC_G_BYTES || b[0] != 'B' || b[1] != 'A' || b[2] != 'G' || b[3] != '1')
		return 0;
	if ((b[4] != 'K' && b[4] != 'S' && b[4] != 'Z') ||
	    (b[5] != 'K' && b[5] != 'c' && b[5] != 'n' && b[5] != 'A' && b[5] != 'N') ||
	    b[6] < '0' || b[6] > '0' + BTAAC_CRASH_MAX)
		return 0;
	g->session = (char)b[4];
	g->test = (char)b[5];
	g->crashes = (uint8_t)(b[6] - '0');
	return 1;
}

static inline void btaac_guard_encode(const BtAacGuard *g, uint8_t *b)
{
	b[0] = 'B'; b[1] = 'A'; b[2] = 'G'; b[3] = '1';
	b[4] = (uint8_t)g->session;
	b[5] = (uint8_t)g->test;
	b[6] = (uint8_t)('0' + (g->crashes > BTAAC_CRASH_MAX ? BTAAC_CRASH_MAX : g->crashes));
	b[7] = '\n';
}

/* What a start does with the record it found: turns what was left running into what happened (the record is then to be written
 * back as it stands) and says what this start may do. */
static inline BtAacBoot btaac_guard_boot(BtAacGuard *g)
{
	BtAacBoot b = { 0, 0, 0, 0 };

	if (g->test == 'c')
		g->test = 'A';                       /* the C encoder took the console down in its own test */
	else if (g->test == 'n')
		g->test = 'N';                       /* so did the NEON build */
	if (g->session == 'S' || g->session == 'Z') {
		b.unclean = 1;
		if (g->crashes < BTAAC_CRASH_MAX)
			g->crashes++;
		if (g->session == 'Z' && g->test == 'K')
			g->test = 'N';                   /* a session on the NEON build: it is the one to stop using */
		g->session = 'K';
	}
	b.aac_banned = g->test == 'A';
	b.neon_banned = g->test == 'N' || b.aac_banned;
	b.hold = b.unclean || g->crashes >= BTAAC_CRASH_LIMIT || b.aac_banned;
	return b;
}

static inline void btaac_guard_session_begin(BtAacGuard *g, int neon)
{
	g->session = neon ? 'Z' : 'S';
}

/* A session that ended cleanly (the stream stopped, the module stopped): the count of those that did not starts again. */
static inline void btaac_guard_session_end(BtAacGuard *g)
{
	g->session = 'K';
	g->crashes = 0;
}

/* ---- the self-test's verdict ----------------------------------------------------------------------------- */

#define BTAAC_ST_FRAMES      16u        /* frames of the self-test signal */

enum BtAacSelfTest {
	BTAAC_ST_NOT_RUN = 0,   /* not run yet this start                                                                 */
	BTAAC_ST_PASS,          /* both builds encoded the signal, every access unit and the whole stream the same        */
	BTAAC_ST_NEON_DIFF,     /* the C build is fine; the NEON build gave other bytes, or could not be run: only C may be used */
	BTAAC_ST_NEON_BANNED,   /* the C build is fine; the NEON build took the console down before: not run again        */
	BTAAC_ST_C_FAIL,        /* the C build did not give a clean encode: no AAC                                        */
	BTAAC_ST_ERROR          /* the test could not run (no memory, no thread): no AAC until it has                     */
};

/* What one build gave on the signal. */
typedef struct BtAacRun {
	uint8_t  ok;            /* every call succeeded, no consistency check of the encoder failed, every access unit fitted */
	uint32_t aus;           /* access units out                                                                       */
	uint32_t bytes;         /* bytes out                                                                              */
	uint64_t hash;          /* FNV-1a 64 over every output byte, in order                                              */
	uint16_t len[BTAAC_ST_FRAMES];   /* the length of each access unit                                                 */
} BtAacRun;

#define BTAAC_FNV_BASIS 0xCBF29CE484222325ull
#define BTAAC_FNV_PRIME 0x100000001B3ull

static inline uint64_t btaac_fnv(uint64_t h, const uint8_t *p, uint32_t n)
{
	while (n--) {
		h ^= *p++;
		h *= BTAAC_FNV_PRIME;
	}
	return h;
}

static inline int btaac_run_same(const BtAacRun *a, const BtAacRun *b)
{
	if (a->aus != b->aus || a->bytes != b->bytes || a->hash != b->hash)
		return 0;
	for (uint32_t i = 0; i < a->aus && i < BTAAC_ST_FRAMES; i++)
		if (a->len[i] != b->len[i])
			return 0;
	return 1;
}

/* The verdict. `c` is the C build's run (NULL: no encoder could be made of it), `n` the NEON build's (NULL: none could, or it was
 * not run: `neon_banned` says it was banned). NEON is allowed only on BTAAC_ST_PASS. */
static inline int btaac_selftest_decide(const BtAacBoot *boot, const BtAacRun *c, const BtAacRun *n)
{
	if (boot->aac_banned)
		return BTAAC_ST_C_FAIL;
	if (!c)
		return BTAAC_ST_ERROR;
	if (!c->ok || c->aus != BTAAC_ST_FRAMES)
		return BTAAC_ST_C_FAIL;
	if (boot->neon_banned)
		return BTAAC_ST_NEON_BANNED;
	if (!n)
		return BTAAC_ST_NEON_DIFF;           /* no encoder could be made of it: the same for the caller */
	if (!n->ok || !btaac_run_same(c, n))
		return BTAAC_ST_NEON_DIFF;
	return BTAAC_ST_PASS;
}

static inline int btaac_neon_allowed(int verdict) { return verdict == BTAAC_ST_PASS; }
/* The plain C build may be used once it has passed its own test. */
static inline int btaac_c_allowed(int verdict)
{
	return verdict == BTAAC_ST_PASS || verdict == BTAAC_ST_NEON_DIFF || verdict == BTAAC_ST_NEON_BANNED;
}

/* ---- the test signals: integer only, the same samples everywhere -------------------------------------------- */

static inline uint32_t btaac_hash32(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

/* White noise of about plus and minus a quarter of full scale: the hardest signal for the encoder (the slowest in the console
 * timing runs on the console). Frame `n`. */
static inline void btaac_sig_noise(uint32_t n, int16_t *l, int16_t *r)
{
	*l = (int16_t)((int16_t)(btaac_hash32(n * 2u) >> 16) >> 2);
	*r = (int16_t)((int16_t)(btaac_hash32(n * 2u + 1u) >> 16) >> 2);
}

/* The self-test's signal: noise on the left; a triangle wave (period 48 samples, 1 kHz) with a little noise on the right; a
 * loud click, then silence, in the middle of the third frame (the encoder switches to short blocks there). Frame `n`. */
static inline void btaac_sig_selftest(uint32_t n, int16_t *l, int16_t *r)
{
	int32_t ph = (int32_t)(n % 48u), tri = ph < 24 ? ph : 48 - ph;
	int32_t right = (tri - 12) * 1200 + ((int16_t)(btaac_hash32(n * 2u + 1u) >> 16) >> 6);
	int32_t left = (int16_t)(btaac_hash32(n * 2u) >> 16) >> 2;

	if (n >= 2560u && n < 2624u)
		left = right = (n & 1u) ? 20000 : -20000;
	else if (n >= 2624u && n < 3072u)
		left = right = 0;
	*l = (int16_t)left;
	*r = (int16_t)right;
}

#endif /* BTAAC_CORE_H */
