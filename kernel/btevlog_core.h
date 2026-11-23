/*
 * btevlog_core.h - the link log's logic, with no system call, so the host test compiles it as it is (kernel/btevlog.c is its worker side,
 * kernel/btevlog_hook.c the one routine that runs inside the Bluetooth code).
 *
 * What it keeps, per Bluetooth link: who opened it, the profiles in the order they opened, the authentication and encryption results, and
 * why and when it ended; plus the rules that keep a reconnect loop from filling the log.
 *
 * The records come from the Bluetooth code's event handler (a leaf stores them in a ring: BtelRing) and are applied by the worker, never by the
 * hook. Times in a record are the 32-bit microsecond clock; btel_wide() widens one against the worker's own clock (valid while the record is
 * less than 71 minutes old: the worker drains twice a second).
 */
#ifndef BTEVLOG_CORE_H
#define BTEVLOG_CORE_H

#include <stdint.h>
#include <string.h>

/* ---- the ring the hook writes ---------------------------------------------------------------------------------------------- */

#define BTEL_RING_N  64u              /* a power of two: a link's whole start-up is under 20 events */

enum {
	BTEL_K_NONE = 0,
	BTEL_K_CONN_REQ,                  /* Connection Request (the remote asks to connect): w1,w2 address, w3 link type                             */
	BTEL_K_CONN_CMP,                  /* Connection Complete: w1,w2 address, w3 = status | handle << 8 | link type << 24                           */
	BTEL_K_DISC,                      /* Disconnection Complete: w1 = handle | status << 16, w2 = reason                                           */
	BTEL_K_AUTH,                      /* Authentication Complete: w1 = handle | status << 16                                                       */
	BTEL_K_ENC,                       /* Encryption Change: w1 = handle | status << 16, w2 = encryption enabled                                    */
	BTEL_K_ROLE                       /* Role Change: w1,w2 address, w3 = status | role << 8 (0 central, 1 peripheral)                             */
};

typedef struct BtelRec {
	volatile uint32_t seq;            /* the record's number + 1; 0 = empty or being written */
	volatile uint32_t t;              /* the 32-bit microsecond clock                          */
	volatile uint32_t kind, w1, w2, w3;
} BtelRec;

typedef struct BtelRing {
	volatile uint32_t head;           /* records claimed so far */
	BtelRec e[BTEL_RING_N];
} BtelRing;

/* A handful of stores and one atomic add: no call, no lock, no wait. The writer claims a slot, fills it, and publishes its number last. */
static inline void btel_put(BtelRing *r, uint32_t t, uint32_t kind, uint32_t w1, uint32_t w2, uint32_t w3)
{
	uint32_t n = __atomic_fetch_add(&r->head, 1u, __ATOMIC_ACQ_REL);
	BtelRec *e = &r->e[n & (BTEL_RING_N - 1u)];

	__atomic_store_n(&e->seq, 0u, __ATOMIC_RELAXED);
	__atomic_thread_fence(__ATOMIC_RELEASE);   /* the zero is seen before any field changes */
	e->t = t;
	e->kind = kind;
	e->w1 = w1;
	e->w2 = w2;
	e->w3 = w3;
	__atomic_store_n(&e->seq, n + 1u, __ATOMIC_RELEASE);
}

/* The next record at *tail, copied to *out (1), or 0 if there is none, or the writer has not published it yet (asked again next pass). Records that were overwritten
 * before they were read are skipped and counted in *lost. */
static inline int btel_take(BtelRing *r, uint32_t *tail, BtelRec *out, uint32_t *lost)
{
	for (;;) {
		uint32_t head = __atomic_load_n(&r->head, __ATOMIC_ACQUIRE), s0, s1;
		const BtelRec *e;

		if (*tail == head)
			return 0;
		if (head - *tail > BTEL_RING_N) {
			*lost += head - *tail - BTEL_RING_N;
			*tail = head - BTEL_RING_N;
		}
		e = &r->e[*tail & (BTEL_RING_N - 1u)];
		s0 = __atomic_load_n(&e->seq, __ATOMIC_ACQUIRE);
		if (s0 == *tail + 1u) {
			out->t = e->t;
			out->kind = e->kind;
			out->w1 = e->w1;
			out->w2 = e->w2;
			out->w3 = e->w3;
			__atomic_thread_fence(__ATOMIC_ACQUIRE);   /* the fields were read before the number is read again */
			s1 = __atomic_load_n(&e->seq, __ATOMIC_RELAXED);
			if (s1 == s0) {
				out->seq = s0;
				(*tail)++;
				return 1;
			}
		} else if (s0 == 0u || s0 < *tail + 1u) {
			return 0;                 /* claimed but not published yet */
		}
		(*lost)++;                    /* overwritten while it was read */
		(*tail)++;
	}
}

