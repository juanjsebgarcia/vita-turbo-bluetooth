/*
 * test_btldac_stream.h - the LDAC scenarios of test/test_btaac_stream.c (included there after the aptX ones, so they share the fake Bluetooth code): the endpoint choice and
 * the configuration bytes (kernel/btldac_sep.h), the negotiation when the Vita opens the stream, the offer and the acceptance when the headphones open it (endpoint 5), the
 * media hook's packets (RTP header, one payload header byte, whole frames; one or two to a call, the flag on the last only; the limit of 679), the worker's session and gates
 * (a limit under 679 ends the session and marks the device), the add-on's absence, the codec choice and the bit rate setting. The add-on is a fake table here (its encoder
 * is tested in test/test_btldac.c); what is tested is what the main module does with it.
 */

/* ---- the add-on's table, as the main module holds it ----------------------------------------------------------------- */
static struct { int starts, stops, start_ret, selftest, hold, state, last_error, syncs, modes, last_mode, last_mtu, pushes, trims, last_keep, pops; uint32_t kbps; } g_ld;
typedef struct { uint8_t data[BTLDAC_FRAMES_BYTES]; uint32_t len, nf; } LdPkt;
static LdPkt g_ldq[32];
static int g_ldq_n, g_ldq_rd;
static int fl_start(uint32_t mode, uint32_t mtu)
{
	g_ld.starts++;
	g_ld.last_mode = (int)mode;
	g_ld.last_mtu = (int)mtu;
	if (g_ld.start_ret == BTLDAC_OK)
		g_ld.state = BTLDAC_S_RUNNING;
	return g_ld.start_ret;
}
static void fl_stop(void) { g_ld.stops++; g_ld.state = BTLDAC_S_IDLE; }
static void fl_sync(int on) { g_ld.syncs += on; }
static void fl_set_mode(uint32_t mode) { g_ld.modes++; g_ld.last_mode = (int)mode; }
static void fl_info(BtLdacInfo *o)
{
	memset(o, 0, sizeof(*o));
	o->selftest = (uint32_t)g_ld.selftest;
	o->hold = (uint32_t)g_ld.hold;
	o->state = (uint32_t)g_ld.state;
	o->last_error = g_ld.last_error;
	o->kbps = g_ld.kbps;
}
static uint32_t fl_push(const int16_t *pcm, uint32_t frames) { (void)pcm; g_ld.pushes++; return frames; }
static uint32_t fl_pop(uint8_t *out, uint32_t cap, uint32_t *nf, uint32_t *seq)
{
	if (g_ldq_rd >= g_ldq_n)
		return 0;
	LdPkt *p = &g_ldq[g_ldq_rd];

	g_ld.pops++;
	if (p->len > cap)
		return 0;
	memcpy(out, p->data, p->len);
	*nf = p->nf;
	*seq = (uint32_t)g_ldq_rd++;
	return p->len;
}
static uint32_t fl_trim(uint32_t keep)
{
	uint32_t dropped = 0;

	g_ld.trims++;
	g_ld.last_keep = (int)keep;
	while ((uint32_t)(g_ldq_n - g_ldq_rd) > keep) {
		g_ldq_rd++;
		dropped++;
	}
	return dropped;
}
static uint32_t fl_depth(void) { return (uint32_t)(g_ldq_n - g_ldq_rd); }
static const BtLdacApi k_ld_api = { .size = sizeof(BtLdacApi), .version = BTLDAC_API_VERSION, .start = fl_start, .stop = fl_stop, .sync = fl_sync, .set_mode = fl_set_mode,
                                    .get_info = fl_info, .push_pcm = fl_push, .pop_pkt = fl_pop, .pkt_trim = fl_trim, .pkt_depth = fl_depth };
static void ld_up(void)   /* the add-on loaded and its self-test passed */
{
	memset(&g_ld, 0, sizeof(g_ld));
	g_ld.selftest = BTLDAC_ST_PASS;
	g_ldq_n = g_ldq_rd = 0;
	btldac_link_api = &k_ld_api;
}
/* a packet of the engine: `nf` frames of `len` bytes, byte i is i plus `seed` */
static void ld_queue(uint32_t nf, uint32_t len, uint8_t seed)
{
	LdPkt *p = &g_ldq[g_ldq_n++];

	p->len = len;
	p->nf = nf;
	for (uint32_t i = 0; i < len; i++)
		p->data[i] = (uint8_t)(i + seed);
}

/* an LDAC headset's endpoint (44.1, 48, 88.2 and 96 kHz, every channel mode, SCMS-T offered: 18 bytes, more than the Bluetooth code takes) and some that are not ours */
static const uint8_t k_caps_ld[18] = { 0x01, 0x00, 0x07, 0x0A, 0x00, 0xFF, 0x2D, 0x01, 0x00, 0x00, 0xAA, 0x00, 0x3C, 0x07, 0x04, 0x02, 0x02, 0x00 };
static const uint8_t k_caps_ld_44[14] = { 0x01, 0x00, 0x07, 0x0A, 0x00, 0xFF, 0x2D, 0x01, 0x00, 0x00, 0xAA, 0x00, 0x20, 0x07 };    /* 44.1 kHz only */
static const uint8_t k_caps_ld_mono[14] = { 0x01, 0x00, 0x07, 0x0A, 0x00, 0xFF, 0x2D, 0x01, 0x00, 0x00, 0xAA, 0x00, 0x10, 0x04 };  /* 48 kHz, mono only */
static const uint8_t k_ld_cfg[14] = { 0x01, 0x00, 0x07, 0x0A, 0x00, 0xFF, 0x2D, 0x01, 0x00, 0x00, 0xAA, 0x00, 0x10, 0x01 };

