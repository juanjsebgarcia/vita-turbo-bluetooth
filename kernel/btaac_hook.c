/*
 * btaac_hook.c - the hook bodies of AAC streaming (H1 and H3 of btaac_hook.h), and nothing else: this object file is checked whole by
 * tools/check_bteq_leaf.sh. They run in the Bluetooth code's own threads: no logging, no locks, no waits, no allocation, no floating point,
 * no vector instruction, and no call but the original routines (indirect), the engine's access unit leaves (btt_aac_pop_au, btt_aac_au_trim, themselves
 * indirect calls) and the Bluetooth code's own frame sender by pointer (the fallback, and the replies of the acceptor path).
 *
 * The signalling hook (H1) reads each reply the Bluetooth code receives; pages are checked with the address-translation operation before
 * they are read: the message is the Bluetooth code's own buffer, but an unmapped page would be a data abort in
 * its thread. The media hook (H3) never waits: with no access unit ready it sends nothing for this block.
 */
#include <taihen.h>

#include "btaac.h"
#include "btaac_hook.h"
#include "btaac_hook_fn.h"
#include "btaac_latm.h"
#include "btaac_lat.h"
#include "btaptx_link.h"
#include "btldac_link.h"

#define REF_SPINS    200000u      /* loads, no system call: about a millisecond at most */
#define BAD_STATE    0x802F0508   /* what the Bluetooth code's send routine itself returns when it cannot send (a negative number: nothing was sent) */
#define MAX_MSG      0x2000       /* a signalling message longer than this is not one */
#define CAPS_COPY    64u          /* the most of a capabilities reply that is looked at */

#define SIG_DISCOVER 1u
#define SIG_GETCAPS  2u
#define SIG_SETCONF  3u
#define MSG_COMMAND  0u
#define MSG_GENERAL  1u          /* general reject: the sink does not support the command at all */
#define MSG_ACCEPT   2u
#define MSG_REJECT   3u
#define SIG_OPEN     6u
#define SIG_START    7u
#define SIG_CLOSE    8u
#define SIG_SUSPEND  9u
#define SIG_ABORT    10u
#define ACT_MAX      24u         /* the most of a command that is copied: SET_CONFIGURATION is at most 4 + the code's own limit of 16 */
#define REC_CONFIGURED 5u        /* the stream record's state after the code accepted a SET_CONFIGURATION (acceptor) */
#define REC_IDLE     1u

volatile tai_hook_ref_t btaac_disp_ref, btaac_send_ref;
volatile uint32_t btaac_disp_in, btaac_send_in;

#ifdef BTT_HOST_TEST
/* the host test says which addresses are mapped (btaac_host_mapped is the test's); the translation operation is the console's */
extern int btaac_host_mapped(uintptr_t va);
static inline int mapped_page(uintptr_t va) { return btaac_host_mapped(va); }
#else
/* 1 if va is readable at PL1; interrupts masked so a handler cannot overwrite PAR between the two instructions. */
static inline __attribute__((always_inline)) int mapped_page(uintptr_t va)
{
	uint32_t cpsr, par;

	__asm__ volatile("mrs %0, cpsr\n\tcpsid i" : "=r"(cpsr) :: "memory");
	__asm__ volatile("mcr p15, 0, %0, c7, c8, 0\n\tisb" :: "r"(va) : "memory");   /* ATS1CPR */
	__asm__ volatile("mrc p15, 0, %0, c7, c4, 0" : "=r"(par));                    /* PAR */
	if (!(cpsr & 0x80u))
		__asm__ volatile("cpsie i" ::: "memory");
	return !(par & 1u);   /* bit 0 set = the translation faulted */
}
#endif

/* Every byte of [p, p + n) is on a mapped page (n >= 1). */
static inline __attribute__((always_inline)) int readable(const void *ptr, uint32_t n)
{
	uintptr_t p = (uintptr_t)ptr, first = p & ~(uintptr_t)0xFFF, last = (p + n - 1u) & ~(uintptr_t)0xFFF;

	for (uintptr_t pg = first; ; pg += 0x1000u) {
		if (!mapped_page(pg < p ? p : pg))
			return 0;
		if (pg == last)
			return 1;
	}
}

/* ---- H1: the signalling receive routine ------------------------------------------------------------------------------------ */

/* The reply to GET_CAPABILITIES for the endpoint being asked (the connection's own index into the list DISCOVER returned). */
static void note_caps(uintptr_t conn, const uint8_t *msg, int len)
{
	uint8_t c[CAPS_COPY];
	uint8_t info[2];
	uint32_t idx = *(const volatile uint8_t *)(conn + BTAAC_C_INDEX), n, i;
	BtAacSink sink;

	btaac_neg.n_caps++;
	/* a choice already made stands, except that aptX HD is preferred to classic aptX when a later endpoint of the sink offers it */
	if ((btaac_neg.have && !(btaac_neg.want_aptx_hd && !btaac_neg.apt_hd)) || idx + 2u > 16u || len <= 2 || len > MAX_MSG)
		return;
	info[0] = *(const volatile uint8_t *)(conn + BTAAC_C_LIST + idx);
	info[1] = *(const volatile uint8_t *)(conn + BTAAC_C_LIST + idx + 1u);
	if (!btaac_sep_free_audio_sink(info))
		return;
	n = (uint32_t)len - 2u;
	if (n > CAPS_COPY)
		n = CAPS_COPY;
	if (!readable(msg + 2, n))
		return;
	for (i = 0; i < n; i++)
		c[i] = msg[2 + i];
	if (btaac_neg.want_aptx && btaac_neg.want_aptx_hd && btaptx_sep_scan_hd(c, n, &sink)) {
		btaac_neg.apt_hd = 1;
	} else if (btaac_neg.have || !(btaac_neg.want_ldac ? btldac_sep_scan(c, n, &sink) : btaac_neg.want_aptx ? btaptx_sep_scan(c, n, &sink) : btaac_sep_scan(c, n, &sink))) {
		return;   /* (with a choice already made only an HD endpoint changes it) */
	} else {
		btaac_neg.apt_hd = 0;
	}
	btaac_neg.aptx = btaac_neg.want_aptx;
	btaac_neg.ldac = btaac_neg.want_ldac;
	btaac_neg.seid = (uint32_t)(info[0] & 0xFCu);
	btaac_neg.object = sink.object;
	btaac_neg.vbr = sink.vbr;
	btaac_neg.max_bps = sink.max_bps;
	btaac_neg.n_chosen++;
	__atomic_store_n(&btaac_neg.have, 1u, __ATOMIC_RELEASE);
}