/* ---- names ----------------------------------------------------------------------------------------------------------------- */

/* The standard error codes the controller reports as a status or a disconnect reason, in plain words ("" for a code with no name here: the caller prints the number). */
static inline const char *btel_reason_name(uint32_t c)
{
	switch (c) {
	case 0x00: return "success";
	case 0x01: return "unknown command";
	case 0x02: return "unknown connection";
	case 0x03: return "hardware failure";
	case 0x04: return "page timeout";
	case 0x05: return "authentication failure";
	case 0x06: return "PIN or key missing";
	case 0x07: return "memory capacity exceeded";
	case 0x08: return "connection timeout";
	case 0x09: return "connection limit exceeded";
	case 0x0A: return "voice link limit exceeded";
	case 0x0B: return "connection already exists";
	case 0x0C: return "command not allowed";
	case 0x0D: return "rejected, limited resources";
	case 0x0E: return "rejected, security";
	case 0x0F: return "rejected, unacceptable address";
	case 0x10: return "connection accept timeout";
	case 0x11: return "unsupported feature or parameter";
	case 0x12: return "invalid parameters";
	case 0x13: return "remote user terminated";
	case 0x14: return "remote terminated, low resources";
	case 0x15: return "remote terminated, power off";
	case 0x16: return "local host terminated";
	case 0x17: return "repeated attempts";
	case 0x18: return "pairing not allowed";
	case 0x1A: return "unsupported remote feature";
	case 0x1F: return "unspecified error";
	case 0x22: return "LMP response timeout";
	case 0x23: return "LMP transaction collision";
	case 0x24: return "LMP PDU not allowed";
	case 0x25: return "encryption mode not acceptable";
	case 0x26: return "link key cannot be changed";
	case 0x28: return "instant passed";
	case 0x2A: return "transaction collision";
	case 0x2F: return "insufficient security";
	case 0x30: return "parameter out of range";
	case 0x35: return "role switch failed";
	case 0x37: return "simple pairing not supported by host";
	case 0x38: return "host busy, pairing";
	case 0x3A: return "controller busy";
	case 0x3B: return "unacceptable connection parameters";
	case 0x3D: return "connection terminated due to MIC failure";
	case 0x3E: return "connection failed to be established";
	}
	return "";
}

/* ---- profiles -------------------------------------------------------------------------------------------------------------- */

enum {
	BTEL_P_NONE = 0, BTEL_P_SDP, BTEL_P_HSP, BTEL_P_HFP, BTEL_P_RFCOMM, BTEL_P_A2DP_SIG, BTEL_P_A2DP_MEDIA, BTEL_P_AVRCP, BTEL_P_AVRCP_BROWSE,
	BTEL_P_HID, BTEL_P_OTHER, BTEL_P_N
};

static inline const char *btel_prof_name(unsigned tok)
{
	static const char *const n[BTEL_P_N] = { "?", "sdp", "hsp", "hfp", "rfcomm", "a2dp-sig", "a2dp-media", "avrcp", "avrcp-browse", "hid", "other" };

	return tok < BTEL_P_N ? n[tok] : "?";
}

#define BTEL_PROF_MAX   12            /* profiles kept per link (a start-up has 5 to 7)           */
#define BTEL_PF_DIR     0x80u         /* in a profile byte: the channel object says the remote asked for it (a flag of the object; shown in the development readings only) */
#define BTEL_PF_TOK(p)  ((p) & 0x7Fu)

/* ---- the links ------------------------------------------------------------------------------------------------------------- */