static void t_ld_sep(void)
{
	BtAacSink s;
	uint8_t c[40], cfg[BTLDAC_CONFIG_LEN];
	uint32_t cat;

	CHECK(btldac_sep_scan(k_caps_ld, sizeof(k_caps_ld), &s) && s.object == 0 && s.max_bps == 0, "an LDAC endpoint: 48 kHz and stereo offered");
	CHECK(!btldac_sep_scan(k_caps_aac6, sizeof(k_caps_aac6), &s) && !btldac_sep_scan(k_caps_sbc5, sizeof(k_caps_sbc5), &s) && !btldac_sep_scan(k_caps_vendor7, sizeof(k_caps_vendor7), &s) &&
	      !btldac_sep_scan(k_caps_vendor9, sizeof(k_caps_vendor9), &s), "AAC, SBC, aptX and the other vendor codec are not LDAC");
	CHECK(!btldac_sep_scan(k_caps_ld_44, sizeof(k_caps_ld_44), &s), "44.1 kHz only: no 48 kHz bit");
	CHECK(!btldac_sep_scan(k_caps_ld_mono, sizeof(k_caps_ld_mono), &s), "mono only");
	CHECK(!btldac_sep_scan(NULL, 0, &s) && !btldac_sep_scan(k_caps_ld, 13, &s) && !btldac_sep_scan(k_caps_ld, 3, &s), "an empty or truncated reply");
	memcpy(c, k_caps_ld, 18);
	c[3] = 0x30;
	CHECK(!btldac_sep_scan(c, 18, &s), "an element running past the reply");
	memcpy(c, k_caps_ld, 18);
	c[6] = 0x2C;
	CHECK(!btldac_sep_scan(c, 18, &s), "another vendor");
	memcpy(c, k_caps_ld, 18);
	c[10] = 0xAB;
	CHECK(!btldac_sep_scan(c, 18, &s), "another codec of that vendor");
	memcpy(c, "\x04\x02\x02\x00\x08\x00", 6);   /* content protection, delay reporting, then the vendor element */
	memcpy(c + 6, k_caps_ld + 2, 12);
	CHECK(btldac_sep_scan(c, 18, &s), "the element behind other categories");
	btldac_sep_config(cfg);
	CHECK(!memcmp(cfg, k_ld_cfg, 14) && BTLDAC_CONFIG_LEN == 14 && BTLDAC_CONFIG_LEN <= BTAAC_CAPS_BUF && BTLDAC_CONFIG_LEN <= BTAAC_CONFIG_BUF, "the configuration: 01 00 07 0A 00 FF 2D 01 00 00 AA 00 10 01 (48 kHz, stereo)");
	CHECK(btldac_sep_scan(cfg + 2, 12, &s), "the configuration scans back as an LDAC element");
	/* what the headphones may configure on endpoint 5 */
	CHECK(btldac_sep_check_config(k_ld_cfg, 14, &s, &cat) == 0, "the one configuration we advertise");
	static const struct { const char *what; uint8_t cfg[18]; int n; uint32_t cat, err; } bad[] = {
		{ "44.1 kHz", { 1, 0, 7, 10, 0, 0xFF, 0x2D, 1, 0, 0, 0xAA, 0, 0x20, 1 }, 14, 7, 0x29 },
		{ "both frequencies", { 1, 0, 7, 10, 0, 0xFF, 0x2D, 1, 0, 0, 0xAA, 0, 0x30, 1 }, 14, 7, 0x29 },
		{ "mono", { 1, 0, 7, 10, 0, 0xFF, 0x2D, 1, 0, 0, 0xAA, 0, 0x10, 4 }, 14, 7, 0x29 },
		{ "dual channel", { 1, 0, 7, 10, 0, 0xFF, 0x2D, 1, 0, 0, 0xAA, 0, 0x10, 2 }, 14, 7, 0x29 },
		{ "another vendor's codec", { 1, 0, 7, 10, 0, 0xFF, 0x4F, 0, 0, 0, 1, 0, 0x10, 1 }, 14, 7, 0x29 },
		{ "AAC", { 1, 0, 7, 8, 0, 2, 0x80, 0, 0x84, 3, 0xE8, 0 }, 12, 7, 0x29 },
		{ "SBC", { 1, 0, 7, 6, 0, 0, 0x11, 0x15, 2, 0x35 }, 10, 7, 0x29 },
		{ "content protection", { 1, 0, 4, 2, 2, 0, 7, 10, 0, 0xFF, 0x2D, 1, 0, 0, 0xAA, 0 }, 16, 4, 0x17 },
		{ "a short element", { 1, 0, 7, 9, 0, 0xFF, 0x2D, 1, 0, 0, 0xAA, 0, 0x10 }, 13, 7, 0x11 },
		{ "an element running past the end", { 1, 0, 7, 10, 0, 0xFF, 0x2D, 1, 0, 0 }, 10, 7, 0x11 },
		{ "no transport", { 7, 10, 0, 0xFF, 0x2D, 1, 0, 0, 0xAA, 0, 0x10, 1 }, 12, 1, 0x1A },
		{ "no codec", { 1, 0 }, 2, 7, 0x1A },
		{ "two codec elements", { 7, 10, 0, 0xFF, 0x2D, 1, 0, 0, 0xAA, 0, 0x10, 1, 7, 10 }, 14, 7, 0x11 },
		{ "a trailing byte", { 1, 0, 7, 10, 0, 0xFF, 0x2D, 1, 0, 0, 0xAA, 0, 0x10, 1, 0 }, 15, 0, 0x11 },
	};
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		uint32_t e = btldac_sep_check_config(bad[i].cfg, (uint32_t)bad[i].n, &s, &cat);

		CHECK(e == bad[i].err && cat == bad[i].cat, "%s: error 0x%x category %u, wanted 0x%x and %u", bad[i].what, (unsigned)e, (unsigned)cat, (unsigned)bad[i].err, (unsigned)bad[i].cat);
	}
	CHECK(BTLDAC_SEID_BYTE == 0x14 && BTLDAC_SEID_BYTE != BTAAC_SEID_AAC_BYTE && BTLDAC_SEID_BYTE != BTAPTX_SEID_BYTE && BTLDAC_SEID_BYTE != BTAAC_SEID_SBC_BYTE, "endpoint 5 is its own");
}

