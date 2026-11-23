/*
 * test_btevlog.c - host test of the link log (kernel/btevlog_core.h, the hook-side routine kernel/btevlog_hook.c, the worker side kernel/btevlog.c), compiled in as they are.
 *
 *   make test      (cc -Itest/host test/test_btevlog.c, then run)
 *
 * What is proved:
 *   - the reason names (the ones a bug report is about: 0x05, 0x08, 0x13, 0x16, 0x22, 0x3D) and that a code without a name is printed by number;
 *   - the ring: records come out in order and whole, a slot being written is not taken, records overwritten before they were read are counted;
 *   - the hook-side routine stores a record only for a whole, correctly sized event of the six kinds (and only for an ACL link, and only while on), reads nothing beyond the
 *     event's length, and puts the address and handle where the logic expects them;
 *   - who opened a link (a connection request first = the remote, none = the Vita), a failed connection attempt, the duration, an ending of a handle the log never saw,
 *     authentication and encryption counts, a handle reused, a full table;
 *   - the profiles are noted in the order the channels appear, with the audio PSM's first channel as signalling and its second as media, the RFCOMM profile tag as hsp or hfp,
 *     the remote-asked direction; a channel that closed and reopened is noted again; a slot whose address is not a known link is never read for profiles;
 *   - the log lines: the example of a link ending 41.2 s after the remote opened it, the address shown by its first three bytes only, the failed attempt, authentication failures;
 *   - a reconnect loop: thirty endings of the same kind are one line, one summary and a few "up" lines at most; endings that differ are capped per minute and per start, the cap is
 *     said once, the held-back lines are summed up once; nothing is written after the per-start limit; the lines stay well under the log's size;
 */
#include "btevlog_fixture.h"

/* ---- tests --------------------------------------------------------------------------------- */

static void test_names(void)
{
	CHECK(!strcmp(btel_reason_name(0x13), "remote user terminated"), "0x13");
	CHECK(!strcmp(btel_reason_name(0x08), "connection timeout"), "0x08");
	CHECK(!strcmp(btel_reason_name(0x16), "local host terminated"), "0x16");
	CHECK(!strcmp(btel_reason_name(0x05), "authentication failure"), "0x05");
	CHECK(!strcmp(btel_reason_name(0x22), "LMP response timeout"), "0x22");
	CHECK(!strcmp(btel_reason_name(0x3D), "connection terminated due to MIC failure"), "0x3D");
	CHECK(!strcmp(btel_reason_name(0xEE), ""), "a code with no name is empty");
	for (unsigned c = 0; c < 256; c++)
		CHECK(strlen(btel_reason_name(c)) < 44, "name too long for a log line: 0x%02x", c);
	char b[64];
	el_reason_text(b, sizeof(b), 0x13);
	CHECK(!strcmp(b, "remote user terminated (0x13)"), "'%s'", b);
	el_reason_text(b, sizeof(b), 0xEE);
	CHECK(!strcmp(b, "error 0xee"), "'%s'", b);
	puts("  ok: reason names");
}

static void test_ring(void)
{
	BtelRing r;
	BtelRec o;
	uint32_t tail = 0, lost = 0;

	memset(&r, 0, sizeof(r));
	CHECK(!btel_take(&r, &tail, &o, &lost), "empty ring gives nothing");
	for (uint32_t i = 0; i < 10; i++)
		btel_put(&r, 1000 + i, BTEL_K_DISC, i, i * 2, i * 3);
	for (uint32_t i = 0; i < 10; i++) {
		CHECK(btel_take(&r, &tail, &o, &lost), "record %u", i);
		CHECK(o.t == 1000 + i && o.kind == BTEL_K_DISC && o.w1 == i && o.w2 == i * 2 && o.w3 == i * 3, "fields of %u", i);
	}
	CHECK(!btel_take(&r, &tail, &o, &lost) && !lost, "all taken, none lost");

	/* a slot claimed but not yet published is not taken, and is taken once it is */
	{
		uint32_t n = __atomic_fetch_add(&r.head, 1u, __ATOMIC_ACQ_REL);

		CHECK(!btel_take(&r, &tail, &o, &lost) && tail == n, "unpublished slot waits");
		r.e[n & (BTEL_RING_N - 1)].kind = BTEL_K_AUTH;
		r.e[n & (BTEL_RING_N - 1)].seq = n + 1;
		CHECK(btel_take(&r, &tail, &o, &lost) && o.kind == BTEL_K_AUTH, "published slot is taken");
	}
	/* more than the ring holds: the oldest are lost and counted, the newest come out in order */
	for (uint32_t i = 0; i < BTEL_RING_N + 5; i++)
		btel_put(&r, i, BTEL_K_ENC, i, 0, 0);
	CHECK(btel_take(&r, &tail, &o, &lost), "after an overrun");
	CHECK(lost == 5 && o.w1 == 5, "lost %u, first %u", lost, o.w1);
	for (uint32_t i = 6; i < BTEL_RING_N + 5; i++) {
		CHECK(btel_take(&r, &tail, &o, &lost) && o.w1 == i, "in order after the overrun: %u", i);
	}
	CHECK(!btel_take(&r, &tail, &o, &lost), "drained");
	puts("  ok: ring");
}