#define BTEL_ACTIVE  8
#define BTEL_HIST    16
#define BTEL_PEND    4
#define BTEL_CH      16               /* channel objects in a link slot of the Bluetooth code */
#define BTEL_PEND_US 30000000u        /* an incoming request that never completed is forgotten after 30 s */

enum { BTEL_OPEN_UNKNOWN = 0, BTEL_OPEN_VITA = 1, BTEL_OPEN_HEADSET = 2 };
enum { BTEL_E_ENDED = 0, BTEL_E_FAILED = 1, BTEL_E_UNKNOWN = 2 };   /* a link that ended; a connection that never came up; a handle the log did not see open */

typedef struct BtelEnd {              /* a link, as it was when it ended (kept for the development readings' history) */
	uint32_t id, lo, hi;
	uint32_t handle, kind, reason, status, opener, role;
	uint32_t dur_ds;                  /* tenths of a second */
	uint32_t ended_s;                 /* seconds since the worker started when it ended */
	uint8_t auth_ok, auth_bad, enc_on, enc_off;
	uint8_t nprof, prof[BTEL_PROF_MAX];
} BtelEnd;

typedef struct BtelLink {
	uint8_t used;
	uint32_t id, handle, lo, hi, opener, role;
	uint64_t t_open;                  /* the worker's clock (us) when the controller reported the link up */
	uint8_t auth_ok, auth_bad, enc_on, enc_off;
	uint8_t nprof, prof[BTEL_PROF_MAX];
	uint16_t ch_psm[BTEL_CH];         /* the PSM noted for each channel object of the link's slot (0 = none noted); the audio PSM's first is the signalling, the second the media */
} BtelLink;

typedef struct BtelPend { uint8_t used; uint32_t lo, hi; uint64_t t; } BtelPend;

typedef struct BtelState {
	BtelLink link[BTEL_ACTIVE];
	BtelPend pend[BTEL_PEND];
	BtelEnd hist[BTEL_HIST];
	uint32_t hist_n;                  /* links entered in the history so far (the newest is hist[(hist_n - 1) % BTEL_HIST]) */
	uint32_t next_id;                 /* links numbered since the start */
	uint32_t lost;                    /* records that were overwritten before the worker read them */
} BtelState;

/* What applying one record produces for the log. */
enum { BTEL_O_NONE = 0, BTEL_O_OPEN, BTEL_O_END, BTEL_O_FAIL, BTEL_O_AUTH_BAD, BTEL_O_ENC_OFF };
typedef struct BtelOut { int kind; BtelEnd e; } BtelOut;

/* The 32-bit microsecond clock t, widened against now (the worker's wide clock) and the low word read with it. */
static inline uint64_t btel_wide(uint64_t now_wide, uint32_t now_low, uint32_t t)
{
	return now_wide - (uint32_t)(now_low - t);
}

static inline void btel_init(BtelState *s)
{
	memset(s, 0, sizeof(*s));
}

static inline BtelLink *btel_by_handle(BtelState *s, uint32_t h)
{
	for (int i = 0; i < BTEL_ACTIVE; i++)
		if (s->link[i].used && s->link[i].handle == h)
			return &s->link[i];
	return 0;
}

static inline BtelLink *btel_by_addr(BtelState *s, uint32_t lo, uint32_t hi)
{
	for (int i = 0; i < BTEL_ACTIVE; i++)
		if (s->link[i].used && s->link[i].lo == lo && s->link[i].hi == (hi & 0xFFFFu))
			return &s->link[i];
	return 0;
}

static inline void btel_fill_end(const BtelLink *l, BtelEnd *e)
{
	memset(e, 0, sizeof(*e));
	e->id = l->id;
	e->lo = l->lo;
	e->hi = l->hi;
	e->handle = l->handle;
	e->opener = l->opener;
	e->role = l->role;
	e->auth_ok = l->auth_ok;
	e->auth_bad = l->auth_bad;
	e->enc_on = l->enc_on;
	e->enc_off = l->enc_off;
	e->nprof = l->nprof;
	memcpy(e->prof, l->prof, sizeof(e->prof));
}

static inline void btel_hist_add(BtelState *s, const BtelEnd *e)
{
	s->hist[s->hist_n % BTEL_HIST] = *e;
	s->hist_n++;
}