static void ld_neg_world(void)
{
	world(2, 1);
	btaac_neg.want_ldac = 1;
}
/* the reference headset's list with endpoint 9 (the second vendor codec) replaced by an LDAC one */
static void ld_all_caps(uint8_t *conn)
{
	ask_caps(conn, 0, k_caps_vendor7, sizeof(k_caps_vendor7));
	ask_caps(conn, 2, k_caps_aac6, sizeof(k_caps_aac6));
	ask_caps(conn, 4, k_caps_sbc5, sizeof(k_caps_sbc5));
	ask_caps(conn, 6, k_caps_ld, sizeof(k_caps_ld));
}

static void t_ld_negotiation(void)
{
	uint8_t *conn = slot(2);
	uint8_t *rec = conn + BTAAC_C_REC_B;

	ld_neg_world();
	start_negotiation(conn);
	CHECK(btaac_neg.state == BTAAC_N_PROBING && !btaac_neg.have, "probing");
	ld_all_caps(conn);
	CHECK(btaac_neg.have && btaac_neg.ldac && !btaac_neg.aptx && btaac_neg.seid == 0x24 && btaac_neg.n_chosen == 1, "endpoint 9 (byte 0x24) is chosen, as LDAC: seid %02x", (unsigned)btaac_neg.seid);
	stock_narrows(conn);
	btaac_h2_after(conn, 8, conn + BTAAC_C_REC_B + 6, 14);
	CHECK(btaac_neg.state == BTAAC_N_PENDING && rec[5] == 0x24 && rec[4] == 0x04 && get32(rec, 0x18) == 14 && !memcmp(rec + 6, k_ld_cfg, 14) && rec[0x20] == 0x30,
	      "the record: endpoint %02x, length %u, the 14 LDAC bytes", rec[5], (unsigned)get32(rec, 0x18));
	CHECK(btaac_neg.config_len == 14 && btaac_neg.sbc_seid == 0x14 && btaac_neg.sbc_len == 14 && !memcmp((const void *)btaac_neg.config, k_ld_cfg, 14), "the SBC choice kept for the fallback");
	CHECK(send_disp(conn, 2, 3, (const uint8_t *)"", 0) == g_disp_ret && btaac_neg.state == BTAAC_N_CONFIGURED && g_l2.calls == 0, "the sink accepts: configured as LDAC");
	/* the same headset with aptX or AAC wanted: its own endpoint, as before; LDAC is not looked for */
	world(2, 1);
	btaac_neg.want_aptx = 1;
	start_negotiation(conn);
	ld_all_caps(conn);
	CHECK(btaac_neg.have && btaac_neg.aptx && !btaac_neg.ldac && btaac_neg.seid == 0x1C, "aptX wanted: endpoint 7");
	world(2, 1);
	start_negotiation(conn);
	ld_all_caps(conn);
	CHECK(btaac_neg.have && !btaac_neg.aptx && !btaac_neg.ldac && btaac_neg.seid == 0x18, "AAC wanted: endpoint 6");
	/* LDAC wanted by a headset that has none: SBC untouched */
	ld_neg_world();
	start_negotiation(conn);
	all_caps(conn);
	stock_narrows(conn);
	btaac_h2_after(conn, 8, conn + BTAAC_C_REC_B + 6, 14);
	CHECK(!btaac_neg.have && btaac_neg.state == BTAAC_N_NONE && !btaac_neg.ldac && !memcmp(rec + 6, k_sbc_cfg, 14) && get32(rec, 0x18) == 14, "no LDAC endpoint: the code's SBC is untouched");
	/* a headset that lists only a 44.1 kHz LDAC: not used */
	ld_neg_world();
	start_negotiation(conn);
	ask_caps(conn, 0, k_caps_ld_44, sizeof(k_caps_ld_44));
	CHECK(!btaac_neg.have, "44.1 kHz only: not offered");
	/* the sink rejects: the SBC configuration again, the device is not offered a codec again this boot */
	ld_neg_world();
	start_negotiation(conn);
	ld_all_caps(conn);
	stock_narrows(conn);
	btaac_h2_after(conn, 8, conn + BTAAC_C_REC_B + 6, 14);
	CHECK(send_disp(conn, 3, 3, (const uint8_t *)"\x01\x29", 2) == g_disp_ret && btaac_neg.state == BTAAC_N_FALLBACK && g_l2.calls == 1 && g_l2.acp == 0x14 && g_l2.len == 14 &&
	      !memcmp(g_l2.caps, k_sbc_cfg, 14) && btaac_banned((uintptr_t)conn), "rejected: SBC sent again, byte for byte; the device marked");
	start_negotiation(conn);
	CHECK(btaac_neg.state == BTAAC_N_NONE && !btaac_neg.ldac, "marked: SBC on the next connection");
}

