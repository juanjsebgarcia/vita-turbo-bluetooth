/*
 * bthfp_conn.h - the console-started hands-free connection, and the swap of the console's own headset link. Included by
 * bthfp_mod.c only, so it is part of that file's translation unit; the host test compiles it the same way.
 *
 * Why: the console opens the headset profile (HSP) to a headset that offers it, and never the hands-free profile, which
 * is the one headsets report their battery over. This opens the hands-free connection: once the headset's sound link is
 * up and it offers a hands-free channel, the worker calls the Bluetooth code's RFCOMM connect routine (OFF_CONNECT) for
 * that channel with the headset path's profile tag (0x10000), so its lines reach the AT hook (bthfp_hook.c), which
 * answers them (bthfp.c). The call is made from the worker, holding the Bluetooth code's own lock (OFF_LOCK/OFF_UNLOCK),
 * never from a hook body. The outcome is read from the link's connected-profile word (+0x3E10, bit 0x10000).
 *
 * When: a link slot whose ACL link is up, whose device record has the audio bit, while Bluetooth sound out is up. Three
 * seconds after that (if the headset opens hands-free itself in the meantime, nothing is done), one connect; the state resets
 * when the slot stops being that headset. No hands-free record after ten seconds: no connect, logged. A connect that gets no
 * link in ten seconds has its unanswered channel object closed (the one this connect opened, never one the headset opened),
 * and is tried again about 30 and 60 seconds after the sound link came up: three attempts at most per link, none once any
 * headset or hands-free link is up. The swap's own connect is not retried.
 *
 * Mode 4 adds the swap: when the link that is up is the console's headset-profile link and the headset also offers
 * hands-free, the worker closes that RFCOMM channel with the channel's own close callback (OFF_CLOSE), adds the profile
 * to the link's refused-profile word (+0x3E28) so it is not opened again on this link, clears the headset bit in the
 * connected word once no channel with the headset tag is left, and then opens hands-free as above. The sound link is not
 * touched. At most once per link; if anything is not as expected it gives up and logs why.
 *
 * The microphone: a headset that offers hands-free gets the console's voice bit (bthfp_mic.h). When the audio policy then asks for the
 * microphone the console parks the request on the link (+0x3E24 holds the voice bit) until a headset profile is up; this connects now
 * (the same path and lock rules), at most once per parked request. The swap also clears the voice mark with the headset bit, so a
 * request made while the console's own link is gone parks instead of failing.
 *
 * Reads of the Bluetooth code's data are unlocked (a word can be one update old; the segment is always mapped). A device
 * pointer is used only if it is exactly an entry of the device table and names the same address as the link slot.
 */

#define OFF_LOCK     0x1B6B4u    /* the lock the exported calls take (returns 0 when held)                               */
#define OFF_UNLOCK   0x1B7A0u    /* and its release                                                                      */
#define OFF_CONNECT  0x15E1Cu    /* the RFCOMM connect routine: (connection, channel object or 0, profile, channel byte) */
#define OFF_HSCON    0x1189Cu    /* the console's own headset connect (checked, not called)                                */
#define OFF_CLOSE    0x15608u    /* swap: the RFCOMM channel's close callback: (connection, channel object), returns 1   */
#define OFF_RFC_VT   0x1F3E8u    /* ... the RFCOMM channel's callback table                                               */

/* Those routines are recognised by a hash of their first bytes, not by a copy of them: the window at each offset must have the
 * FNV-1a 64-bit hash below (bytes_fnv in bthfp_mod.c). Where the loader writes an address load (movw/movt) into the window, the
 * hash is of its opcode and register bits only: the immediate is left out. Those loads' offsets are listed per window, 4 bytes
 * each. The windows are the same on all twelve firmware builds. */
#define LOCK_LEN       0x40u
#define UNLOCK_LEN     0x30u
#define CONNECT_LEN    0x68u
#define HSCON_LEN      0x28u    /* no loader-written byte: the call at its end is relative */
#define CLOSE_LEN      0x4Cu    /* none either: its two calls are relative */
static const uint8_t k_lock_at[]    = { 4, 8, 12, 18, 50, 54 };
static const uint8_t k_unlock_at[]  = { 8, 12, 40, 44 };
static const uint8_t k_connect_at[] = { 14, 22, 90, 94 };
#define LOCK_FNV       0x00E4F8CA71592B06ull
#define UNLOCK_FNV     0x2E264870FFCCB156ull
#define CONNECT_FNV    0x25604197563D82FFull
#define HSCON_FNV      0x325EBCF134FA557Aull
#define CLOSE_FNV      0xCD799887026F71BEull

/* The callback table's entries as offsets from the module's text (the loader writes them as addresses: compared after the base
 * is subtracted). */
static const uint32_t k_rfc_vt[4] = { 0x15655u, 0x155DDu, 0x15799u, 0x15609u };