/* ---- the headphones' codec list (btcodecs.h): read from the same replies, whatever codec is being sent ------------------------------------------------ */

/* The headphones' DISCOVER reply: the endpoints they list. Starts a list for this connection. */
static void cdc_discover(uintptr_t conn, const uint8_t *msg, int len)
{
	volatile BtCdcRec *r = &btaac_cdc;
	uint32_t n, i;

	r->conn = 0;
	if (len <= 3 || len > MAX_MSG)
		return;
	n = (uint32_t)len - 2u;
	if (n > BTCDC_MAX_SEP * 2u)
		n = BTCDC_MAX_SEP * 2u;
	n &= ~1u;
	if (!n || !readable(msg + 2, n))
		return;
	for (i = 0; i < n; i++)
		r->info[i] = msg[2 + i];
	r->nsep = n / 2u;
	r->seen = 0;
	r->mask = 0;
	r->conn = conn;
}

/* The reply to GET_CAPABILITIES for the endpoint the code is asking (its index into the list, read before the code's own handler moves it on). When every
 * listed endpoint has answered, the list is published for the worker. */
static void cdc_caps(uintptr_t conn, const uint8_t *msg, int len)
{
	volatile BtCdcRec *r = &btaac_cdc;
	uint32_t idx = *(const volatile uint8_t *)(conn + BTAAC_C_INDEX), ep, n, i;
	uint8_t c[CAPS_COPY], info[2];

	if (r->conn != conn || (idx & 1u) || (idx >> 1) >= r->nsep || ((r->seen >> (idx >> 1)) & 1u) || len > MAX_MSG)
		return;
	ep = idx >> 1;
	info[0] = r->info[idx];
	info[1] = r->info[idx + 1u];
	n = len > 2 ? (uint32_t)len - 2u : 0u;
	if (n > CAPS_COPY)
		n = CAPS_COPY;
	if (n && readable(msg + 2, n)) {
		for (i = 0; i < n; i++)
			c[i] = msg[2 + i];
		if (btcdc_sep_is_audio_sink(info))
			r->mask |= btcdc_scan_caps(c, n);
	} else if (n) {
		return;   /* unreadable: the list stays unfinished (never stored) */
	}
	r->seen |= 1u << ep;
	if (r->seen != (1u << r->nsep) - 1u)
		return;
	/* a sequence word, odd while the fields are written: the worker takes a copy only when it saw the same even number before and after */
	__atomic_store_n(&r->done_seq, r->done_seq + 1u, __ATOMIC_RELEASE);
	__atomic_thread_fence(__ATOMIC_RELEASE);   /* the odd number is seen before any field changes */
	r->done_lo = btaac_rd32(conn, BTAAC_C_ADDR_LO);
	r->done_hi = btaac_rd32(conn, BTAAC_C_ADDR_HI) & 0xFFFFu;
	r->done_mask = r->mask;
	r->n_done++;
	r->conn = 0;
	__atomic_store_n(&r->done_seq, r->done_seq + 1u, __ATOMIC_RELEASE);
}

static void cdc_note(uintptr_t conn, const uint8_t *msg, int len)
{
	if (len < 2 || !readable(msg, 2) || ((msg[0] >> 2) & 3u) != 0u || (msg[0] & 3u) != MSG_ACCEPT)
		return;
	if ((msg[1] & 0x3Fu) == SIG_DISCOVER)
		cdc_discover(conn, msg, len);
	else if ((msg[1] & 0x3Fu) == SIG_GETCAPS)
		cdc_caps(conn, msg, len);
}

/* A new negotiation for a connection (DISCOVER, from either side): the gates close and everything of the last connection is forgotten. The caller sets the state. */
static void neg_begin(uintptr_t conn, uint32_t init)
{
	btaac_neg.n_discover++;
	btaac_neg.send_gate = 0;
	btaac_neg.pcm_gate = 0;
	btaac_neg.have = 0;
	btaac_neg.aptx = 0;
	btaac_neg.apt_hd = 0;
	btaac_neg.ldac = 0;
	btaac_neg.apt_ready = 0;
	btaac_neg.ep_byte = 0;
	btaac_neg.conn = conn;
	btaac_neg.init = init;
	btaac_neg.acceptor = 0;
	btaac_neg.acc_flags = 0;
	btaac_neg.seq = 0;
	btaac_neg.ts = 0;
	btaac_neg.have_last = 0;
	btaac_neg.ssrc = 0x56544100u + (btaac_neg.n_discover & 0xFFu);
}

/* ---- the acceptor path: the headphones open the stream ---------------------------------------------------------------------- */

/* The bytes handed to the code and the fixed replies. Module data: the frame sender copies what it is given, but never the stack of a frame that is gone. */
/* the code's endpoints 1 (source) and 2 (sink), then ours: 3, an audio source, free */
static const uint8_t k_disc_list[6] = { 0x04, 0x00, 0x08, 0x08, BTAAC_SEID_AAC_BYTE, 0x00 };
static const uint8_t k_disc_list_aptx[6] = { 0x04, 0x00, 0x08, 0x08, BTAPTX_SEID_BYTE, 0x00 };   /* ... or endpoint 4, the aptX source (only the codec the Bluetooth Codec setting names is offered) */
static const uint8_t k_disc_list_ldac[6] = { 0x04, 0x00, 0x08, 0x08, BTLDAC_SEID_BYTE, 0x00 };   /* ... or endpoint 5, the LDAC source */
/* Music Source Only (btaac_neg.src_only): the same lists without the code's endpoint 2, the sink: a device that can be either a source or a speaker finds only the Vita's
 * source side to talk to. */