static void t_ld_acceptor(void)
{
	uint8_t *conn;

	acc_world(1);
	btaac_neg.want_ldac = 1;
	conn = g_aconn;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 2) == 0 && g_stock.calls == 0 && g_nrep == 1 && g_rep[0].b == 6 && !memcmp(g_rep[0].data, "\x04\x00\x08\x08\x14\x00", 6),
	      "DISCOVER: the code's endpoints 1 and 2, then endpoint 5 (an LDAC source, byte 0x14); the code does not see it");
	CHECK(btaac_neg.state == BTAAC_N_OFFERED && btaac_neg.acceptor && btaac_neg.ldac && !btaac_neg.aptx && btaac_neg.ep_byte == 0x14, "offered: LDAC, endpoint byte 0x14");
	CHECK(cmd1(6, 2, 0x0C) == g_disp_ret && g_stock.n_unknown == 1 && g_nrep == 1, "endpoint 3 (AAC) is not offered: the code rejects it as unknown");
	CHECK(cmd1(7, 2, 0x10) == g_disp_ret && g_stock.n_unknown == 2 && g_nrep == 1, "nor endpoint 4 (aptX)");
	CHECK(cmd1(8, 2, 0x14) == 0 && g_nrep == 2 && g_rep[1].sig == 2 && g_rep[1].b == 14 && !memcmp(g_rep[1].data, k_ld_cfg, 14), "endpoint 5's capabilities: answered here");
	{
		BtAacSink sk;

		CHECK(btldac_sep_scan(g_rep[1].data + 2, 12, &sk), "the reply scans as LDAC, 48 kHz stereo");
	}
	{
		uint8_t bad[14];

		memcpy(bad, k_ld_cfg, 14);
		bad[12] = 0x20;
		CHECK(send_conf(9, 0x14, 0x14, bad, 14) == 0 && g_stock.calls == g_stock.calls && g_nrep == 3 && g_rep[2].sig == 3 && g_rep[2].fmt == 0x3456 && g_rep[2].a == 7 && g_rep[2].b == 0x29 &&
		      REC_STATE() == 1 && btaac_neg.state == BTAAC_N_OFFERED, "44.1 kHz refused with a reject; the record untouched");
		CHECK(send_conf(9, 0x14, 0x14, k_aac_cfg, 12) == 0 && g_nrep == 4 && g_rep[3].a == 7 && g_rep[3].b == 0x29, "an AAC configuration for endpoint 5 is refused too");
	}
	CHECK(send_conf(9, 0x14, 0x14, k_ld_cfg, 14) == g_disp_ret && g_stock.n_conf == 1 && g_stock.msg[2] == 0x04 && g_stock.msg[3] == 0x14, "the good one: the code ran on a substitute for endpoint 1");
	{
		const uint8_t *rec = conn + BTAAC_C_REC_B;

		CHECK(get32(rec, 0) == 5 && rec[5] == 0x14 && get32(rec, 0x18) == 14 && !memcmp(rec + 6, k_ld_cfg, 14), "the record: configured, the sink's endpoint, the 14 LDAC bytes");
		CHECK(btaac_neg.state == BTAAC_N_CONFIGURED && btaac_neg.ldac && btaac_neg.config_len == 14 && btaac_neg.n_acc_config == 1, "configured as LDAC");
	}
	CHECK(cmd1(0xA, 6, 0x14) == g_disp_ret && g_stock.msg[2] == 4 && REC_STATE() == 6, "OPEN reaches the code as endpoint 1");
	put32(conn, BTAAC_C_REC_B, 8);
	CHECK(cmd1(0xB, 7, 0x14) == g_disp_ret && g_stock.msg[2] == 4 && REC_STATE() == 10, "START: streaming");
	CHECK(cmd1(0xC, 8, 0x14) == g_disp_ret && REC_STATE() == 1, "CLOSE");
	CHECK(cmd1(0xD, 7, 0x10) == g_disp_ret && g_stock.ptr == g_msg, "a command for endpoint 4 is the code's own on an LDAC connection");
	/* LDAC not wanted: endpoint 5 is not offered */
	acc_world(1);
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	btaac_disp_hook(g_aconn, g_chan, g_msg, 2);
	CHECK(!memcmp(g_rep[0].data, "\x04\x00\x08\x08\x0C\x00", 6) && !btaac_neg.ldac, "AAC wanted: endpoint 3 listed, as before");
	CHECK(cmd1(8, 2, 0x14) == g_disp_ret && g_stock.n_unknown == 1 && g_nrep == 1, "endpoint 5 is the code's to reject");
	/* the master switch off: the code's own SBC-only answer */
	acc_world(0);
	btaac_neg.want_ldac = 1;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	CHECK(btaac_disp_hook(g_aconn, g_chan, g_msg, 2) == g_disp_ret && g_stock.n_discover == 1 && g_nrep == 0, "not wanted: the code answers");
}