/* The profile `tok` (with its direction bit) noted on link `l`; once per channel. */
static inline void btel_add_profile(BtelLink *l, unsigned tok, int remote_asked)
{
	if (l->nprof < BTEL_PROF_MAX)
		l->prof[l->nprof++] = (uint8_t)(tok | (remote_asked ? BTEL_PF_DIR : 0u));
}

/* One record applied at time `t` (the worker's wide clock, us; `now_s` the seconds since the worker started). */
static inline void btel_apply(BtelState *s, const BtelRec *r, uint64_t t, uint32_t now_s, BtelOut *o)
{
	BtelLink *l;
	uint32_t status, handle;

	o->kind = BTEL_O_NONE;
	switch (r->kind) {
	case BTEL_K_CONN_REQ: {
		BtelPend *p = 0;

		if ((r->w3 & 0xFFu) != 1u)
			return;                   /* a voice link's request (link type 0 or 2) says nothing about who opens the ACL link */

		for (int i = 0; i < BTEL_PEND; i++) {
			if (s->pend[i].used && (t - s->pend[i].t > BTEL_PEND_US))
				s->pend[i].used = 0;
			if (s->pend[i].used && s->pend[i].lo == r->w1 && s->pend[i].hi == (r->w2 & 0xFFFFu))
				p = &s->pend[i];
		}
		for (int i = 0; !p && i < BTEL_PEND; i++)
			if (!s->pend[i].used)
				p = &s->pend[i];
		if (!p)
			p = &s->pend[0];          /* all busy: the oldest slot is as good as any */
		p->used = 1;
		p->lo = r->w1;
		p->hi = r->w2 & 0xFFFFu;
		p->t = t;
		return;
	}
	case BTEL_K_CONN_CMP: {
		uint32_t opener = BTEL_OPEN_VITA;
		BtelPend *p = 0;

		status = r->w3 & 0xFFu;
		handle = (r->w3 >> 8) & 0xFFFu;
		for (int i = 0; i < BTEL_PEND; i++)
			if (s->pend[i].used && s->pend[i].lo == r->w1 && s->pend[i].hi == (r->w2 & 0xFFFFu) && t - s->pend[i].t <= BTEL_PEND_US)
				p = &s->pend[i];
		if (p) {
			opener = BTEL_OPEN_HEADSET;   /* the controller only reports a request for a connection the remote began */
			p->used = 0;
		}
		if (status) {
			BtelLink tmp;

			memset(&tmp, 0, sizeof(tmp));
			tmp.id = ++s->next_id;
			tmp.lo = r->w1;
			tmp.hi = r->w2 & 0xFFFFu;
			tmp.opener = opener;
			btel_fill_end(&tmp, &o->e);
			o->e.kind = BTEL_E_FAILED;
			o->e.status = status;
			o->e.reason = status;
			o->e.ended_s = now_s;
			btel_hist_add(s, &o->e);
			o->kind = BTEL_O_FAIL;
			return;
		}
		l = btel_by_handle(s, handle);    /* a handle reused before the end was seen: the old entry goes */
		for (int i = 0; !l && i < BTEL_ACTIVE; i++)
			if (!s->link[i].used)
				l = &s->link[i];
		if (!l) {                         /* table full: the oldest link is dropped */
			l = &s->link[0];
			for (int i = 1; i < BTEL_ACTIVE; i++)
				if (s->link[i].t_open < l->t_open)
					l = &s->link[i];
		}
		memset(l, 0, sizeof(*l));
		l->used = 1;
		l->id = ++s->next_id;
		l->handle = handle;
		l->lo = r->w1;
		l->hi = r->w2 & 0xFFFFu;
		l->opener = opener;
		l->t_open = t;
		btel_fill_end(l, &o->e);
		o->kind = BTEL_O_OPEN;
		return;
	}
	case BTEL_K_DISC: {
		uint32_t reason = r->w2 & 0xFFu;

		handle = r->w1 & 0xFFFu;
		status = (r->w1 >> 16) & 0xFFu;
		if (status)
			return;                       /* the disconnect command failed: the link is still up */
		l = btel_by_handle(s, handle);
		if (l) {
			btel_fill_end(l, &o->e);
			o->e.kind = BTEL_E_ENDED;
			o->e.dur_ds = (uint32_t)((t - l->t_open) / 100000u);
			l->used = 0;
		} else {
			memset(&o->e, 0, sizeof(o->e));
			o->e.id = ++s->next_id;
			o->e.handle = handle;
			o->e.kind = BTEL_E_UNKNOWN;
		}
		o->e.reason = reason;
		o->e.ended_s = now_s;
		btel_hist_add(s, &o->e);
		o->kind = BTEL_O_END;
		return;
	}
	case BTEL_K_AUTH:
		handle = r->w1 & 0xFFFu;
		status = (r->w1 >> 16) & 0xFFu;
		l = btel_by_handle(s, handle);
		if (!l)
			return;
		if (status) {
			if (l->auth_bad < 255)
				l->auth_bad++;
			btel_fill_end(l, &o->e);
			o->e.status = status;
			o->kind = BTEL_O_AUTH_BAD;
		} else if (l->auth_ok < 255) {
			l->auth_ok++;
		}
		return;
	case BTEL_K_ENC:
		handle = r->w1 & 0xFFFu;
		status = (r->w1 >> 16) & 0xFFu;
		l = btel_by_handle(s, handle);
		if (!l)
			return;
		if (!status && r->w2) {
			if (l->enc_on < 255)
				l->enc_on++;
		} else {
			if (l->enc_off < 255)
				l->enc_off++;
			btel_fill_end(l, &o->e);
			o->e.status = status;
			o->kind = BTEL_O_ENC_OFF;
		}
		return;
	case BTEL_K_ROLE:
		status = r->w3 & 0xFFu;
		l = btel_by_addr(s, r->w1, r->w2);
		if (l && !status)
			l->role = ((r->w3 >> 8) & 0xFFu) + 1u;   /* 1 central, 2 peripheral; 0 = not seen */
		return;
	}
}