static const uint8_t k_src_list[2] = { 0x04, 0x00 };
static const uint8_t k_src_list_aac[4] = { 0x04, 0x00, BTAAC_SEID_AAC_BYTE, 0x00 };
static const uint8_t k_src_list_aptx[4] = { 0x04, 0x00, BTAPTX_SEID_BYTE, 0x00 };
static const uint8_t k_src_list_ldac[4] = { 0x04, 0x00, BTLDAC_SEID_BYTE, 0x00 };
static const uint8_t k_caps_reply_ldac[BTLDAC_CONFIG_LEN] = { 0x01, 0x00, 0x07, 0x0A, 0x00, 0xFF, 0x2D, 0x01, 0x00, 0x00, 0xAA, 0x00, 0x10, 0x01 };   /* LDAC, 48 kHz, stereo */
static const uint8_t k_caps_reply_aptx[BTAPTX_CONFIG_LEN] = { 0x01, 0x00, 0x07, 0x09, 0x00, 0xFF, 0x4F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x12 };   /* aptX, 48 kHz, stereo */
static const uint8_t k_caps_reply[BTAAC_CONFIG_LEN] = { 0x01, 0x00, 0x07, 0x08, 0x00, 0x02, 0xC0, 0x00, 0x84, 0x04, 0xE2, 0x00 };   /* MPEG-2 and MPEG-4 LC, 48 kHz, stereo, up to 320 kb/s */
static const uint8_t k_sbc_subst[10] = { 0x01, 0x00, 0x07, 0x06, 0x00, 0x00, 0x11, 0x15, 0x02, 0x35 };   /* SBC 48 kHz joint stereo, 16 blocks, 8 subbands, loudness, bitpool 2-53: what the code takes */

/* What the receive hook does with a command. */
enum { ACT_NONE = 0,     /* the original runs with the message as it came                                              */
       ACT_DONE,         /* answered here: the original does not run                                                   */
       ACT_SUB,          /* the original runs with `sub` instead (endpoint 3 turned into endpoint 1)                    */
       ACT_SETCONF_AAC,  /* ... and, when the code accepted it, the record is made AAC (conf_done)                      */
       ACT_SETCONF_SBC   /* the sink configured the SBC endpoint on a connection we had offered AAC on                  */
};
typedef struct Act {
	uint32_t kind, len, rec_before, int_seid;
	uint8_t  sub[ACT_MAX];
	BtAacSink sink;
} Act;

/* 7 arguments: connection, signalling channel number, format, the first byte, the signal, then either (bytes, length) or (a byte, a byte): the shape of the
 * code's own calls at its DISCOVER, GET_CAPABILITIES and reject senders. */
typedef int (*l2_send7_t)(void *conn, uint32_t cid, const void *fmt, uint32_t b0, uint32_t sig, uintptr_t a, uint32_t b);

/* The signalling channel's number, if we can answer on this connection at all (the sender and the formats known, the channel readable). */
static int can_reply(const void *chan, uint32_t *cid)
{
	if (!btaac_neg.l2_send || !btaac_neg.l2_fmt_acc || !btaac_neg.l2_fmt_rej || !chan || !readable((const uint8_t *)chan + 0x38, 2))
		return 0;
	*cid = *(const volatile uint16_t *)((const uint8_t *)chan + 0x38);
	return 1;
}

static void send_accept(void *connp, uint32_t cid, uint32_t b0, uint32_t sig, const uint8_t *data, uint32_t n)
{
	((l2_send7_t)btaac_neg.l2_send)(connp, cid, (const void *)btaac_neg.l2_fmt_acc, (b0 & 0xF0u) | MSG_ACCEPT, sig, (uintptr_t)data, n);
}

static void send_reject(void *connp, uint32_t cid, uint32_t b0, uint32_t sig, uint32_t cat, uint32_t err)
{
	((l2_send7_t)btaac_neg.l2_send)(connp, cid, (const void *)btaac_neg.l2_fmt_rej, (b0 & 0xF0u) | MSG_REJECT, sig, cat, err);
}

/* A connection the headphones opened and offered endpoint 3 on, in one of the states that name it. */
static inline int acceptor_conn(uintptr_t conn)
{
	return conn == btaac_neg.conn && btaac_neg.acceptor && btaac_neg.state != BTAAC_N_NONE;
}

/* Music Source Only: the reply to a DISCOVER the code would answer itself lists its endpoint 1 alone. Where we cannot answer (the sender or the channel is not known) the code's own
 * reply stands, sink and all (counted, and the worker logs it). */
static void src_discover(void *connp, void *chan, const uint8_t *msg, Act *a)
{
	uint32_t cid;

	if (!btaac_neg.src_only)
		return;
	if (!can_reply(chan, &cid)) {
		btaac_neg.n_src_noreply++;
		return;
	}
	send_accept(connp, cid, msg[0], SIG_DISCOVER, k_src_list, sizeof(k_src_list));
	btaac_neg.n_src_discover++;
	a->kind = ACT_DONE;
}

/* The headphones' DISCOVER: a new negotiation. When AAC is wanted (and the device is not marked) the reply lists endpoint 3 after the code's two. */
static void acc_discover(uintptr_t conn, void *connp, void *chan, const uint8_t *msg, Act *a)
{
	uint32_t cid;

	/* The Vita opened this link and its stream record is in use (not idle): a DISCOVER from the headphones now is a rediscovery on a live connection, the code
	 * answers it as it always did (but for the sink left out of the list when Music Source Only is on) and nothing else of ours changes (taking it over would let SBC packets go to a sink configured for AAC). */
	if (conn == btaac_neg.conn && btaac_neg.init == BTAAC_INIT_VITA && btaac_neg.state != BTAAC_N_NONE && btaac_rd32(conn, BTAAC_C_REC_B) >= 3u) {
		src_discover(connp, chan, msg, a);
		return;
	}
	neg_begin(conn, BTAAC_INIT_HEADSET);
	if (btaac_neg.enable && !btaac_banned(conn) && can_reply(chan, &cid)) {
		const uint8_t *list;
		uint32_t n;

		btaac_neg.acceptor = 1;
		btaac_neg.acc_flags = BTAAC_ACC_ADVERTISED;
		btaac_neg.n_acc_discover++;
		btaac_neg.aptx = btaac_neg.want_aptx;
		btaac_neg.ldac = btaac_neg.want_ldac;
		btaac_neg.ep_byte = btaac_neg.want_ldac ? BTLDAC_SEID_BYTE : btaac_neg.want_aptx ? BTAPTX_SEID_BYTE : BTAAC_SEID_AAC_BYTE;
		__atomic_store_n(&btaac_neg.state, (uint32_t)BTAAC_N_OFFERED, __ATOMIC_RELEASE);
		if (btaac_neg.src_only) {
			list = btaac_neg.want_ldac ? k_src_list_ldac : btaac_neg.want_aptx ? k_src_list_aptx : k_src_list_aac;
			n = sizeof(k_src_list_aac);
			btaac_neg.n_src_discover++;
		} else {
			list = btaac_neg.want_ldac ? k_disc_list_ldac : btaac_neg.want_aptx ? k_disc_list_aptx : k_disc_list;
			n = sizeof(k_disc_list);
		}
		send_accept(connp, cid, msg[0], SIG_DISCOVER, list, n);
		a->kind = ACT_DONE;
		return;
	}
	__atomic_store_n(&btaac_neg.state, (uint32_t)BTAAC_N_NONE, __ATOMIC_RELEASE);   /* the code's own reply: SBC only */
	src_discover(connp, chan, msg, a);
}