/* the media hook: the engine's packets, one or two to a call */
static void ld_media_world(uint32_t mtu)
{
	media_world(mtu, BTAAC_N_CONFIGURED);
	btaac_neg.ldac = 1;
	btaac_neg.want_ldac = 1;
	ld_up();
}

static void t_ld_media(void)
{
	uint8_t *conn = slot(1);
	uint8_t want[660];

	ld_media_world(1021);
	btaac_neg.ssrc = 0x56544101;
	/* nothing made yet: nothing is sent, counted */
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0 && btaac_neg.n_ldac_dry == 1 && btaac_neg.n_no_au == 1, "no packet ready: an error number, nothing sent");
	/* one packet: RTP header, the frame count, the frames as made; the flag as it came; the code's own SBC buffer never */
	ld_queue(6, 660, 0x11);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_calls == 1, "sent");
	for (int i = 0; i < 660; i++)
		want[i] = (uint8_t)(i + 0x11);
	CHECK(g_send_last.len == 673 && g_send_last.flag == 1 && g_send_last.buf != g_sbc && g_npkts == 1 && g_pkts[0].len == 673, "one packet of 673 bytes");
	{
		const uint8_t *p = g_pkts[0].data;

		CHECK(p[0] == 0x80 && p[1] == 96 && p[2] == 0 && p[3] == 0 && !p[4] && !p[5] && !p[6] && !p[7] && p[8] == 0x56 && p[9] == 0x54 && p[10] == 0x41 && p[11] == 0x01 && p[12] == 6 && !memcmp(p + 13, want, 660),
		      "RTP header (sequence 0, timestamp 0, the session's source), 6 frames, the frames as the engine made them");
	}
	CHECK(btaac_neg.n_sent == 1 && btaac_neg.n_ldac_pkts == 1 && btaac_neg.n_ldac_calls == 1 && btaac_neg.last_au_len == 660, "counted");
	/* the next packet: the sequence number up by one, the timestamp by 6 frames of 128 */
	ld_queue(6, 660, 0x22);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 0) == g_send_ret && g_pkts[1].data[3] == 1 && g_pkts[1].data[6] == 3 && g_pkts[1].data[7] == 0 && g_pkts[1].flag == 0, "sequence 1, timestamp 768, the flag as given (0)");
	/* two packets ready (660 kb/s makes 1.33 a block): both go in this call, back to back, the flag on the last only, no frame cut */
	ld_queue(3, 660, 0x33);
	ld_queue(3, 660, 0x44);
	g_send_calls = 0;
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_calls == 2 && g_npkts == 4, "two packets in one call");
	CHECK(g_pkts[2].flag == 0 && g_pkts[3].flag == 1 && g_pkts[2].len == 673 && g_pkts[3].len == 673 && g_pkts[2].data[12] == 3 && g_pkts[3].data[12] == 3, "the flag on the last only; 3 frames each");
	CHECK(g_pkts[2].data[3] == 2 && g_pkts[3].data[3] == 3 && g_pkts[2].data[6] == 0x06 && g_pkts[2].data[7] == 0x00 && g_pkts[3].data[6] == 0x07 && g_pkts[3].data[7] == 0x80,
	      "numbers 2 and 3; the timestamp is 1536, then 1920: 384 on for a packet of 3 frames");
	CHECK(btaac_neg.n_ldac_calls2 == 1 && btaac_neg.n_ldac_pkts == 4 && btaac_neg.n_back == 1 && btaac_neg.n_pieces == 4 && btaac_neg.n_sent == 3, "counted: a call of two");
	/* three ready: two go, one stays for the next call */
	ld_queue(3, 660, 1);
	ld_queue(3, 660, 2);
	ld_queue(3, 660, 3);
	g_send_calls = 0;
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_calls == 2 && fl_depth() == 1, "at most two to a call");
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_calls == 3 && fl_depth() == 0, "the third on the next call");
	/* the ring is held to the latency bound first: the oldest go */
	ld_up();
	btaac_neg.ldac_keep = 3;
	for (int k = 0; k < 8; k++)
		ld_queue(6, 660, (uint8_t)k);
	g_send_calls = 0;
	g_npkts = 0;
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_ld.last_keep == 3 && btaac_neg.n_ldac_trim == 5 && g_npkts == 2 && g_pkts[0].data[13] == 5, "8 waiting, a bound of 3: the 5 oldest dropped, then two sent from the rest (the sixth packet's first byte 5)");
	/* a failed send: the code's error comes back and is counted; the rest of the call is not sent */
	ld_up();
	ld_queue(3, 660, 9);
	ld_queue(3, 660, 9);
	g_send_calls = 0;
	g_send_fail_at = 0;
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && btaac_neg.n_send_err >= 1 && g_send_calls == 1, "the code's send failed: its error number comes back, no second packet");
	g_send_fail_at = -1;
	/* a media channel limit under 679: nothing is sent, counted (the session was refused when it began; this is the net) */
	ld_media_world(672);
	ld_queue(6, 660, 1);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0 && btaac_neg.n_ldac_mtu == 1 && fl_depth() == 1, "a limit of 672: nothing sent, the packet stays");
	ld_media_world(679);
	ld_queue(6, 660, 1);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_calls == 1, "a limit of 679: sent");
	/* a limit that cannot be read is the default, 672: not enough */
	ld_media_world(1021);
	*(uintptr_t *)(conn + BTAAC_C_MEDIA_CH) = 0;
	ld_queue(6, 660, 1);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0 && btaac_neg.n_ldac_mtu == 1, "no channel to read: the default limit, 672: not enough for LDAC");
	/* a packet the builder refuses (more frames than four bits count) is dropped, counted */
	ld_media_world(1021);
	ld_queue(16, 660, 1);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && btaac_neg.n_wrap_err == 1 && g_send_calls == 0, "a bad packet from the engine: nothing sent");
	/* the stream record not open: the routine's own condition */
	ld_media_world(1021);
	put32(conn, BTAAC_C_REC_B, 8);
	ld_queue(6, 660, 1);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0, "a stream record that is not open: nothing");
	/* no session: the code's SBC packet is never sent on an LDAC stream */
	ld_media_world(1021);
	btaac_neg.send_gate = 0;
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0 && btaac_neg.n_swallowed == 1, "no session: the code's SBC packet is dropped");
}