typedef int (*bt_lock_fn)(void);                               /* 0 = held */
typedef int (*bt_unlock_fn)(int);
typedef int (*bt_connect_fn)(void *conn, void *chan, uint32_t profile, uint32_t channel_byte);
typedef int (*bt_close_fn)(void *conn, void *chan);

static bt_lock_fn g_bt_lock;
static bt_unlock_fn g_bt_unlock;
static bt_connect_fn g_bt_connect;
static bt_close_fn g_bt_close;
static int g_rt_state;                      /* 0 not looked at, 1 the routines recognised, 2 not (no connect, no table move) */
static int g_close_state;                   /* the swap's close routine and table: 0 not looked at, 1 recognised, 2 not (no close) */

#define HFP_PROFILE_HS   0x10000u           /* the headset path's profile tag */
#define HFP_RC_NOLOCK    0x7FFFFFFEu        /* "the call" result when the lock could not be taken (not a console value)    */
#define HFP_RC_NOCHAN    0x802F0701u        /* what the connect routine returns when it has no channel record               */
#define HFP_RC_NOOBJ     0x7FFFFFFDu        /* swap: the headset path's channel object was not found (not a console value) */

/* The swap's close routine and the RFCOMM callback table it belongs to, once, after the lock and connect are recognised. */
static void hfp_close_check(void)
{
	const volatile uint8_t *b = (const volatile uint8_t *)g_at_base;
	int ok = bytes_fnv(b + OFF_CLOSE, CLOSE_LEN, 0, 0) == CLOSE_FNV;

	for (int i = 0; ok && i < 4; i++)
		ok = *(const volatile uint32_t *)(g_at_base + OFF_RFC_VT + 4u * (uint32_t)i) - (uint32_t)g_at_base == k_rfc_vt[i];
	if (ok) {
		g_bt_close = (bt_close_fn)(g_at_base + OFF_CLOSE + 1);
		g_close_state = 1;
	} else {
		g_close_state = 2;
		BTT_KLOG("bluetooth hfp: the RFCOMM close routine is not recognised - the swap will not close the headset link");
	}
}

/* The lock, then the routines, are recognised once, after the Bluetooth code is found (g_at_base): 1 yes, 2 no. */
static int hfp_routines_check(void)
{
	const volatile uint8_t *b = (const volatile uint8_t *)g_at_base;

	if (g_rt_state || !g_at_base)
		return g_rt_state;
	if (bytes_fnv(b + OFF_LOCK, LOCK_LEN, k_lock_at, sizeof(k_lock_at)) == LOCK_FNV &&
	    bytes_fnv(b + OFF_UNLOCK, UNLOCK_LEN, k_unlock_at, sizeof(k_unlock_at)) == UNLOCK_FNV &&
	    bytes_fnv(b + OFF_CONNECT, CONNECT_LEN, k_connect_at, sizeof(k_connect_at)) == CONNECT_FNV &&
	    bytes_fnv(b + OFF_HSCON, HSCON_LEN, 0, 0) == HSCON_FNV) {
		g_bt_lock = (bt_lock_fn)(g_at_base + OFF_LOCK + 1);              /* +1: Thumb code */
		g_bt_unlock = (bt_unlock_fn)(g_at_base + OFF_UNLOCK + 1);
		g_bt_connect = (bt_connect_fn)(g_at_base + OFF_CONNECT + 1);
		g_rt_state = 1;
		hfp_close_check();
	} else {
		g_rt_state = 2;
		BTT_KLOG("bluetooth hfp: the Bluetooth code's lock or connect routine is not recognised - no table move, no connect");
	}
	return g_rt_state;
}

/* The Bluetooth code's lock, taken at the raised priority the other lock takers here use (the thread that holds it is the
 * one that writes or calls, so nothing in the Bluetooth code runs meanwhile). 0 = held. `raise` 0: leave the priority
 * alone (module stop: the caller is not the worker). */
static int hfp_lock(int raise)
{
	int rc;

	if (!g_bt_lock || !g_bt_unlock)
		return -1;
	if (raise)
		ksceKernelChangeThreadPriority(0, HFP_LOCK_PRIO);
	rc = g_bt_lock();
	if (rc != 0 && raise)
		ksceKernelChangeThreadPriority(0, HFP_WORKER_PRIO);
	return rc;
}

static void hfp_unlock(int raise)
{
	g_bt_unlock(0);
	if (raise)
		ksceKernelChangeThreadPriority(0, HFP_WORKER_PRIO);
}

/* ---- the headset's link slot, read ------------------------------------------------------------- */