/* The headphones' SET_CONFIGURATION for endpoint 3: checked here, then handed to the code as one for its SBC endpoint. */
static void acc_setconf(uintptr_t conn, void *connp, void *chan, const uint8_t *msg, int len, Act *a)
{
	uint8_t c[BTAAC_CONFIG_MAX];
	uint32_t cid, n, cat = 0, err, i;

	if (!can_reply(chan, &cid))
		return;                                 /* the code rejects the unknown endpoint itself */
	if (len < 4 || (uint32_t)len > 4u + BTAAC_CONFIG_MAX) {
		err = BTAAC_ERR_BAD_LENGTH;             /* the code's own limit */
	} else {
		n = (uint32_t)len - 4u;
		if (!readable(msg, (uint32_t)len))
			return;
		for (i = 0; i < n; i++)
			c[i] = msg[4 + i];
		err = btaac_neg.ldac ? btldac_sep_check_config(c, n, &a->sink, &cat) : btaac_neg.aptx ? btaptx_sep_check_config(c, n, &a->sink, &cat) : btaac_sep_check_config(c, n, &a->sink, &cat);
	}
	if (err) {
		btaac_neg.n_acc_reject++;
		send_reject(connp, cid, msg[0], SIG_SETCONF, cat, err);
		a->kind = ACT_DONE;
		return;
	}
	a->sub[0] = msg[0];
	a->sub[1] = msg[1];
	a->sub[2] = BTAAC_SEID_SBC_BYTE;
	a->sub[3] = msg[3];
	for (i = 0; i < sizeof(k_sbc_subst); i++)
		a->sub[4 + i] = k_sbc_subst[i];
	a->len = 4u + (uint32_t)sizeof(k_sbc_subst);
	a->int_seid = msg[3];
	a->rec_before = btaac_rd32(conn, BTAAC_C_REC_B);
	a->kind = ACT_SETCONF_AAC;
}

/* After the code ran on the substitute: if it accepted (the record went from idle to configured, naming the sink's endpoint) the stream is AAC: the record's
 * capability bytes are made the configuration we will send, and the media side takes over. A refusal by the code (the record in use, a state it does not
 * allow) leaves everything as the code made it: the sink saw the code's own reject. */
static void conf_done(uintptr_t conn, const Act *a)
{
	uint8_t *rb = (uint8_t *)(conn + BTAAC_C_REC_B);
	BtAacSink sink = a->sink;
	uint32_t bps, n;

	if (a->rec_before != REC_IDLE || btaac_rd32(conn, BTAAC_C_REC_B) != REC_CONFIGURED || rb[5] != (uint8_t)a->int_seid) {
		btaac_neg.n_acc_stock_no++;
		return;
	}
	sink.seid = (uint8_t)(a->int_seid & 0xFCu);
	bps = btaac_neg.ldac ? 0u : btaac_neg.aptx ? BTAPTX_KBPS * 1000u : btaac_sep_bps(&sink, btaac_neg.kbps);
	btaac_neg.seid = sink.seid;
	btaac_neg.object = sink.object;
	btaac_neg.vbr = sink.vbr;
	btaac_neg.max_bps = sink.max_bps;
	btaac_neg.bps = bps;
	if (btaac_neg.ldac)
		btldac_sep_config((uint8_t *)btaac_neg.config);
	else if (btaac_neg.aptx)
		btaptx_sep_config((uint8_t *)btaac_neg.config);
	else
		btaac_sep_config(&sink, bps, (uint8_t *)btaac_neg.config);
	btaac_neg.config_len = btaac_neg.ldac ? BTLDAC_CONFIG_LEN : btaac_neg.aptx ? BTAPTX_CONFIG_LEN : BTAAC_CONFIG_LEN;
	for (n = 0; n < btaac_neg.config_len; n++)
		rb[6 + n] = btaac_neg.config[n];
	*(volatile uint32_t *)(rb + 0x18) = btaac_neg.config_len;
	btaac_neg.have = 1;
	btaac_neg.n_rewrite++;
	btaac_neg.n_acc_config++;
	btaac_neg.acc_flags |= BTAAC_ACC_CONFIGURED;
	__atomic_store_n(&btaac_neg.state, (uint32_t)BTAAC_N_CONFIGURED, __ATOMIC_RELEASE);
}

/* After the code accepted an SBC configuration of endpoint 1 on a connection that had been offered AAC: it is SBC now. */
static void sbc_done(uintptr_t conn, const Act *a)
{
	if (a->rec_before == REC_IDLE && btaac_rd32(conn, BTAAC_C_REC_B) == REC_CONFIGURED && btaac_neg.state == BTAAC_N_CONFIGURED) {
		btaac_neg.send_gate = 0;
		btaac_neg.pcm_gate = 0;
		btaac_neg.n_acc_sbc++;
		__atomic_store_n(&btaac_neg.state, (uint32_t)BTAAC_N_NONE, __ATOMIC_RELEASE);
	}
}