static void t_ld_pcm(void)
{
	static int16_t blk[1024];
	uint8_t *conn = slot(1);

	ld_media_world(1021);
	btaac_pcm_block(blk);
	CHECK(g_ld.pushes == 1 && btaac_neg.n_pcm_blocks == 1 && !btaac_neg.apt_ready, "the sender's block goes to the add-on's ring (512 frames), not the aptX stash");
	btaac_neg.pcm_gate = 0;
	btaac_pcm_block(blk);
	CHECK(g_ld.pushes == 1, "gate down: nothing");
	btaac_neg.pcm_gate = 1;
	put32(conn, BTAAC_C_PROFILES, 0);
	btaac_pcm_block(blk);
	CHECK(g_ld.pushes == 1, "not started: nothing");
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	btaac_neg.state = BTAAC_N_PENDING;
	btaac_pcm_block(blk);
	CHECK(g_ld.pushes == 1, "not configured: nothing");
}

static void ld_stream_world(void)
{
	stream_world();
	btaac_link_api = 0;   /* LDAC works with the AAC module absent */
	btaptx_link_api = 0;
	ld_up();
	g_bteq_in = 1;
}
static void ld_up_stream(uint8_t *conn)
{
	btaac_neg.conn = (uintptr_t)conn;
	btaac_neg.state = BTAAC_N_CONFIGURED;
	btaac_neg.ldac = 1;
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	put32(conn, BTAAC_C_PROFILES, 0);
	*(uintptr_t *)(conn + BTAAC_C_MEDIA_CH) = (uintptr_t)g_chan;
	put32(g_chan, 8, 1021);
}
static void ld_pass(void) { btt_aac_stream_pass(1, BTT_CODEC_LDAC, 1); }
static void ld_pass2(void) { ld_pass(); ld_pass(); }   /* a session is asked for in one pass and its gates go up in the next (the engine's thread makes the encoder in between) */