#define HC_STATE     0x18u        /* the Bluetooth code's own state word (seg1): bits 0x38 == 8 once it is up (as the exports check) */
#define HC_CONN0     0x104A8u     /* the link table: 7 slots of 0x41D0 bytes                                                        */
#define HC_CONN_STRIDE 0x41D0u
#define HC_NSLOT     7
#define HC_DEV0      0x2D960u     /* the device table: 32 entries of 0x318 bytes                                                    */
#define HC_DEV_END   0x33C60u
#define HC_DEV_STRIDE 0x318u
#define HC_C_FLAGS   0x04u        /* link slot: bit 0x100 = the ACL link is up (SceBt sets it when it completes); bit 0x40 = the state its
                                   * ksceBtGetConnectingInfo reports as 4, which its own policy does not start profiles in */
#define HC_C_MAC_LO  0x18u
#define HC_C_MAC_HI  0x1Cu
#define HC_C_DEV     0x14A4u      /* ... the device record it belongs to                                                            */
#define HC_C_CHAN    0x14ABu      /* ... the RFCOMM channel byte of the headset path                                                */
#define HC_C_CONN    0x3E10u      /* ... the connected-profile word                                                                 */
#define HC_C_PARKED  0x3E24u      /* ... the request waiting for a profile to come up (0x20000000 = a voice request)                 */
#define HC_C_PEER    0x3E28u      /* ... profiles the remote refused (answered DM): the policy does not open them on this link;
                                   * the swap adds the headset path's bit to it */
#define HC_D_MAC_LO  0x00u
#define HC_D_MAC_HI  0x04u
#define HC_D_BITS    0x20u        /* device record: profile bits (0x8 audio sink, 0x1 hands-free, 0x10000 headset, ...)             */
#define HC_D_HF      0x30u        /* ... hands-free channel byte (the server channel times 2), 0 = none                             */
#define HC_D_HS      0x32u        /* ... headset channel byte                                                                       */
#define HC_AV_BTOUT  0x10u        /* the audio policy's device bit: Bluetooth sound out                                             */
#define HC_BIT_LINK  0x100u
#define HC_BIT_OTHER 0x40u
#define HC_DEV_SINK  0x8u
#define HC_BUSY_BITS (HFP_PROFILE_HS | 0x1u)
#define HC_VOICE_MARK 0x20000000u /* device record: the headset has a microphone; link slot: a voice profile is connected (or, parked, wanted) */
#define HC_PROFILE_BITS 0x30003u  /* the link slot's connected word: the profile bits (the voice mark is set whenever one is up)             */
#define HC_CH0       0x14B0u      /* the link slot's channel objects: 16 of 0x268 bytes */
#define HC_CH_STRIDE 0x268u
#define HC_CH_END    0x3B30u
#define HC_CH_PSM    0x34u        /* channel object: its L2CAP PSM (u16; 3 = RFCOMM; 0 once its L2CAP channel is closed)             */
#define HC_CH_VT     0x3Cu        /* ... its callback table                                                                          */
#define HC_CH_SRV    0x25Cu       /* ... (RFCOMM) the server channel byte it was opened to */
#define HC_CH_PROF   0x264u       /* ... (RFCOMM) the profile tag                                                                    */
#define HC_CH_BITS   0x260u       /* ... (RFCOMM) its state bits (0x20: marked for closing, set by the close)                          */
#define HC_CH_CLOSING 0x20u
#define HC_CONSOLE_CHAN 8u        /* the console's own server channel 4 as a channel byte: connects to it are the headset's own      */
#define HC_PSM_RFCOMM 3u

typedef struct {
	int slot;                  /* the headset's link slot, -1 = none                                          */
	uintptr_t conn;            /* its address                                                                 */
	uint32_t mac_lo, mac_hi;
	uint32_t flags, conn_word, peer_word, dev_bits, parked;
	uint8_t hf, hs, chan;      /* the headset's hands-free and headset channel bytes, the channel byte in use */
	int a2dp;                  /* the audio policy says Bluetooth sound out is up                             */
} HfpScan;

static uint32_t hc_rd32(uintptr_t a) { return *(const volatile uint32_t *)a; }
static uint8_t hc_rd8(uintptr_t a) { return *(const volatile uint8_t *)a; }
static uint16_t hc_rd16(uintptr_t a) { return *(const volatile uint16_t *)a; }