/* A command the headphones sent. Everything that is not about endpoint 3 (or, for DISCOVER, a new connection) is the code's own. */
static void acceptor(uintptr_t conn, void *connp, void *chan, const uint8_t *msg, int len, uint32_t sig, Act *a)
{
	uint32_t seid, cid, st, i, n, ep = btaac_neg.ep_byte ? btaac_neg.ep_byte : BTAAC_SEID_AAC_BYTE;   /* the endpoint this connection was offered */

	if (sig == SIG_DISCOVER) {
		acc_discover(conn, connp, chan, msg, a);
		return;
	}
	if (len < 3 || !acceptor_conn(conn))
		return;
	st = btaac_neg.state;
	n = (uint32_t)len > ACT_MAX ? ACT_MAX : (uint32_t)len;
	if (!readable(msg, n))
		return;
	seid = msg[2];
	if (sig == SIG_GETCAPS && seid == ep) {
		if (btaac_neg.enable && st != BTAAC_N_BROKEN && can_reply(chan, &cid)) {
			btaac_neg.n_acc_caps++;
			btaac_neg.acc_flags |= BTAAC_ACC_ASKED;
			send_accept(connp, cid, msg[0], SIG_GETCAPS, btaac_neg.ldac ? k_caps_reply_ldac : btaac_neg.aptx ? k_caps_reply_aptx : k_caps_reply,
			            btaac_neg.ldac ? BTLDAC_CONFIG_LEN : btaac_neg.aptx ? BTAPTX_CONFIG_LEN : BTAAC_CONFIG_LEN);
			a->kind = ACT_DONE;
		}
		return;
	}
	if (sig == SIG_SETCONF) {
		if (seid == ep && btaac_neg.enable && (st == BTAAC_N_OFFERED || st == BTAAC_N_CONFIGURED))
			acc_setconf(conn, connp, chan, msg, len, a);
		else if (seid == BTAAC_SEID_SBC_BYTE && (st == BTAAC_N_OFFERED || st == BTAAC_N_CONFIGURED)) {
			a->rec_before = btaac_rd32(conn, BTAAC_C_REC_B);
			a->kind = ACT_SETCONF_SBC;
		}
		return;
	}
	if (seid != ep || (sig != SIG_OPEN && sig != SIG_START && sig != SIG_CLOSE && sig != SIG_SUSPEND && sig != SIG_ABORT))
		return;
	if (!(st == BTAAC_N_CONFIGURED || st == BTAAC_N_BROKEN || (sig == SIG_ABORT && st == BTAAC_N_OFFERED)))
		return;                                 /* not configured as AAC: the code rejects the unknown endpoint itself */
	if ((uint32_t)len != n)
		return;                                 /* (a list of endpoints longer than this is not one we make) */
	for (i = 0; i < n; i++)
		a->sub[i] = msg[i];
	a->sub[2] = BTAAC_SEID_SBC_BYTE;
	a->len = n;
	a->kind = ACT_SUB;
	btaac_neg.n_acc_cmd++;
}

/* Music Source Only, for the commands the codec path (observe above) does not see or does not answer: a DISCOVER on a connection with no codec on offer (the reply without
 * the sink), and a SET_CONFIGURATION that names the sink, the endpoint the reply no longer lists, which is rejected (BAD_ACP_SEID) before the code can make the Vita the
 * receiver. Every other command is the code's: with the sink never configured its stream record stays idle, so an OPEN, START, SUSPEND, CLOSE or ABORT for it gets the code's
 * own state reject, and a GET_CAPABILITIES for it its own (harmless) answer. */
static void src_only_step(uintptr_t conn, void *connp, void *chan, const uint8_t *msg, int len, Act *a)
{
	uint32_t sig, cid;

	if (len < 2 || !readable(msg, 2) || ((msg[0] >> 2) & 3u) != 0u || (msg[0] & 3u) != MSG_COMMAND)
		return;
	sig = msg[1] & 0x3Fu;
	if (sig == SIG_DISCOVER) {
		if (!(btaac_neg.enable || (conn == btaac_neg.conn && btaac_neg.state != BTAAC_N_NONE)))   /* (otherwise the codec path answers it, acc_discover) */
			src_discover(connp, chan, msg, a);
		return;
	}
	if (sig == SIG_SETCONF && len >= 3 && readable(msg, 3) && msg[2] == BTAAC_SEID_SINK_BYTE) {
		if (!can_reply(chan, &cid)) {
			btaac_neg.n_src_noreply++;
			return;
		}
		send_reject(connp, cid, msg[0], SIG_SETCONF, 0u, BTAAC_ERR_BAD_ACP_SEID);
		btaac_neg.n_src_reject++;
		a->kind = ACT_DONE;
	}
}

/* Looks at one received message before the original handles it. Returns 1 if the original is to be followed by the SBC resend. `a` says what else to do. */
static int observe(uintptr_t conn, void *connp, void *chan, const uint8_t *msg, int len, Act *a)
{
	uint32_t sig, mt;

	if (len < 2 || !readable(msg, 2))
		return 0;
	if (((msg[0] >> 2) & 3u) != 0u)           /* a fragment of a longer message: not ours */
		return 0;
	mt = msg[0] & 3u;
	sig = msg[1] & 0x3Fu;
	if (mt == MSG_COMMAND) {
		acceptor(conn, connp, chan, msg, len, sig, a);
		return 0;
	}
	if (mt == MSG_ACCEPT && sig == SIG_DISCOVER) {
		/* a new negotiation for this connection (the code has not asked any endpoint yet) */
		neg_begin(conn, BTAAC_INIT_VITA);
		__atomic_store_n(&btaac_neg.state, (uint32_t)(btaac_neg.enable && !btaac_banned(conn) ? BTAAC_N_PROBING : BTAAC_N_NONE),
		                 __ATOMIC_RELEASE);
		return 0;
	}
	if (conn != btaac_neg.conn)
		return 0;
	if (mt == MSG_ACCEPT && sig == SIG_GETCAPS && btaac_neg.state == BTAAC_N_PROBING) {
		note_caps(conn, msg, len);
		return 0;
	}
	if (mt == MSG_ACCEPT && sig == SIG_SETCONF && btaac_neg.state == BTAAC_N_PENDING) {
		btaac_neg.n_accept++;
		__atomic_store_n(&btaac_neg.state, (uint32_t)BTAAC_N_CONFIGURED, __ATOMIC_RELEASE);
		return 0;
	}
	if ((mt == MSG_REJECT || mt == MSG_GENERAL) && sig == SIG_SETCONF && btaac_neg.state == BTAAC_N_PENDING) {
		btaac_neg.n_reject++;
		btaac_ban_codec(conn);
		__atomic_store_n(&btaac_neg.state, (uint32_t)BTAAC_N_FALLBACK, __ATOMIC_RELEASE);
		return 1;
	}
	return 0;
}