static void t_ld_worker(void)
{
	uint8_t *conn = sslot(2);

	stream_world();
	btaac_link_api = 0;
	btaptx_link_api = 0;
	btldac_link_api = 0;
	/* no add-on: LDAC wanted means SBC, and nothing is installed */
	ld_pass();
	CHECK(g_ninst == 1 && g_inst[0].hook == btaac_disp_hook && !btaac_neg.enable && !g_installed, "no LDAC module: no codec is offered, only the reader of the codec lists is in");
	CHECK(btt_codec_effective(BTT_CODEC_LDAC) == BTT_CODEC_SBC && btt_ldac_avail() == 0, "the effective codec is SBC, nothing available");
	/* the add-on there but its self-test not passed, or held */
	ld_up();
	g_ld.selftest = BTLDAC_ST_NOTRUN;
	CHECK(btt_codec_effective(BTT_CODEC_LDAC) == BTT_CODEC_SBC && btt_ldac_avail() == BTT_LDAC_AVAIL_MODULE, "module there, not tested: SBC");
	g_ld.selftest = BTLDAC_ST_FAIL;
	CHECK(btt_codec_effective(BTT_CODEC_LDAC) == BTT_CODEC_SBC, "self-test failed: SBC");
	g_ld.selftest = BTLDAC_ST_PASS;
	g_ld.hold = 1;
	CHECK(btt_codec_effective(BTT_CODEC_LDAC) == BTT_CODEC_SBC && btt_ldac_avail() == (BTT_LDAC_AVAIL_MODULE | BTT_LDAC_AVAIL_TESTED | BTT_LDAC_AVAIL_HELD), "held: SBC, the bits say so");
	g_ld.hold = 0;
	CHECK(btt_codec_effective(BTT_CODEC_LDAC) == BTT_CODEC_LDAC && btt_ldac_avail() == (BTT_LDAC_AVAIL_MODULE | BTT_LDAC_AVAIL_TESTED) && btt_ldac_codec_effective(BTT_CODEC_AAC) == BTT_CODEC_SBC, "usable");
	CHECK(btt_codec_effective(BTT_CODEC_SBC) == BTT_CODEC_SBC && btt_codec_effective(BTT_CODEC_AAC) == BTT_CODEC_SBC && btt_codec_effective(BTT_CODEC_APTX) == BTT_CODEC_SBC, "SBC stays SBC; AAC and aptX without their modules are SBC");
	/* wanted: the hooks go in, the next headphones are offered LDAC */
	btt_aac_stream_set_ldac_mode(BTLDAC_MODE_660);
	ld_pass();
	CHECK(g_ninst == 2 && g_installed && btaac_neg.enable && btaac_neg.want_ldac && !btaac_neg.want_aptx, "installed; LDAC offered");
	ld_up_stream(conn);
	ld_pass();
	CHECK(btt_aac_stream_fast() && !btt_aac_stream_is_aac() && g_ld.starts == 0 && !c_gates(), "an LDAC stream not started: watched closely, no session; not AAC (the SBC send queue limits apply)");
	/* the stream starts: the session, with the setting and the link's limit, then the gates (blocks first) */
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	ld_pass();
	CHECK(g_ld.starts == 1 && !c_gates(), "the session is asked for first; the gates wait for it to run");
	ld_pass();
	CHECK(g_ld.starts == 1 && g_ld.last_mode == BTLDAC_MODE_660 && g_ld.last_mtu == 1021 && c_gates() && log_has("LDAC is playing"), "session started once with the 660 setting and the limit 1021, gates up");
	ld_pass();
	CHECK(g_ld.starts == 1, "not started twice");
	/* the bit rate setting changes while it runs: the add-on is told at once */
	btt_aac_stream_set_ldac_mode(BTLDAC_MODE_ABR);
	g_ld.modes = 0;
	ld_pass();
	CHECK(g_ld.modes == 1 && g_ld.last_mode == BTLDAC_MODE_ABR && g_ld.starts == 1, "the new setting reaches the running session");
	btt_aac_stream_set_ldac_mode(99);
	ld_pass();
	CHECK(g_ld.last_mode == BTLDAC_MODE_330, "a setting out of range is 330");
	/* the latency setting bounds the engine's output, in packets of the mode's size */
	btt_aac_stream_set_ldac_mode(BTLDAC_MODE_330);
	btt_aac_stream_set_latency(2);
	ld_pass();
	CHECK(btaac_neg.ldac_keep == 3, "Lowest at 330 kb/s: 3 packets");
	btt_aac_stream_set_latency(0);
	ld_pass();
	CHECK(btaac_neg.ldac_keep == 8, "Standard at 330 kb/s: 8 packets");
	/* a pause under 3 s keeps it, over 3 s ends it: gates down first, then the add-on stopped */
	put32(conn, BTAAC_C_PROFILES, 0);
	g_us += 2u * 1000000u;
	ld_pass();
	CHECK(c_gates() && g_ld.stops == 0, "2 s: kept");
	g_us += 1500000u;
	ld_pass();
	CHECK(!c_gates() && g_ld.stops == 1 && log_has("LDAC session ended"), "over 3 s: ended");
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	ld_pass2();
	CHECK(g_ld.starts == 2 && c_gates(), "started again: a new session");
	/* the link goes */
	put32(conn, BTAAC_C_REC_B, 0);
	ld_pass();
	CHECK(!c_gates() && g_ld.stops == 2 && log_has("LDAC stream is gone"), "the record closed: ended");
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	ld_pass2();
	CHECK(g_ld.starts == 3 && c_gates(), "back: a third session");
	/* standby: the session is over before the handler returns */
	CHECK(g_sev_handler && g_sev_handler(1, 0x20F, NULL, NULL) == 0 && c_gates(), "a wake-up changes nothing");
	CHECK(g_sev_handler(0, 0x101, NULL, NULL) == 0 && c_gates() && g_ld.stops == 2, "a cancelled attempt leaves it");
	CHECK(g_sev_handler(0, 0x20F, NULL, NULL) == 0 && !c_gates() && g_ld.stops == 3, "standby: the session ended, gates down");
	/* the engine ends its own session (an encoder error): no sound, the device marked */
	ld_up();
	ld_pass();
	CHECK(g_ld.starts == 1, "a session again");
	g_ld.state = BTLDAC_S_FAILED;
	g_ld.last_error = BTLDAC_E_ENCODE;
	ld_pass();
	CHECK(btaac_neg.state == BTAAC_N_BROKEN && !c_gates() && btaac_banned((uintptr_t)conn) && log_has("the engine ended its session"), "the engine failed: broken, marked");
	/* the add-on refuses a session: no sound, the device marked */
	ld_up();
	btaac_neg.state = BTAAC_N_CONFIGURED;
	btaac_neg.ban_n = 0;
	CHECK(!btaac_banned((uintptr_t)conn), "the mark cleared for this scenario");
	g_ld.start_ret = BTLDAC_ERR_HELD;
	ld_pass();
	CHECK(btaac_neg.state == BTAAC_N_BROKEN && !c_gates() && btaac_banned((uintptr_t)conn) && log_has("would not start"), "a refused session: broken, marked\n%s", g_log);
	CHECK(btaac_ban_bits(btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_LO), btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_HI)) == 0 &&
	      btaac_local_bits(btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_LO), btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_HI)) == BTAAC_BAN_LDAC &&
	      log_has("not offered this start: the LDAC add-on could not start a session"), "and the mark is the module's own: not a refusal by the headphones\n%s", g_log);
	/* module stop */
	ld_up();
	btaac_neg.state = BTAAC_N_CONFIGURED;
	btaac_neg.send_gate = btaac_neg.pcm_gate = 1;
	g_ld.state = BTLDAC_S_RUNNING;
	btt_aac_stream_restore();
	CHECK(!c_gates() && !btaac_neg.enable && g_ld.stops >= 1 && g_ld.state == BTLDAC_S_IDLE && g_nrel == 2, "module stop: gates down, session ended, hooks released");
}