/* The first link slot that is a live headset link (see above); s->slot is -1 if none. 0 if the data segment is not readable. */
static int hfp_scan(HfpScan *s)
{
	uintptr_t seg1 = btt_bt_seg1(), av;

	memset(s, 0, sizeof(*s));
	s->slot = -1;
	if (!seg1)
		return 0;
	for (int i = 0; i < HC_NSLOT; i++) {
		uintptr_t c = seg1 + HC_CONN0 + (uintptr_t)i * HC_CONN_STRIDE, d;
		uint32_t fl = hc_rd32(c + HC_C_FLAGS), lo = hc_rd32(c + HC_C_MAC_LO), hi = hc_rd32(c + HC_C_MAC_HI);
		uint32_t dev = hc_rd32(c + HC_C_DEV) - (uint32_t)seg1;      /* offset in the segment, or garbage */

		if (!(fl & HC_BIT_LINK) || (fl & HC_BIT_OTHER) || !(lo || (hi & 0xFFFF)))
			continue;
		if (dev < HC_DEV0 || dev >= HC_DEV_END || (dev - HC_DEV0) % HC_DEV_STRIDE)
			continue;
		d = seg1 + dev;
		if (hc_rd32(d + HC_D_MAC_LO) != lo || (hc_rd32(d + HC_D_MAC_HI) & 0xFFFF) != (hi & 0xFFFF))
			continue;
		if (!(hc_rd32(d + HC_D_BITS) & HC_DEV_SINK))
			continue;
		s->slot = i;
		s->conn = c;
		s->mac_lo = lo;
		s->mac_hi = hi & 0xFFFF;
		s->flags = fl;
		s->conn_word = hc_rd32(c + HC_C_CONN);
		s->peer_word = hc_rd32(c + HC_C_PEER);
		s->parked = hc_rd32(c + HC_C_PARKED);
		s->dev_bits = hc_rd32(d + HC_D_BITS);
		s->hf = hc_rd8(d + HC_D_HF);
		s->hs = hc_rd8(d + HC_D_HS);
		s->chan = hc_rd8(c + HC_C_CHAN);
		break;
	}
	av = btt_hfp_av_seg1();
	s->a2dp = av && (*(const volatile uint32_t *)(av + AV_DEVBITS) & HC_AV_BTOUT) ? 1 : 0;
	return 1;
}

/* Swap: the console's own headset-profile link is what is up: the link's channel byte is the headset's headset channel
 * (not its hands-free one, which it also offers), the headset path is connected, no hands-free bit, and the headset did not
 * refuse the headset profile. */
static int hfp_hs_stock(const HfpScan *s)
{
	return s->slot >= 0 && s->hf && s->hs && s->hf != s->hs && s->chan == s->hs && (s->conn_word & HFP_PROFILE_HS) &&
	       !(s->conn_word & 0x1u) && !(s->peer_word & HFP_PROFILE_HS);
}

/* Swap: the headset path's RFCOMM channel object on that link (PSM 3, opened to the headset channel, the headset path's
 * profile tag, the RFCOMM callback table), or 0. */
static uintptr_t hfp_hs_chan(const HfpScan *s)
{
	if (s->slot < 0 || !s->hs || !g_at_base)
		return 0;
	for (uintptr_t ch = s->conn + HC_CH0; ch + HC_CH_STRIDE <= s->conn + HC_CH_END; ch += HC_CH_STRIDE)
		if (hc_rd16(ch + HC_CH_PSM) == HC_PSM_RFCOMM && hc_rd32(ch + HC_CH_SRV) == s->hs && hc_rd32(ch + HC_CH_PROF) == HFP_PROFILE_HS &&
		    hc_rd32(ch + HC_CH_VT) == (uint32_t)(g_at_base + OFF_RFC_VT))
			return ch;
	return 0;
}

/* Swap: any RFCOMM channel object on that link that carries the headset path's profile tag, whichever channel and whoever opened
 * it (the headset may open its own link to the console's channel 4 while ours closes): the headset path is free only when there is none. */
static int hfp_hs_any(const HfpScan *s)
{
	if (s->slot < 0)
		return 0;
	for (uintptr_t ch = s->conn + HC_CH0; ch + HC_CH_STRIDE <= s->conn + HC_CH_END; ch += HC_CH_STRIDE)
		if (hc_rd16(ch + HC_CH_PSM) == HC_PSM_RFCOMM && hc_rd32(ch + HC_CH_PROF) == HFP_PROFILE_HS)
			return 1;
	return 0;
}

/* An RFCOMM channel object this module's connect opened: opened to the headset's hands-free channel with the headset path's tag, the
 * RFCOMM callback table. Not one the headset opened (to the console's channel 4), and none where the channel bytes cannot tell them
 * apart. */
static int hfp_ours(const HfpScan *s, uintptr_t ch)
{
	return s->slot >= 0 && s->hf && s->hf != HC_CONSOLE_CHAN && s->hf != s->hs && g_at_base && hc_rd16(ch + HC_CH_PSM) == HC_PSM_RFCOMM &&
	       hc_rd32(ch + HC_CH_SRV) == s->hf && hc_rd32(ch + HC_CH_PROF) == HFP_PROFILE_HS && hc_rd32(ch + HC_CH_VT) == (uint32_t)(g_at_base + OFF_RFC_VT);
}

/* ---- the state machine (pure: no reads, no calls) ------------------------------------------------ */