static void test_hook(void)
{
	BtelRec o;
	uint32_t tail = 0, lost = 0;

	reset();
	ev_conn_cmp(ADDR, 0, 0x2A, 1);
	CHECK(btevlog_ring.head == 0, "nothing is noted while off");
	btevlog_on = 1;
	ev_conn_cmp(ADDR, 0, 0x2A, 1);
	CHECK(btel_take(&btevlog_ring, &tail, &o, &lost) && o.kind == BTEL_K_CONN_CMP, "connection complete");
	CHECK(o.w1 == ADDR_LO && o.w2 == ADDR_HI && (o.w3 & 0xFF) == 0 && ((o.w3 >> 8) & 0xFFF) == 0x2A, "its fields: %08x %04x %08x", o.w1, o.w2, o.w3);
	ev_conn_cmp(ADDR, 0, 0x2B, 0);
	CHECK(!btel_take(&btevlog_ring, &tail, &o, &lost), "a voice link's connection complete is not noted");
	ev_conn_req(ADDR);
	CHECK(btel_take(&btevlog_ring, &tail, &o, &lost) && o.kind == BTEL_K_CONN_REQ && o.w1 == ADDR_LO && o.w2 == ADDR_HI, "connection request");
	ev_disc(0, 0x2A, 0x13);
	CHECK(btel_take(&btevlog_ring, &tail, &o, &lost) && o.kind == BTEL_K_DISC && (o.w1 & 0xFFF) == 0x2A && o.w2 == 0x13, "disconnection complete");
	ev_auth(5, 0x2A);
	CHECK(btel_take(&btevlog_ring, &tail, &o, &lost) && o.kind == BTEL_K_AUTH && (o.w1 >> 16) == 5, "authentication complete");
	ev_enc(0, 0x2A, 1);
	CHECK(btel_take(&btevlog_ring, &tail, &o, &lost) && o.kind == BTEL_K_ENC && o.w2 == 1, "encryption change");
	ev_role(ADDR, 1);
	CHECK(btel_take(&btevlog_ring, &tail, &o, &lost) && o.kind == BTEL_K_ROLE && ((o.w3 >> 8) & 0xFF) == 1, "role change");

	/* wrong sizes and other events: nothing; and nothing is read beyond the length given */
	{
		uint8_t e[16] = { 0x05, 4, 0, 0x2A, 0, 0x13 };
		uint8_t guard[2] = { 0, 0 };

		(void)guard;
		btevlog_note_hci(e, 5);       /* cut short */
		btevlog_note_hci(e, 7);       /* longer than its own length says */
		btevlog_note_hci(e, 2);
		btevlog_note_hci(e, 0);
		btevlog_note_hci(0, 6);
		e[1] = 3;
		btevlog_note_hci(e, 5);       /* a length that is not a disconnection's */
		e[0] = 0x0E; e[1] = 4;
		btevlog_note_hci(e, 6);       /* command complete: not ours */
		e[0] = 0x13; e[1] = 5;
		btevlog_note_hci(e, 7);
	}
	CHECK(!btel_take(&btevlog_ring, &tail, &o, &lost), "malformed and unrelated events are not noted");
	puts("  ok: hook-side routine");
}

static void pass_all(void)
{
	btt_btevlog_pass(1);
}