/* The SBC choice the code made, sent again as SET_CONFIGURATION: the same call, from the same thread, as the code's own at the end of its
 * negotiation (text 0xA988: connection, the signalling channel's number, the format, the label, 3, the two endpoints, the bytes, their length).
 * The stream record is put back as it was (the code's state machine never moved: it ignores a reject of SET_CONFIGURATION), the label is
 * the one it used. */
typedef int (*l2_send_t)(void *conn, uint32_t cid, const void *fmt, uint32_t label, uint32_t sig, uint32_t acp, uint32_t intr,
                         const void *caps, uint32_t len);

static void resend_sbc(void *connp, const void *chan)
{
	uintptr_t conn = (uintptr_t)connp;
	uint8_t *rb = (uint8_t *)(conn + BTAAC_C_REC_B);
	uint32_t cid, n;

	if (!btaac_neg.l2_send || !btaac_neg.l2_fmt || !chan || !readable((const uint8_t *)chan + 0x38, 2) || btaac_neg.sbc_len > BTAAC_CAPS_BUF ||
	    btaac_rd32(conn, BTAAC_C_REC_B) != 4u) {
		btaac_neg.n_resend_none++;
		return;
	}
	cid = *(const volatile uint16_t *)((const uint8_t *)chan + 0x38);
	for (n = 0; n < BTAAC_CAPS_BUF; n++)
		rb[6 + n] = btaac_neg.sbc_caps[n];
	rb[5] = (uint8_t)btaac_neg.sbc_seid;
	*(volatile uint32_t *)(rb + 0x18) = btaac_neg.sbc_len;
	((l2_send_t)btaac_neg.l2_send)(connp, cid, (const void *)btaac_neg.l2_fmt, rb[0x20], 3u, rb[5], rb[4], rb + 6,
	                                         btaac_neg.sbc_len);
	btaac_neg.n_resend++;
}

int btaac_disp_hook(void *conn, void *chan, const uint8_t *msg, int len)
{
	tai_hook_ref_t ref;
	uint32_t spins = REF_SPINS;
	uintptr_t c = (uintptr_t)conn;
	int ret = 0, resend = 0;
	Act a;

	/* The hook goes live a moment before taiHEN stores the reference. */
	while ((ref = btaac_disp_ref) == 0 && --spins)
		;
	if (!ref)
		return 0;   /* never set: the original is skipped for this one message, in the first millisecond after install only */

	__atomic_fetch_add(&btaac_disp_in, 1u, __ATOMIC_ACQUIRE);
	a.kind = ACT_NONE;
	if (btaac_cdc.on && btaac_conn_ok(c))
		cdc_note(c, msg, len);
	if (btaac_neg.src_only && btaac_conn_ok(c))
		src_only_step(c, conn, chan, msg, len, &a);
	if (a.kind != ACT_DONE && btaac_conn_ok(c) && (btaac_neg.enable || (c == btaac_neg.conn && btaac_neg.state != BTAAC_N_NONE)))
		resend = observe(c, conn, chan, msg, len, &a);
	if (a.kind != ACT_DONE) {
		if (a.kind == ACT_SUB || a.kind == ACT_SETCONF_AAC)
			ret = TAI_CONTINUE(int, ref, conn, chan, a.sub, (int)a.len);
		else
			ret = TAI_CONTINUE(int, ref, conn, chan, msg, len);
		if (a.kind == ACT_SETCONF_AAC)
			conf_done(c, &a);
		else if (a.kind == ACT_SETCONF_SBC)
			sbc_done(c, &a);
	}
	if (resend)
		resend_sbc(conn, chan);
	__atomic_fetch_sub(&btaac_disp_in, 1u, __ATOMIC_RELEASE);
	return ret;
}

/* ---- H3: the media packet send routine ------------------------------------------------------------------------------------ */

static uint8_t h_au[BTAAC_AU_MAX];
static uint8_t h_apt[BTAPTX_PACKET_BYTES];
static uint8_t h_apt_hd[BTAPTX_HD_BLOCK_BYTES];
static uint8_t h_pkt_hd[BTAPTX_HD_PACKET_MAX];
static uint8_t h_latm[BTAAC_LATM_MAX];
static uint8_t h_pkt[BTAAC_LATM_MAX + BTAAC_RTP_HDR];

/* The largest packet the sink's L2CAP channel takes, as the Bluetooth code keeps it (the media channel's MTU), made safe. Always inlined into the hook: the hook's object
 * may call nothing (tools/check_bteq_leaf.sh), and btaac_media_mtu below is a second caller. */
static inline __attribute__((always_inline)) uint32_t media_mtu(uintptr_t conn)
{
	uintptr_t chan = *(const volatile uintptr_t *)(conn + BTAAC_C_MEDIA_CH);

	if (!chan || !readable((const void *)(chan + 8u), 4))
		return BTAAC_MTU_DEFAULT;
	return btaac_mtu_sane(*(const volatile uint32_t *)(chan + 8u));
}

uint32_t btaac_media_mtu(uintptr_t conn)
{
	return media_mtu(conn);
}