enum {
	HC_IDLE,         /* no headset link                                                                 */
	HC_WAIT_A2DP,    /* a headset link, the sound link not up yet                                       */
	HC_WAIT_HF,      /* sound up: settling, and waiting for the headset's hands-free record            */
	HC_BUSY,         /* a headset or hands-free link was already up (the headset's own, or the console's own): nothing to do */
	HC_NOHF,         /* the headset has no hands-free record (10 s after the sound link came up)        */
	HC_NOTREADY,     /* it has, but the hook is not answering or a routine is not recognised            */
	HC_CALLED,       /* the connect routine was called: waiting for the link                            */
	HC_UP,           /* the link is up                                                                  */
	HC_FAILED,       /* the lock could not be taken, or the routine had no channel record               */
	HC_TIMEOUT,      /* called, and no link after 10 s (tried again later while attempts are left)      */
	HC_CLOSING,      /* swap: the console's own headset link is being closed                        */
	HC_CLOSE_FAILED, /* swap: the lock, the routine or the channel object was not there: nothing done */
	HC_CLOSE_TIMEOUT,/* swap: not closed, or not connected, within 5 s of the close: given up      */
	HC_STATES
};

#define HFP_SETTLE_MS    3000u    /* from the sound link up: the headset may open its own link in that time */
#define HFP_SDP_WAIT_MS  10000u   /* ... and its records are read by then or not at all                      */
#define HFP_LINK_WAIT_MS 10000u
#define HFP_CLOSE_WAIT_MS 5000u   /* swap: from the close to the headset link's channel being gone       */

#define HFP_GO_CONNECT 1          /* hfp_conn_step's answers: connect now, */
#define HFP_GO_CLOSE   2          /* or close the console's own headset link first (the swap),               */
#define HFP_GO_DROP    3          /* or close our unanswered channel object (hfp_conn_drop)                  */
#define HFP_TRIES      3u         /* connects per link: at 3 s, 30 s and 60 s after the sound link came up   */

typedef struct {
	int found, a2dp, ready, busy, linked;
	int swap;                         /* mode 4 is on and its close routine recognised                    */
	int hs_stock;                     /* the console's own headset-profile link is what is up (hfp_hs_stock) */
	int hs_live;                      /* an RFCOMM channel object with the headset path's tag is there (hfp_hs_any) */
	int can_close;                    /* the close routine is recognised: an unanswered connect can be cleaned up and tried again */
	uint32_t mac_lo, mac_hi;
	uint8_t hf;
} HfpConnIn;

typedef struct {
	int state;
	uint32_t mac_lo, mac_hi;
	uint64_t t_a2dp, t_called, t_close;
	uint32_t attempts, last_rc;       /* calls made; the newest result (0x7FFFFFFF = none yet) */
	uint32_t tries;                   /* calls made on this link                               */
	int seen;                         /* connections tracked since the module started          */
	int closed;                       /* the swap closed the console's headset link on this link */
	uint32_t closes;                  /* swap closes made since the module started         */
} HfpConn;

static HfpConn g_hc = { .state = HC_IDLE, .last_rc = 0x7FFFFFFFu };

/* When each connect on a link is due, from the sound link coming up (the first also waits for the headset's records). */
static const uint16_t k_try_ms[HFP_TRIES] = { HFP_SETTLE_MS, 30000u, 60000u };

/* One pass. Returns HFP_GO_CONNECT when the caller should connect now (and then say what came of it with hfp_conn_called),
 * HFP_GO_CLOSE when it should close the console's own headset link first (then hfp_conn_close_called), HFP_GO_DROP when it should
 * close our unanswered channel object (once, as the state goes to HC_TIMEOUT), else 0. */
static int hfp_conn_step(HfpConn *c, const HfpConnIn *in, uint64_t now_ms)
{
	if (c->state != HC_IDLE && (!in->found || in->mac_lo != c->mac_lo || in->mac_hi != c->mac_hi)) {
		c->state = HC_IDLE;                     /* that link is gone: the next one is a new connection */
		c->closed = 0;
		c->tries = 0;
	}
	if (!in->found)
		return 0;
	if (c->state == HC_IDLE) {
		c->state = HC_WAIT_A2DP;
		c->mac_lo = in->mac_lo;
		c->mac_hi = in->mac_hi;
		c->seen++;
	}
	switch (c->state) {
	case HC_WAIT_A2DP:
		if (!in->a2dp)
			return 0;
		c->state = HC_WAIT_HF;
		c->t_a2dp = now_ms;
		return 0;
	case HC_WAIT_HF:
		if (in->busy && in->swap && in->hs_stock && !c->closed) {
			if (now_ms - c->t_a2dp < HFP_SETTLE_MS)
				return 0;                   /* the headset may still open hands-free itself */
			if (!in->ready) {
				if (now_ms - c->t_a2dp >= HFP_SDP_WAIT_MS)
					c->state = HC_NOTREADY;
				return 0;
			}
			return HFP_GO_CLOSE;
		}
		if (in->busy) {
			c->state = HC_BUSY;
			return 0;
		}
		if (now_ms - c->t_a2dp < HFP_SETTLE_MS)
			return 0;
		if (in->hf && in->ready)
			return HFP_GO_CONNECT;
		if (now_ms - c->t_a2dp >= HFP_SDP_WAIT_MS)
			c->state = in->hf ? HC_NOTREADY : HC_NOHF;
		return 0;
	case HC_CLOSING:
		if (now_ms - c->t_close >= HFP_CLOSE_WAIT_MS) {
			c->state = HC_CLOSE_TIMEOUT;    /* not gone, or gone and hands-free could not be opened, in 5 s: no connect */
			return 0;
		}
		if (!in->hs_live)
			return HFP_GO_CONNECT;          /* gone: open hands-free (hfp_conn_called moves the state on) */
		return 0;
	case HC_CALLED:
		if (in->linked) {
			c->state = HC_UP;
		} else if (now_ms - c->t_called >= HFP_LINK_WAIT_MS) {
			c->state = HC_TIMEOUT;
			return in->can_close && !c->closed ? HFP_GO_DROP : 0;
		}
		return 0;
	case HC_TIMEOUT:
		if (!in->can_close || c->closed || c->tries >= HFP_TRIES)
			return 0;                       /* nothing to clean up with, the swap's own connect, or the attempts are used */
		if (in->busy) {
			c->state = HC_BUSY;             /* a headset or hands-free link came up, from either side */
			return 0;
		}
		if (in->hf && in->ready && now_ms - c->t_a2dp >= k_try_ms[c->tries])
			return HFP_GO_CONNECT;
		return 0;
	default:
		return 0;
	}
}