/* ---- the rules that keep a reconnect loop out of the log ------------------------------------------------------------------- */

#define BTEL_WIN_S        60u         /* the rolling window of the line cap                                   */
#define BTEL_WIN_MAX      8u          /* lines in one window; the rest are counted and summed up once          */
#define BTEL_BOOT_MAX     120u        /* lines in all, per start (about 18 KB of the log's 64 KB)               */
#define BTEL_UNKNOWN_MAX  3u          /* endings of a handle the log never saw that are written, per start      */
#define BTEL_COLLAPSE_S   120u        /* an ending the same as the last one within this is folded into a count  */

typedef struct BtelLim {
	uint32_t win_start_s, in_win, held, total;
	int capped;                       /* the per-start limit was reached and said so                           */
	/* the folded repeats: the key of the last ending that was logged */
	int have;
	uint32_t key_lo, key_hi, key_reason, key_opener, key_s;
	uint32_t repeats;                 /* endings folded into the last logged one since                         */
	uint32_t rep_id, rep_dur_ds;      /* the newest folded one                                                 */
	/* the folded group a summary line is about: copied out when the group ends, before the key moves on */
	uint32_t sum_lo, sum_hi, sum_reason, sum_opener, sum_id, sum_dur_ds;
} BtelLim;

static inline void btel_lim_sum_copy(BtelLim *m)
{
	m->sum_lo = m->key_lo;
	m->sum_hi = m->key_hi;
	m->sum_reason = m->key_reason;
	m->sum_opener = m->key_opener;
	m->sum_id = m->rep_id;
	m->sum_dur_ds = m->rep_dur_ds;
}

static inline void btel_lim_init(BtelLim *m)
{
	memset(m, 0, sizeof(*m));
}

/* 1 if a line may be written now (and it is counted), 0 if it is held back (counted, to be summed up when the window ends). */
static inline int btel_lim_allow(BtelLim *m, uint32_t now_s)
{
	if (m->total >= BTEL_BOOT_MAX)
		return 0;
	if (now_s - m->win_start_s >= BTEL_WIN_S || !m->win_start_s) {
		m->win_start_s = now_s ? now_s : 1u;
		m->in_win = 0;
	}
	if (m->in_win >= BTEL_WIN_MAX) {
		m->held++;
		return 0;
	}
	m->in_win++;
	m->total++;
	return 1;
}