static void test_logic_lines(void)
{
	reset();
	pass_all();                                   /* the first pass: hook in, nothing noted yet */
	CHECK(btevlog_on == 1 && g_evt_calls > 0, "the hook is asked for and noting is on");

	/* the remote opens the link; profiles come up over the next second; it ends 41.2 s after it came up */
	tick(1000);
	ev_conn_req(ADDR);
	ev_conn_cmp(ADDR, 0, 0x12, 1);
	ev_auth(0, 0x12);
	ev_enc(0, 0x12, 1);
	pass_all();
	CHECK(has_line("link #1 11:22:33:xx:xx:xx up: opened by the remote"), "opened line");
	CHECK(btt_btevlog_fast() == 1, "a young link asks for the faster worker");
	slot_up(2, ADDR_LO, ADDR_HI);
	chan(2, 0, 1, 0, 0);                          /* sdp, asked for by the Vita */
	tick(100);
	pass_all();
	chan(2, 1, 3, 0x10000u, 2);                   /* the headset path's rfcomm, asked for by the remote */
	chan(2, 2, 0x19, 0, 2);                       /* a2dp signalling */
	tick(100);
	pass_all();
	chan(2, 3, 0x19, 0, 2);                       /* a2dp media */
	chan(2, 4, 0x17, 0, 2);                       /* avrcp */
	tick(100);
	pass_all();
	chan(2, 4, 0, 0, 0);                          /* avrcp closes and opens again */
	pass_all();
	chan(2, 4, 0x17, 0, 0);
	pass_all();
	tick(41000);
	ev_disc(0, 0x12, 0x13);
	pass_all();
	CHECK(count_lines(" ended after ") == 1, "one ending line");
	CHECK(has_line("link #1 11:22:33:xx:xx:xx ended after 41.3 s: remote user terminated (0x13); opened by the remote; "
	               "profiles: sdp, hsp, a2dp-sig, a2dp-media, avrcp, avrcp; auth ok, encrypted"), "the ending line");
	for (int i = 0; i < g_nlog; i++)
		CHECK(!strstr(g_log[i], "44:55:66") && !strstr(g_log[i], "66:55"), "the last three bytes of the address never appear: %s", g_log[i]);

	/* the Vita opens a link: no request came first; the headset hangs up before the link is up (failure) */
	g_nlog = 0;
	tick(5000);
	ev_conn_cmp(ADDR, 0, 0x13, 1);
	tick(8000);
	ev_disc(0, 0x13, 0x08);
	ev_conn_cmp(ADDR, 4, 0, 1);
	ev_conn_req(ADDR);
	ev_conn_cmp(ADDR, 0x05, 0, 1);
	pass_all();
	CHECK(has_line("opened by the Vita"), "no request first: the Vita opened it");
	CHECK(has_line("ended after 8.0 s: connection timeout (0x08); opened by the Vita; profiles: none seen; auth not seen, enc not seen"), "timeout ending");
	CHECK(has_line("link attempt to 11:22:33:xx:xx:xx failed: page timeout (0x04)"), "failed attempt by the Vita");
	CHECK(has_line("link attempt from 11:22:33:xx:xx:xx failed: authentication failure (0x05)"), "failed attempt by the remote");

	/* an ending of a handle never seen, an authentication failure and an encryption turned off */
	g_nlog = 0;
	tick(61000);                                  /* a new minute: the window's cap is not what is tested here */
	ev_disc(0, 0x77, 0x16);
	ev_conn_cmp(ADDR, 0, 0x20, 1);
	ev_auth(0x05, 0x20);
	ev_enc(0, 0x20, 0);
	ev_disc(1, 0x20, 0x16);                       /* a failed disconnect command: the link is still up */
	pass_all();
	CHECK(has_line("link handle 0x077 ended: local host terminated (0x16)"), "unknown handle");
	CHECK(has_line("authentication failed: authentication failure (0x05)"), "authentication failure line");
	CHECK(has_line("encryption turned off"), "encryption off line");
	CHECK(count_lines(" ended after ") == 0, "a failed disconnect is not an ending");
	puts("  ok: link lines");
}