/* One access unit of the engine, if the pacing says one is due and there is one, as one or more RTP packets sent through the Bluetooth code's own routine. */
static int send_aac(void *connp, tai_hook_ref_t ref, uint32_t flag)
{
	uintptr_t conn = (uintptr_t)connp;
	BtAacPace *pace = (BtAacPace *)&btaac_neg.pace;
	uint32_t au_seq = 0, au_len, latm_len, mtu, pieces, k, keep, depth, dropped = 0, preroll = btaac_neg.preroll;
	int ret = (int)BAD_STATE, dec;

	if (btaac_rd32(conn, BTAAC_C_REC_B) != BTAAC_REC_OPEN)   /* the routine's own condition for sending at all */
		return ret;
	keep = btaac_pace_keep(btaac_neg.keep, preroll);   /* the pre-roll counts: the trim never leaves fewer than it asked to be ready */
	if (keep)   /* what waits in the engine is part of the delay: no more than the latency setting allows, the oldest go first */
		dropped = btt_aac_au_trim(keep);
	depth = btt_aac_au_depth();
	if (dropped) {
		btaac_neg.n_trim++;
		btaac_neg.n_trimmed_units += dropped;
		btaac_neg.last_au += dropped;   /* the timeline stays continuous: what was dropped never existed for the sink (the sink catches up) */
	}
	dec = btaac_pace_step(pace, preroll, depth);
	if (dec != BTAAC_PACE_SEND) {
		if (dec == BTAAC_PACE_DRY)
			btaac_neg.n_no_au++;
		else if (dec == BTAAC_PACE_WAIT_SPACING)
			btaac_neg.n_spaced++;
		else
			btaac_neg.n_waiting++;
		return ret;
	}
	au_len = btt_aac_pop_au(h_au, sizeof(h_au), &au_seq);
	if (!au_len) {
		btaac_neg.n_no_au++;
		return ret;
	}
	btaac_pace_sent(pace);
	latm_len = btaac_latm_wrap(h_latm, sizeof(h_latm), h_au, au_len);
	if (!latm_len) {
		btaac_neg.n_wrap_err++;
		return ret;
	}
	mtu = media_mtu(conn);
	btaac_neg.ts = btaac_ts_next(btaac_neg.ts, btaac_neg.last_au, au_seq, (int)btaac_neg.have_last);
	btaac_neg.last_au = au_seq;
	btaac_neg.have_last = 1;
	btaac_neg.last_mtu = mtu;
	btaac_neg.last_au_len = au_len;
	btaac_neg.last_latm_len = latm_len;
	if (au_len > btaac_neg.max_au_len)
		btaac_neg.max_au_len = au_len;
	pieces = btaac_latm_pieces(latm_len, mtu);
	if (pieces > 1u) {   /* the safety net: the encoder is held to one packet (btaac_enc_limit), so this is a sink with a smaller limit than the session was made for, or a failed cap */
		btaac_neg.n_over++;
		btaac_neg.n_back += pieces - 1u;
	}
	/* Every piece goes out in this call, one after the other: the next block's call never holds the rest of a unit back (a gap between two pieces of one unit is what a
	 * headset plays as a stutter). */
	for (k = 0; k < pieces; k++) {
		uint32_t n = btaac_rtp_piece(h_pkt, h_latm, latm_len, mtu, k, (uint16_t)btaac_neg.seq, btaac_neg.ts, btaac_neg.ssrc);

		btaac_neg.seq = (btaac_neg.seq + 1u) & 0xFFFFu;
		/* The code counts one packet in flight for this call (it adds one when we return >= 0) and takes one away when the packet that carries `flag` has been
		 * completed on the link (the frame sender's "C#" format files `flag` with the packet, the completion handler runs the count down when it completes). A unit cut into
		 * pieces is several packets: only the last carries the flag, so the count goes up once and down once. */
		ret = TAI_CONTINUE(int, ref, connp, h_pkt, n, k + 1u == pieces ? flag : 0u);
		if (ret < 0) {
			btaac_neg.n_send_err++;
			break;
		}
		btaac_neg.n_pieces++;
	}
	if (ret >= 0)
		btaac_neg.n_sent++;
	return ret;
}

/* LDAC: the packets the engine has made since the last call, at most BTLDAC_SEND_MAX, each its own L2CAP packet (RTP header, one payload header byte, whole frames: never cut),
 * all sent in this call one after the other, only the last carrying the code's flag (so its in-flight count goes up and down once for the call, as AAC's pieces). A block
 * is 4 encoder calls: 330 kb/s makes a packet for 1.5 blocks, 660 kb/s one for 0.75, so a call sends 0, 1 or 2. The ring is held to the latency setting's bound first (the
 * oldest go), and a call with nothing ready sends nothing (the next one finds more). The media channel's limit was checked when the session began; a smaller one now
 * sends nothing and is counted (n_ldac_mtu). */
static uint8_t h_ldac_frames[BTLDAC_FRAMES_BYTES];
static uint8_t h_ldac_pkt[BTLDAC_SEND_MAX][BTLDAC_PKT_MAX];
static int send_ldac(void *connp, tai_hook_ref_t ref, uint32_t flag)
{
	uintptr_t conn = (uintptr_t)connp;
	uint32_t keep = btaac_neg.ldac_keep, n = 0, k, len, nf, seq = 0, mtu, lens[BTLDAC_SEND_MAX], dropped;
	int ret = (int)BAD_STATE;

	if (btaac_rd32(conn, BTAAC_C_REC_B) != BTAAC_REC_OPEN)   /* the routine's own condition for sending at all */
		return ret;
	mtu = media_mtu(conn);
	btaac_neg.last_mtu = mtu;
	if (!btldac_mtu_ok(mtu)) {
		btaac_neg.n_ldac_mtu++;
		return ret;
	}
	if (keep) {   /* what waits in the engine is part of the delay */
		dropped = btt_ldac_pkt_trim(keep);
		if (dropped) {
			btaac_neg.n_ldac_trim += dropped;
			btaac_neg.n_trim++;
		}
	}
	while (n < BTLDAC_SEND_MAX) {
		len = btt_ldac_pop_pkt(h_ldac_frames, sizeof(h_ldac_frames), &nf, &seq);
		if (!len)
			break;
		lens[n] = btldac_packet(h_ldac_pkt[n], sizeof(h_ldac_pkt[n]), h_ldac_frames, len, nf, (uint16_t)btaac_neg.seq, btaac_neg.ts, btaac_neg.ssrc);
		if (!lens[n]) {
			btaac_neg.n_wrap_err++;
			break;
		}
		btaac_neg.seq = (btaac_neg.seq + 1u) & 0xFFFFu;
		btaac_neg.ts += btldac_ts_step(nf);
		btaac_neg.last_au_len = len;
		n++;
	}
	if (!n) {
		btaac_neg.n_ldac_dry++;
		btaac_neg.n_no_au++;
		return ret;
	}
	if (n > 1u) {
		btaac_neg.n_ldac_calls2++;
		btaac_neg.n_back += n - 1u;
	}
	for (k = 0; k < n; k++) {
		ret = TAI_CONTINUE(int, ref, connp, h_ldac_pkt[k], lens[k], k + 1u == n ? flag : 0u);
		if (ret < 0) {
			btaac_neg.n_send_err++;
			break;
		}
		btaac_neg.n_pieces++;
		btaac_neg.n_ldac_pkts++;
	}
	if (ret >= 0) {
		btaac_neg.n_sent++;
		btaac_neg.n_ldac_calls++;
	}
	return ret;
}