/* What the connect call returned (or HFP_RC_NOLOCK). */
static void hfp_conn_called(HfpConn *c, uint32_t rc, uint64_t now_ms)
{
	c->attempts++;
	c->tries++;
	c->last_rc = rc & 0x7FFFFFFFu;
	if (rc == HFP_RC_NOLOCK || rc == HFP_RC_NOCHAN) {
		c->state = HC_FAILED;
		return;
	}
	c->state = HC_CALLED;
	c->t_called = now_ms;
}

/* Swap: what the close came to: 1 called, or HFP_RC_NOLOCK / HFP_RC_NOOBJ. */
static void hfp_conn_close_called(HfpConn *c, uint32_t rc, uint64_t now_ms)
{
	c->closes++;
	c->closed = 1;                          /* once per link, whatever came of it */
	if (rc == HFP_RC_NOLOCK || rc == HFP_RC_NOOBJ) {
		c->state = HC_CLOSE_FAILED;
		return;
	}
	c->state = HC_CLOSING;
	c->t_close = now_ms;
}

/* The microphone's trigger: one parked voice request starts one connect. `fired` stays set while the request is parked, so a connect that
 * does not come up is not repeated for the same request; it is cleared when the request is gone or the link is another. */
typedef struct { int fired; uint32_t mac_lo, mac_hi; uint32_t count; } HfpTrig;
static HfpTrig g_trig;

/* One pass. `parked`: the link's parked word holds a voice request. `can`: a connect would be made now. 1 = connect now. */
static int hfp_trig_step(HfpTrig *t, int found, uint32_t mac_lo, uint32_t mac_hi, int parked, int can)
{
	if (!found || mac_lo != t->mac_lo || mac_hi != t->mac_hi) {
		t->fired = 0;
		t->mac_lo = found ? mac_lo : 0;
		t->mac_hi = found ? mac_hi : 0;
	}
	if (!found || !parked) {
		t->fired = 0;
		return 0;
	}
	if (t->fired || !can)
		return 0;
	t->fired = 1;
	t->count++;
	return 1;
}

/* The connect machine's states in which a parked request may start the connect: it has not called the connect and is not closing the
 * console's own link (it opens hands-free itself when that is gone). */
static int hfp_trig_state_ok(int st)
{
	return st >= 0 && st < HC_STATES && ((1u << st) & ((1u << HC_WAIT_A2DP) | (1u << HC_WAIT_HF) | (1u << HC_NOHF) | (1u << HC_NOTREADY) |
	       (1u << HC_FAILED) | (1u << HC_TIMEOUT) | (1u << HC_CLOSE_FAILED) | (1u << HC_CLOSE_TIMEOUT)));
}

static void hfp_conn_reset(HfpConn *c)
{
	c->state = HC_IDLE;
	c->closed = 0;
	c->tries = 0;
	g_trig.fired = 0;
}

/* The connected word once the console's own headset link is gone: the headset path's bit out, and the voice mark with it unless another
 * profile is still connected (the console never clears the mark itself). */
static uint32_t hfp_swap_word(uint32_t w)
{
	w &= ~HFP_PROFILE_HS;
	if (!(w & HC_PROFILE_BITS))
		w &= ~HC_VOICE_MARK;
	return w;
}

/* ---- the worker's side ----------------------------------------------------------------------- */