/* A line that is not subject to the window (the sum of what it held back) but counts against the per-start limit: 1 if it may be written. */
static inline int btel_lim_force(BtelLim *m)
{
	if (m->total >= BTEL_BOOT_MAX)
		return 0;
	m->total++;
	return 1;
}

/* Lines held back by the window cap, to be reported once the window is over (0 = nothing to say yet); cleared when taken. */
static inline uint32_t btel_lim_take_held(BtelLim *m, uint32_t now_s)
{
	uint32_t n;

	if (!m->held || now_s - m->win_start_s < BTEL_WIN_S)
		return 0;
	n = m->held;
	m->held = 0;
	return n;
}

/* 1 once, when the per-start limit has been hit and nobody has said so yet. */
static inline int btel_lim_take_capped(BtelLim *m)
{
	if (m->total >= BTEL_BOOT_MAX && !m->capped) {
		m->capped = 1;
		return 1;
	}
	return 0;
}

/* An ending of link `e` seen at now_s: 1 = fold it into the previous one (the same device, reason and opener within BTEL_COLLAPSE_S), 0 = log it. When it is logged and
 * folded repeats were waiting for a summary, *summary is how many (the caller writes that line first, from the sum_* fields: they describe the folded group, not this ending), else 0. */
static inline int btel_collapse(BtelLim *m, const BtelEnd *e, uint32_t now_s, uint32_t *summary)
{
	*summary = 0;
	if (m->have && m->key_lo == e->lo && m->key_hi == e->hi && m->key_reason == e->reason && m->key_opener == e->opener &&
	    now_s - m->key_s < BTEL_COLLAPSE_S) {
		m->repeats++;
		m->rep_id = e->id;
		m->rep_dur_ds = e->dur_ds;
		m->key_s = now_s;
		return 1;
	}
	*summary = m->repeats;
	if (m->repeats)
		btel_lim_sum_copy(m);
	m->repeats = 0;
	m->have = 1;
	m->key_lo = e->lo;
	m->key_hi = e->hi;
	m->key_reason = e->reason;
	m->key_opener = e->opener;
	m->key_s = now_s;
	return 0;
}

/* Folded repeats nobody has summed up for BTEL_COLLAPSE_S (or at module stop, force): how many, and clears them; 0 = none due. */
static inline uint32_t btel_collapse_idle(BtelLim *m, uint32_t now_s, int force)
{
	uint32_t n = m->repeats;

	if (!n || (!force && now_s - m->key_s < BTEL_COLLAPSE_S))
		return 0;
	btel_lim_sum_copy(m);
	m->repeats = 0;
	return n;
}

/* 1 while endings of this device are being folded: its "opened" lines are left out as well (the loop would double the noise). */
static inline int btel_open_quiet(const BtelLim *m, uint32_t lo, uint32_t hi, uint32_t now_s)
{
	return m->have && m->repeats && m->key_lo == lo && m->key_hi == (hi & 0xFFFFu) && now_s - m->key_s < BTEL_COLLAPSE_S;
}

/* ---- the profile of an L2CAP channel --------------------------------------------------------------------------------------- */

/* The profile token for a channel object on the PSM `psm` (the RFCOMM channel's profile tag: 0x10000 = the headset path, 1 = hands-free); `av_n` = the audio PSM's channels
 * noted before this one on the link. */
static inline unsigned btel_psm_token(uint32_t psm, uint32_t rfcomm_tag, unsigned av_n)
{
	switch (psm) {
	case 0x0001: return BTEL_P_SDP;
	case 0x0003: return rfcomm_tag == 0x10000u ? BTEL_P_HSP : rfcomm_tag == 0x1u ? BTEL_P_HFP : BTEL_P_RFCOMM;
	case 0x0011: case 0x0013: return BTEL_P_HID;
	case 0x0017: return BTEL_P_AVRCP;
	case 0x0019: return av_n ? BTEL_P_A2DP_MEDIA : BTEL_P_A2DP_SIG;
	case 0x001B: return BTEL_P_AVRCP_BROWSE;
	}
	return BTEL_P_OTHER;
}

#endif /* BTEVLOG_CORE_H */