static void test_state(void)
{
	BtelState s;
	BtelOut o;
	BtelRec r;

	btel_init(&s);
	/* a request that is never completed is forgotten after 30 s: a later connection is the Vita's */
	memset(&r, 0, sizeof(r));
	r.kind = BTEL_K_CONN_REQ; r.w1 = ADDR_LO; r.w2 = ADDR_HI;
	btel_apply(&s, &r, 1000000, 1, &o);
	r.kind = BTEL_K_CONN_CMP; r.w3 = 0x2A00u;
	btel_apply(&s, &r, 1000000 + 40000000u, 41, &o);
	CHECK(o.kind == BTEL_O_OPEN && o.e.opener == BTEL_OPEN_VITA, "a stale request is not believed");
	/* a request for another address does not make this one the remote's */
	r.kind = BTEL_K_CONN_REQ; r.w1 = 0x01020304u; r.w2 = 0x0506u;
	btel_apply(&s, &r, 50000000, 50, &o);
	r.kind = BTEL_K_CONN_CMP; r.w1 = ADDR_LO; r.w2 = ADDR_HI; r.w3 = 0x2B00u;
	btel_apply(&s, &r, 51000000, 51, &o);
	CHECK(o.e.opener == BTEL_OPEN_VITA, "another device's request");
	/* a handle reused before its ending was seen: one entry, the new link */
	r.w3 = 0x2A00u;
	btel_apply(&s, &r, 52000000, 52, &o);
	{
		int n = 0;
		for (int i = 0; i < BTEL_ACTIVE; i++)
			n += s.link[i].used && s.link[i].handle == 0x2A;
		CHECK(n == 1, "handle reuse keeps one entry (%d)", n);
	}
	/* a full table drops the oldest */
	for (uint32_t h = 1; h <= BTEL_ACTIVE + 2; h++) {
		r.w3 = h << 8;
		btel_apply(&s, &r, 60000000 + h * 1000, 60, &o);
	}
	CHECK(btel_by_handle(&s, BTEL_ACTIVE + 2) && !btel_by_handle(&s, 1), "the oldest was dropped, the newest kept");
	/* the role is kept */
	r.kind = BTEL_K_ROLE; r.w3 = 1u << 8;
	btel_apply(&s, &r, 70000000, 70, &o);
	CHECK(btel_by_addr(&s, ADDR_LO, ADDR_HI)->role == 2, "peripheral role");
	/* the history holds the newest 16 */
	btel_init(&s);
	for (uint32_t i = 0; i < 40; i++) {
		r.kind = BTEL_K_CONN_CMP; r.w3 = (i + 1) << 8;
		btel_apply(&s, &r, 1000000 * i, i, &o);
		r.kind = BTEL_K_DISC; r.w1 = i + 1; r.w2 = 0x13;
		btel_apply(&s, &r, 1000000 * i + 500000, i, &o);
		r.w1 = ADDR_LO;
	}
	CHECK(s.hist_n == 40 && s.hist[(s.hist_n - 1) % BTEL_HIST].id == 40 && s.hist[(s.hist_n - 1) % BTEL_HIST].dur_ds == 5, "history keeps counting; newest is link 40, 0.5 s");
	puts("  ok: link state");
}