static void t_ld_mtu_refused(void)
{
	uint8_t *conn = sslot(2);

	ld_stream_world();
	ld_pass();
	ld_up_stream(conn);
	put32(g_chan, 8, 672);   /* the sink never named a limit of its own: the L2CAP default */
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	g_ld.start_ret = BTLDAC_ERR_MTU;
	ld_pass();
	CHECK(g_ld.starts == 1 && g_ld.last_mtu == 672 && btaac_neg.state == BTAAC_N_BROKEN && !c_gates() && btaac_banned((uintptr_t)conn) && btaac_neg.n_ldac_mtu == 1 &&
	      log_has("packet limit is below the 679 bytes LDAC needs"), "a limit of 672: the session ends before it starts, the device is marked (SBC next time), logged\n%s", g_log);
	CHECK(btaac_ban_bits(btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_LO), btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_HI)) == BTAAC_BAN_LDAC && !log_has("not offered this start"),
	      "the headphones' own packet limit is their doing: marked as a refusal, not as a local mark\n%s", g_log);
	/* the next connection of the same device is SBC */
	g_seen_state = BTAAC_N_NONE;
	ld_pass();
	CHECK(!btaac_neg.ldac || btaac_banned((uintptr_t)conn), "marked");
}

static void t_ld_worker_switch(void)
{
	uint8_t *conn = sslot(2);

	/* an LDAC connection ends and an AAC one begins: the LDAC session ends */
	ld_stream_world();
	ld_pass();
	ld_up_stream(conn);
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	ld_pass2();
	CHECK(g_ld.starts == 1 && c_gates(), "LDAC running");
	btaac_neg.ldac = 0;   /* the next connection negotiated something else (neg_begin closed the gates) */
	btaac_neg.send_gate = btaac_neg.pcm_gate = 0;
	btt_aac_stream_pass(1, BTT_CODEC_SBC, 1);
	CHECK(g_ld.stops == 1 && g_ld.state == BTLDAC_S_IDLE, "the LDAC session of the first connection ended");
}

static void t_ld_link_info(void)
{
	BtLinkIn in;
	BttLinkInfo k;

	memset(&in, 0, sizeof(in));
	in.on = 1; in.recognised = 1; in.connected = 1; in.streaming = 1; in.ldac_stream = 1; in.ldac_kbps = 660; in.aac_bps = 123;
	btlink_compose(&in, &k);
	CHECK(k.codec == BTT_LINK_CODEC_LDAC && k.bitrate_kbps == 660 && k.sample_rate == 48000 && k.accel == 0 && k.sbc_bitpool == 0, "LDAC: codec, the library's bit rate, 48 kHz");
	in.ldac_kbps = 0;
	btlink_compose(&in, &k);
	CHECK(k.codec == BTT_LINK_CODEC_LDAC && k.bitrate_kbps == 330, "no packet made yet: 330, the default");
	in.ldac_kbps = 330;
	in.ldac_stream = 0;
	in.aptx_stream = 1;
	btlink_compose(&in, &k);
	CHECK(k.codec == BTT_LINK_CODEC_APTX && k.bitrate_kbps == 384, "aptX as before");
}

static void t_ld_link_find(void)
{
	/* the table is looked up once, and a table that does not match is refused */
	btldac_link_api = 0;
	g_ld_find = 0;
	btt_ldac_link_find();
	CHECK(!btldac_link_api && log_has("LDAC module is not loaded"), "absent: logged once");
	g_looked = 0;
	g_ld_find = 2;
	btt_ldac_link_find();
	CHECK(!btldac_link_api && log_has("does not match this build"), "a table of another version is refused");
	g_looked = 0;
	g_ld_find = 1;
	ld_up();
	btldac_link_api = 0;
	btt_ldac_link_find();
	CHECK(btldac_link_api == &k_ld_api && log_has("LDAC module is loaded"), "found");
	btt_ldac_link_find();
	btt_ldac_link_sync(1, BTT_CODEC_LDAC);
	CHECK(g_ld.syncs == 1, "the self-test is asked for through the table");
	btldac_link_api = 0;
	btt_ldac_link_sync(1, BTT_CODEC_LDAC);
	btt_ldac_link_sync(1, BTT_CODEC_LDAC);
	CHECK(log_has("LDAC module is not loaded: SBC is sent"), "LDAC wanted, no module: said once");
	CHECK(btt_ldac_session_start(BTLDAC_MODE_330, 679) == BTLDAC_ERR_THREAD && !btt_ldac_session_running() && btt_ldac_state() == BTLDAC_S_IDLE && btt_ldac_last_error() == 0 &&
	      btt_ldac_pop_pkt(NULL, 0, NULL, NULL) == 0 && btt_ldac_push_pcm(NULL, 0) == 0 && btt_ldac_pkt_trim(1) == 0 && btt_ldac_pkt_depth() == 0, "with no add-on every call is a no-op");
}