static const char *hfp_conn_words(int st)
{
	static const char *const w[HC_STATES] = {
		"no headset link", "headset link, waiting for the sound link", "sound link up, waiting for its hands-free record",
		"a headset or hands-free link is already up", "the headset has no hands-free record", "hands-free record found but the hook or a routine is not ready",
		"connect called, waiting for the link", "hands-free link up", "connect failed", "connect called, no link after 10 s",
		"closing the console's own headset link", "could not close the console's own headset link",
		"the console's own headset link did not close, or hands-free could not be opened, within 5 s of the close",
	};
	return st >= 0 && st < HC_STATES ? w[st] : "?";
}

/* Opens the connection: under the lock, with the slot read again, as the console's own headset connect does it. */
static void hfp_conn_connect(const HfpScan *s, uint64_t now_ms, int after_close)
{
	HfpScan t;
	uint32_t rc;
	int cleared = -1;                       /* swap: 1 the worker cleared the headset bit, 0 the Bluetooth code had */
	int marked = 0;                         /* swap: the worker also cleared the voice mark                                */

	if (hfp_lock(1) != 0) {
		BTT_KLOG("bluetooth hfp: connect: could not take the Bluetooth lock");
		hfp_conn_called(&g_hc, HFP_RC_NOLOCK, now_ms);
		return;
	}
	hfp_scan(&t);
	if (after_close && t.slot == s->slot && t.mac_lo == s->mac_lo && t.mac_hi == s->mac_hi && !hfp_hs_any(&t)) {
		/* the console's own headset link is gone; the refused-profile bit is ours (hfp_conn_close) */
		uint32_t w = hfp_swap_word(t.conn_word);

		cleared = (t.conn_word & HFP_PROFILE_HS) != 0;
		marked = (t.conn_word & ~w & HC_VOICE_MARK) != 0;
		if (w != t.conn_word) {
			*(volatile uint32_t *)(t.conn + HC_C_CONN) = w;
			t.conn_word = w;
		}
		t.peer_word &= ~HFP_PROFILE_HS;
	}
	if (t.slot != s->slot || t.mac_lo != s->mac_lo || t.mac_hi != s->mac_hi || !t.hf || (t.conn_word & HC_BUSY_BITS) || (t.peer_word & HFP_PROFILE_HS)) {
		hfp_unlock(1);
		BTT_KLOG("bluetooth hfp: connect: the headset's link changed while taking the lock - not connecting this pass");
		return;
	}
	*(volatile uint8_t *)(t.conn + HC_C_CHAN) = t.hf;
	rc = (uint32_t)g_bt_connect((void *)t.conn, 0, HFP_PROFILE_HS, t.hf);
	hfp_unlock(1);
	if (cleared >= 0)
		BTT_KLOG("bluetooth hfp: the console's own headset link is closed (%s%s)", cleared ? "its connected bit was still set: cleared by this" :
		         "the Bluetooth code cleared its connected bit", marked ? "; voice mark cleared" : "");
	BTT_KLOG("bluetooth hfp: connect: opened RFCOMM channel %u of slot %d (attempt %u); the routine returned 0x%08x", (unsigned)(t.hf >> 1), t.slot,
	         (unsigned)g_hc.attempts + 1, (unsigned)rc);
	hfp_conn_called(&g_hc, rc, now_ms);
}

/* Swap: closes the console's own headset link with the RFCOMM channel's own close, under the lock, with the slot read again,
 * and marks the headset profile refused on this link so the console's policy does not open it again. */
static void hfp_conn_close(const HfpScan *s, uint64_t now_ms)
{
	HfpScan t;
	uintptr_t ch;

	if (hfp_lock(1) != 0) {
		BTT_KLOG("bluetooth hfp: close: could not take the Bluetooth lock");
		hfp_conn_close_called(&g_hc, HFP_RC_NOLOCK, now_ms);
		return;
	}
	hfp_scan(&t);
	if (t.slot != s->slot || t.mac_lo != s->mac_lo || t.mac_hi != s->mac_hi || !hfp_hs_stock(&t)) {
		hfp_unlock(1);
		BTT_KLOG("bluetooth hfp: close: the headset's link changed while taking the lock - not closing this pass");
		return;
	}
	ch = hfp_hs_chan(&t);
	if (!ch) {
		hfp_unlock(1);
		BTT_KLOG("bluetooth hfp: close: the headset link's RFCOMM channel object was not found - nothing closed");
		hfp_conn_close_called(&g_hc, HFP_RC_NOOBJ, now_ms);
		return;
	}
	g_bt_close((void *)t.conn, (void *)ch);
	/* read the word again: the close call may itself have changed it */
	*(volatile uint32_t *)(t.conn + HC_C_PEER) = hc_rd32(t.conn + HC_C_PEER) | HFP_PROFILE_HS;
	hfp_unlock(1);
	BTT_KLOG("bluetooth hfp: close: closing the console's own headset link (RFCOMM channel %u of slot %d) to open hands-free on channel %u",
	         (unsigned)(t.hs >> 1), t.slot, (unsigned)(t.hf >> 1));
	hfp_conn_close_called(&g_hc, 1, now_ms);
}