/* aptX HD (experimental): the block's 128 frames of 6 bytes, sent in as few RTP packets as the media channel's limit allows, one packet when it takes the whole 780 bytes,
 * otherwise two or more of whole frames, the first ones a frame longer (btaptx_hd_pieces; a packet never holds part of a frame, and the public implementations fill a
 * packet to the limit the same way). Every piece goes out in this call, one after the other, as AAC's; only the last carries the code's flag, so the in-flight count goes up
 * and down once for the block. A limit too small for 16 frames to a packet sends nothing (counted in n_over). */
static int send_aptx_hd(void *connp, tai_hook_ref_t ref, uint32_t flag, uint32_t mtu)
{
	uint32_t pieces = btaptx_hd_pieces(mtu), n, k, i, frames, len, off = 0;
	int ret = (int)BAD_STATE;

	if (!pieces) {
		btaac_neg.n_over++;
		return ret;
	}
	n = btt_aptx_encode_hd(btaac_apt_pcm, h_apt_hd);
	if (n != BTAPTX_HD_BLOCK_BYTES) {
		btaac_neg.n_wrap_err++;
		return ret;
	}
	if (pieces > 1u)
		btaac_neg.n_back += pieces - 1u;
	for (k = 0; k < pieces; k++) {
		frames = btaptx_hd_piece_frames(k, pieces);
		len = frames * BTAPTX_HD_FRAME_BYTES;
		btaptx_hd_rtp(h_pkt_hd, (uint16_t)btaac_neg.seq, btaac_neg.ts);
		for (i = 0; i < len; i++)
			h_pkt_hd[BTAPTX_HD_RTP_HDR + i] = h_apt_hd[off + i];
		off += len;
		btaac_neg.seq = (btaac_neg.seq + 1u) & 0xFFFFu;
		btaac_neg.ts += frames * BTAPTX_HD_FRAME_PCM;   /* the timestamp counts the frames of sound sent */
		ret = TAI_CONTINUE(int, ref, connp, h_pkt_hd, BTAPTX_HD_RTP_HDR + len, k + 1u == pieces ? flag : 0u);
		if (ret < 0) {
			btaac_neg.n_send_err++;
			break;
		}
		btaac_neg.n_pieces++;
	}
	if (ret >= 0) {
		btaac_neg.n_sent++;
		btaac_neg.last_au_len = n;
	}
	return ret;
}

/* aptX: the block the sender's hook left (btaac_pcm_block), encoded here and sent as the packet's whole payload: no RTP header, no payload header (the configuration
 * carries no content protection, so no protection byte either). Called by the Bluetooth code once per block, in the sender's thread, after its own gating: a block it
 * dropped never gets here, so the encoder only ever sees what is sent. One packet per call, as SBC: the code's in-flight count and the latency limit keep their meaning. */
static int send_aptx(void *connp, tai_hook_ref_t ref, uint32_t flag)
{
	uintptr_t conn = (uintptr_t)connp;
	uint32_t n;
	int ret = (int)BAD_STATE;

	if (btaac_rd32(conn, BTAAC_C_REC_B) != BTAAC_REC_OPEN)   /* the routine's own condition for sending at all */
		return ret;
	if (!btaac_neg.apt_ready) {
		btaac_neg.n_no_au++;
		return ret;
	}
	btaac_neg.apt_ready = 0;
	btaac_neg.last_mtu = media_mtu(conn);
	if (btaac_neg.apt_hd)
		return send_aptx_hd(connp, ref, flag, btaac_neg.last_mtu);
	if (!btaptx_packet_fits(btaac_neg.last_mtu)) {   /* a sink whose limit is under 512 bytes: there is no way to cut a packet of this codec */
		btaac_neg.n_over++;
		return ret;
	}
	n = btt_aptx_encode(btaac_apt_pcm, h_apt);
	if (!n) {
		btaac_neg.n_wrap_err++;
		return ret;
	}
	ret = TAI_CONTINUE(int, ref, connp, h_apt, n, flag);
	if (ret < 0) {
		btaac_neg.n_send_err++;
		return ret;
	}
	btaac_neg.n_sent++;
	btaac_neg.n_pieces++;
	btaac_neg.last_au_len = n;
	return ret;
}

int btaac_send_hook(void *conn, const void *buf, uint32_t len, uint32_t flag)
{
	tai_hook_ref_t ref;
	uint32_t spins = REF_SPINS, st;
	uintptr_t c = (uintptr_t)conn;
	int ret;

	/* The hook goes live a moment before taiHEN stores the reference. */
	while ((ref = btaac_send_ref) == 0 && --spins)
		;
	if (!ref)
		return (int)BAD_STATE;   /* never set: no call through it; the caller copes with the error (first millisecond after install only) */

	__atomic_fetch_add(&btaac_send_in, 1u, __ATOMIC_ACQUIRE);
	st = btaac_neg.state;
	if (c == btaac_neg.conn && c && (st == BTAAC_N_PENDING || st == BTAAC_N_CONFIGURED || st == BTAAC_N_BROKEN)) {
		/* A stream configured (or being configured) as AAC must never be sent the code's SBC packets: it is the engine's packets, or nothing. */
		if (st == BTAAC_N_CONFIGURED && btaac_neg.send_gate)
			ret = btaac_neg.ldac ? send_ldac(conn, ref, flag) : btaac_neg.aptx ? send_aptx(conn, ref, flag) : send_aac(conn, ref, flag);
		else {
			btaac_neg.n_swallowed++;
			ret = (int)BAD_STATE;
		}
	} else
		ret = TAI_CONTINUE(int, ref, conn, buf, len, flag);
	__atomic_fetch_sub(&btaac_send_in, 1u, __ATOMIC_RELEASE);
	return ret;
}