static void test_profiles(void)
{
	reset();
	pass_all();
	ev_conn_cmp(ADDR, 0, 0x30, 1);
	pass_all();
	slot_up(0, 0x01020304u, 0x0506u);             /* a slot of another device: never read */
	chan(0, 0, 1, 0, 0);
	slot_up(5, ADDR_LO, ADDR_HI);
	chan(5, 7, 0x19, 0, 0);
	chan(5, 2, 3, 1, 0);                          /* hands-free tag */
	chan(5, 3, 3, 0x20000u, 0);                   /* some other rfcomm */
	chan(5, 4, 0x1B, 0, 0);
	chan(5, 5, 0x13, 0, 0);
	chan(5, 6, 0x99, 0, 0);
	pass_all();
	{
		const BtelLink *l = btel_by_handle(&el_st, 0x30);

		CHECK(l && l->nprof == 6, "six channels noted (%d)", l ? l->nprof : -1);
		/* found in channel order within one pass: 2 hfp, 3 rfcomm, 4 browse, 5 hid, 6 other, 7 a2dp-sig */
		CHECK(BTEL_PF_TOK(l->prof[0]) == BTEL_P_HFP && BTEL_PF_TOK(l->prof[1]) == BTEL_P_RFCOMM && BTEL_PF_TOK(l->prof[2]) == BTEL_P_AVRCP_BROWSE &&
		      BTEL_PF_TOK(l->prof[3]) == BTEL_P_HID && BTEL_PF_TOK(l->prof[4]) == BTEL_P_OTHER && BTEL_PF_TOK(l->prof[5]) == BTEL_P_A2DP_SIG, "tokens in channel order");
	}
	/* a second audio channel is the media; a reopened one is noted again; a pass with nothing new notes nothing */
	chan(5, 8, 0x19, 0, 2);
	pass_all();
	pass_all();
	{
		const BtelLink *l = btel_by_handle(&el_st, 0x30);

		CHECK(l->nprof == 7 && BTEL_PF_TOK(l->prof[6]) == BTEL_P_A2DP_MEDIA && (l->prof[6] & BTEL_PF_DIR), "media, asked for by the remote");
		chan(5, 8, 0, 0, 0);
		pass_all();
		chan(5, 8, 0x19, 0, 0);
		pass_all();
		CHECK(l->nprof == 8 && BTEL_PF_TOK(l->prof[7]) == BTEL_P_A2DP_MEDIA, "a reopened channel is noted again");
	}
	/* more profiles than the record holds: the extra are dropped, not written past it */
	for (int k = 9; k < 16; k++)
		chan(5, k, 0x17, 0, 0);
	for (int k = 9; k < 16; k++) {
		chan(5, k, 0, 0, 0);
		pass_all();
		chan(5, k, 0x17, 0, 0);
		pass_all();
	}
	CHECK(btel_by_handle(&el_st, 0x30)->nprof <= BTEL_PROF_MAX, "profile record is bounded");
	/* the longest ending: a full profile list and the longest reason still fits the log's line (the fake log checks every line's length) */
	for (int k = 0; k < 8; k++) {
		chan(5, k, 0x1B, 0, 0);
		chan(5, k + 8, 0, 0, 0);
	}
	pass_all();
	ev_disc(0, 0x30, 0x3D);
	pass_all();
	CHECK(has_line("connection terminated due to MIC failure (0x3d)") && has_line("..."), "the long ending is written, its profile list shortened");
	puts("  ok: profiles");
}

static void test_loop_limits(void)
{
	reset();
	pass_all();
	/* the headset reconnects in a loop: same device, same reason, same opener, 30 times in 3 minutes */
	for (int i = 0; i < 30; i++) {
		tick(6000);
		ev_conn_req(ADDR);
		ev_conn_cmp(ADDR, 0, 0x40, 1);
		pass_all();
		tick(3000);
		ev_disc(0, 0x40, 0x13);
		pass_all();
	}
	CHECK(count_lines(" ended after ") == 1, "thirty identical endings are one line (%d)", count_lines(" ended after "));
	CHECK(g_nlog <= 4, "a loop writes a handful of lines, not 60 (%d)", g_nlog);
	CHECK(el_lim.repeats == 29, "29 folded");
	tick(200000);
	pass_all();
	CHECK(has_line("link log: 29 more endings of 11:22:33:xx:xx:xx like that, remote user terminated (0x13), opened by the remote (last: link #30 after 3.0 s)"), "the summary");

	/* endings that all differ in reason: the window cap holds, says so once, and the sum comes after the window */
	reset();
	pass_all();
	for (int i = 0; i < 40; i++) {
		tick(500);
		ev_conn_cmp(ADDR, 0, 0x50, 1);
		ev_disc(0, 0x50, 0x20 + (i & 1) + (i % 7));
		pass_all();
	}
	CHECK(g_nlog <= (int)BTEL_WIN_MAX + 1, "capped per minute (%d lines)", g_nlog);
	CHECK(el_lim.held > 0, "the rest were held");
	tick(70000);
	pass_all();
	CHECK(has_line("more link line(s) were held back in the last minute"), "held-back lines are summed up");
	CHECK(count_lines("held back") == 1, "once");

	/* a day of it: the per-start limit holds, says so once, and the log stays small */
	reset();
	pass_all();
	for (int i = 0; i < 3000; i++) {
		tick(7000);
		ev_conn_cmp(ADDR, 0, 0x60, 1);
		ev_disc(0, 0x60, 0x20 + i % 25);
		pass_all();
	}
	CHECK(el_lines <= BTEL_BOOT_MAX + 400, "the lines written stay bounded (%u)", (unsigned)el_lines);
	CHECK(count_lines("no more link lines until the next start") == 1, "the limit is said once");
	CHECK(g_log_bytes < 24 * 1024, "the link lines stay well under the log's 64 KB (%u bytes)", (unsigned)g_log_bytes);
	{
		int before = g_nlog;
		tick(7000);
		ev_conn_cmp(ADDR, 0, 0x61, 1);
		ev_disc(0, 0x61, 0x13);
		pass_all();
		CHECK(g_nlog == before, "nothing is written after the limit");
	}
	puts("  ok: reconnect loop limits");
}