/* A connect got no link in ten seconds: closes the channel object it left (hfp_ours), with the RFCOMM channel's own close, under the lock, the
 * slot read again. Nothing else is written. An object already marked for closing is left; nothing is closed once a headset or hands-free link
 * is up. */
static void hfp_conn_drop(const HfpScan *s)
{
	HfpScan t;
	int n = 0;

	if (hfp_lock(1) != 0) {
		BTT_KLOG("bluetooth hfp: no link after 10 s: could not take the Bluetooth lock to close our channel");
		return;
	}
	hfp_scan(&t);
	if (t.slot == s->slot && t.mac_lo == s->mac_lo && t.mac_hi == s->mac_hi && !(t.conn_word & HC_BUSY_BITS))
		for (uintptr_t ch = t.conn + HC_CH0; ch + HC_CH_STRIDE <= t.conn + HC_CH_END; ch += HC_CH_STRIDE)
			if (hfp_ours(&t, ch) && !(hc_rd32(ch + HC_CH_BITS) & HC_CH_CLOSING)) {
				g_bt_close((void *)t.conn, (void *)ch);
				n++;
			}
	hfp_unlock(1);
	BTT_KLOG("bluetooth hfp: no link after 10 s: closed %d of our channel(s) on slot %d (attempt %u of %u%s)", n, t.slot, (unsigned)g_hc.tries,
	         (unsigned)HFP_TRIES, g_hc.tries < HFP_TRIES ? ", will try again" : ", giving up");
}

/* Once per worker pass in mode 3 and 4. `ready`: the hook is answering. `swap`: mode 4. */
static void hfp_conn_poll(uint64_t now_ms, int ready, int swap)
{
	HfpScan s;
	HfpConnIn in;
	int old = g_hc.state, go;

	hfp_scan(&s);
	in.found = s.slot >= 0;
	in.mac_lo = s.mac_lo;
	in.mac_hi = s.mac_hi;
	in.a2dp = s.a2dp;
	in.hf = s.hf;
	in.ready = ready && g_rt_state == 1;
	in.busy = (s.conn_word & HC_BUSY_BITS) || (s.peer_word & HFP_PROFILE_HS);
	in.linked = (s.conn_word & HFP_PROFILE_HS) != 0;
	in.swap = swap && g_close_state == 1;
	in.can_close = g_close_state == 1;
	in.hs_stock = hfp_hs_stock(&s);
	in.hs_live = hfp_hs_any(&s);
	go = hfp_conn_step(&g_hc, &in, now_ms);
	if (g_hc.state != old) {
		if (g_hc.state == HC_IDLE)
			BTT_KLOG("bluetooth hfp: the headset link is gone");
		else if (old == HC_IDLE)
			BTT_KLOG("bluetooth hfp: headset link seen in slot %d (device bits 0x%x, hands-free channel %u, headset channel %u)", s.slot,
			         (unsigned)s.dev_bits, (unsigned)(s.hf >> 1), (unsigned)(s.hs >> 1));
		if (g_hc.state != HC_IDLE && g_hc.state != HC_WAIT_A2DP)
			BTT_KLOG("bluetooth hfp: %s (slot %d, device bits 0x%x, connected word 0x%x)", hfp_conn_words(g_hc.state), s.slot, (unsigned)s.dev_bits,
			         (unsigned)s.conn_word);
		if (g_hc.state == HC_UP)
			BTT_KLOG("bluetooth hfp: the hands-free link is up on RFCOMM channel %u; handshake steps seen 0x%x", (unsigned)(s.chan >> 1),
			         (unsigned)bthfp_steps);
	}
	if (go == HFP_GO_CLOSE) {
		hfp_conn_close(&s, now_ms);
	} else if (go == HFP_GO_DROP) {
		hfp_conn_drop(&s);
	} else if (go == HFP_GO_CONNECT) {
		if (g_hc.state == HC_TIMEOUT)
			BTT_KLOG("bluetooth hfp: still no hands-free link: connecting again (attempt %u of %u)", (unsigned)g_hc.tries + 1, (unsigned)HFP_TRIES);
		hfp_conn_connect(&s, now_ms, g_hc.state == HC_CLOSING);
	} else if (hfp_trig_step(&g_trig, s.slot >= 0, s.mac_lo, s.mac_hi, (s.parked & HC_VOICE_MARK) != 0,
	                         in.ready && !in.busy && s.hf && (s.dev_bits & 0x1u) && hfp_trig_state_ok(g_hc.state))) {
		BTT_KLOG("bluetooth hfp: voice request parked on slot %d, connecting (%u)", s.slot, (unsigned)g_trig.count);
		hfp_conn_connect(&s, now_ms, 0);
	}
}