static void test_switch_and_hook_state(void)
{
	reset();
	pass_all();
	ev_conn_cmp(ADDR, 0, 0x70, 1);
	pass_all();
	btt_btevlog_pass(0);
	CHECK(btevlog_on == 0, "off: noting stops");
	ev_disc(0, 0x70, 0x13);
	btt_btevlog_pass(0);
	CHECK(count_lines(" ended after ") == 0, "nothing is noted while off");
	/* back on: a link seen before is not known; its ending is not mistaken for another */
	pass_all();
	CHECK(btel_by_handle(&el_st, 0x70) == 0, "links seen before the switch-on are forgotten");
	g_hook_in = 0;
	reset();
	g_hook_in = 0;
	pass_all();
	CHECK(btevlog_on == 0, "the hook was refused: nothing is noted");
	puts("  ok: switch and hook state");
}

static void test_review_fixes(void)
{
	/* a pattern change after a fold: the summary describes the folded group (headset-opened, 0x13), not the ending that ended the fold */
	reset();
	pass_all();
	for (int i = 0; i < 5; i++) {
		tick(5000);
		ev_conn_req(ADDR);
		ev_conn_cmp(ADDR, 0, 0x40, 1);
		pass_all();
		tick(3000);
		ev_disc(0, 0x40, 0x13);
		pass_all();
	}
	tick(20000);
	ev_conn_cmp(ADDR, 0, 0x41, 1);                 /* the Vita opens this one, and it times out */
	tick(3000);
	ev_disc(0, 0x41, 0x08);
	pass_all();
	CHECK(has_line("link log: 4 more endings of 11:22:33:xx:xx:xx like that, remote user terminated (0x13), opened by the remote (last: link #5 after 3.0 s)"), "the summary is about the folded group");
	CHECK(has_line("ended after 3.0 s: connection timeout (0x08); opened by the Vita"), "and the new ending is its own line");

	/* a voice link's connection request does not make a later Vita-opened ACL link the remote's */
	reset();
	pass_all();
	{
		uint8_t sco[12] = { 0x04, 10, ADDR[0], ADDR[1], ADDR[2], ADDR[3], ADDR[4], ADDR[5], 0x04, 0x04, 0x24, 0 };
		btevlog_note_hci(sco, 12);
		sco[11] = 2;
		btevlog_note_hci(sco, 12);
	}
	tick(1000);
	ev_conn_cmp(ADDR, 0, 0x50, 1);
	pass_all();
	CHECK(has_line("up: opened by the Vita") && !has_line("opened by the remote"), "a voice request is not an ACL request");

	/* endings of unknown handles are few: they cannot spend the line budget */
	reset();
	pass_all();
	for (int i = 0; i < 20; i++) {
		tick(1000);
		ev_disc(0, 0x300 + i, 0x13);
		pass_all();
	}
	CHECK(count_lines("link handle 0x") == (int)BTEL_UNKNOWN_MAX, "only the first %u unknown endings are written (%d)", (unsigned)BTEL_UNKNOWN_MAX, count_lines("link handle 0x"));
	puts("  ok: review fixes");
}

int main(void)
{
	test_names();
	test_ring();
	test_hook();
	test_logic_lines();
	test_state();
	test_profiles();
	test_loop_limits();
	test_switch_and_hook_state();
	test_review_fixes();
	puts("test_btevlog: all passed");
	return 0;
}
