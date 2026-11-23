/*
 * test_btaptx_stream.h - the aptX scenarios of test/test_btaac_stream.c (included there, after the AAC scenarios, so they share the fake Bluetooth code): the endpoint
 * choice and the configuration bytes (kernel/btaptx_sep.h), the negotiation when the Vita opens the stream, the offer and the acceptance when the headphones open it
 * (endpoint 4), the media hook's packet (the encoder's 512 bytes and nothing else: no RTP header), the worker's session and gates, the fallback to SBC and the add-on's
 * absence. The add-on is a fake table here (its encoder is tested bit for bit in test/test_btaptx.c); what is tested is what the main module does with it.
 */

/* ---- the add-on's table, as the main module holds it ----------------------------------------------------------------- */
static struct { int starts, stops, start_ret, selftest, hold, encodes, encode_fail, syncs, selftest_hd, hd_ctx, last_hd, hd_encodes; } g_apt;
static int fa_start(int hd) { g_apt.starts++; g_apt.last_hd = hd; return g_apt.start_ret; }
static void fa_stop(void) { g_apt.stops++; }
static void fa_sync(int on) { g_apt.syncs += on; }
static void fa_info(BtAptxInfo *o)
{
	memset(o, 0, sizeof(*o));
	o->selftest = (uint32_t)g_apt.selftest;
	o->hold = (uint32_t)g_apt.hold;
	o->selftest_hd = (uint32_t)g_apt.selftest_hd;
	o->hd_ctx_ok = (uint32_t)g_apt.hd_ctx;
}
static uint32_t fa_encode_hd(const int16_t *pcm, uint8_t *out)   /* a recognisable block: byte i is i plus the first sample's low byte */
{
	g_apt.hd_encodes++;
	if (g_apt.encode_fail)
		return 0;
	for (uint32_t i = 0; i < 768; i++)
		out[i] = (uint8_t)(i * 3u + (uint8_t)pcm[0]);
	return 768;
}
static uint32_t fa_encode(const int16_t *pcm, uint8_t *out)   /* a recognisable block: byte i is i plus the first sample's low byte */
{
	g_apt.encodes++;
	if (g_apt.encode_fail)
		return 0;
	for (uint32_t i = 0; i < 512; i++)
		out[i] = (uint8_t)(i + (uint8_t)pcm[0]);
	return 512;
}
static const BtAptxApi k_apt_api = { .size = sizeof(BtAptxApi), .version = BTAPTX_API_VERSION, .start = fa_start, .stop = fa_stop, .sync = fa_sync, .get_info = fa_info, .encode = fa_encode, .encode_hd = fa_encode_hd };
static void apt_up(void)   /* the add-on loaded and its self-test passed (the HD one not: classic aptX only) */
{
	memset(&g_apt, 0, sizeof(g_apt));
	g_apt.selftest = BTAPTX_ST_PASS;
	btaptx_link_api = &k_apt_api;
}
static void apt_hd_up(void)   /* ... and the HD encoder's self-test passed too */
{
	apt_up();
	g_apt.selftest_hd = BTAPTX_ST_PASS;
	g_apt.hd_ctx = 1;
}

/* a headset's aptX endpoint (7) and some that are not ours */
static const uint8_t k_caps_apt_44[17] = { 0x01, 0x00, 0x07, 0x09, 0x00, 0xFF, 0x4F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x22, 0x04, 0x02, 0x02, 0x00 };   /* 44.1 kHz only */
static const uint8_t k_caps_apt_mono[13] = { 0x01, 0x00, 0x07, 0x09, 0x00, 0xFF, 0x4F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x31 };                         /* 48 kHz, mono only */
static const uint8_t k_apt_cfg[13] = { 0x01, 0x00, 0x07, 0x09, 0x00, 0xFF, 0x4F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x12 };

static void t_apt_sep(void)
{
	BtAacSink s;
	uint8_t c[40], cfg[BTAPTX_CONFIG_LEN];
	uint32_t cat;

	CHECK(btaptx_sep_scan(k_caps_vendor7, sizeof(k_caps_vendor7), &s) && s.object == 0 && s.max_bps == 0, "endpoint 7 of the recorded headset is aptX");
	CHECK(!btaptx_sep_scan(k_caps_aac6, sizeof(k_caps_aac6), &s) && !btaptx_sep_scan(k_caps_sbc5, sizeof(k_caps_sbc5), &s), "AAC and SBC are not aptX");
	CHECK(!btaptx_sep_scan(k_caps_vendor9, sizeof(k_caps_vendor9), &s), "another vendor codec (a different id) is not classic aptX");
	CHECK(!btaptx_sep_scan(k_caps_apt_44, sizeof(k_caps_apt_44), &s), "44.1 kHz only: no 48 kHz bit");
	CHECK(!btaptx_sep_scan(k_caps_apt_mono, sizeof(k_caps_apt_mono), &s), "mono only");
	CHECK(!btaptx_sep_scan(NULL, 0, &s) && !btaptx_sep_scan(k_caps_vendor7, 11, &s) && !btaptx_sep_scan(k_caps_vendor7, 3, &s), "an empty or truncated reply");
	memcpy(c, k_caps_vendor7, 17);
	c[3] = 0x20;
	CHECK(!btaptx_sep_scan(c, 17, &s), "an element running past the reply");
	memcpy(c, k_caps_vendor7, 17);
	c[6] = 0x4E;
	CHECK(!btaptx_sep_scan(c, 17, &s), "another vendor");
	memcpy(c, k_caps_vendor7, 17);
	c[10] = 0x02;
	CHECK(!btaptx_sep_scan(c, 17, &s), "another codec of that vendor");
	memcpy(c, "\x04\x02\x02\x00\x08\x00", 6);   /* content protection, delay reporting, then the vendor element */
	memcpy(c + 6, k_caps_vendor7 + 2, 11);
	CHECK(btaptx_sep_scan(c, 17, &s), "the element behind other categories");
	btaptx_sep_config(cfg);
	CHECK(!memcmp(cfg, k_apt_cfg, 13) && BTAPTX_CONFIG_LEN == 13, "the configuration: 01 00 07 09 00 FF 4F 00 00 00 01 00 12 (48 kHz, stereo)");
	/* what the headphones may configure on endpoint 4 */
	CHECK(btaptx_sep_check_config(k_apt_cfg, 13, &s, &cat) == 0, "the one configuration we advertise");
	static const struct { const char *what; uint8_t cfg[16]; int n; uint32_t cat, err; } bad[] = {
		{ "44.1 kHz", { 1, 0, 7, 9, 0, 0xFF, 0x4F, 0, 0, 0, 1, 0, 0x22 }, 13, 7, 0x29 },
		{ "both frequencies", { 1, 0, 7, 9, 0, 0xFF, 0x4F, 0, 0, 0, 1, 0, 0x32 }, 13, 7, 0x29 },
		{ "mono", { 1, 0, 7, 9, 0, 0xFF, 0x4F, 0, 0, 0, 1, 0, 0x11 }, 13, 7, 0x29 },
		{ "both channel modes", { 1, 0, 7, 9, 0, 0xFF, 0x4F, 0, 0, 0, 1, 0, 0x13 }, 13, 7, 0x29 },
		{ "another vendor's codec", { 1, 0, 7, 9, 0, 0xFF, 0xD7, 0, 0, 0, 0xAD, 0, 0x12 }, 13, 7, 0x29 },
		{ "AAC", { 1, 0, 7, 8, 0, 2, 0x80, 0, 0x84, 3, 0xE8, 0 }, 12, 7, 0x29 },
		{ "SBC", { 1, 0, 7, 6, 0, 0, 0x11, 0x15, 2, 0x35 }, 10, 7, 0x29 },
		{ "content protection", { 1, 0, 4, 2, 2, 0, 7, 9, 0, 0xFF, 0x4F, 0, 0, 0, 1, 0 }, 16, 4, 0x17 },
		{ "a short element", { 1, 0, 7, 8, 0, 0xFF, 0x4F, 0, 0, 0, 1, 0 }, 12, 7, 0x11 },
		{ "an element running past the end", { 1, 0, 7, 9, 0, 0xFF, 0x4F, 0, 0, 0 }, 10, 7, 0x11 },
		{ "no transport", { 7, 9, 0, 0xFF, 0x4F, 0, 0, 0, 1, 0, 0x12 }, 11, 1, 0x1A },
		{ "no codec", { 1, 0 }, 2, 7, 0x1A },
		{ "two codec elements", { 7, 9, 0, 0xFF, 0x4F, 0, 0, 0, 1, 0, 0x12, 7, 9 }, 13, 7, 0x11 },
		{ "a trailing byte", { 1, 0, 7, 9, 0, 0xFF, 0x4F, 0, 0, 0, 1, 0, 0x12, 0 }, 14, 0, 0x11 },
	};
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		uint32_t e = btaptx_sep_check_config(bad[i].cfg, (uint32_t)bad[i].n, &s, &cat);

		CHECK(e == bad[i].err && cat == bad[i].cat, "%s: error 0x%x category %u, wanted 0x%x and %u", bad[i].what, (unsigned)e, (unsigned)cat, (unsigned)bad[i].err, (unsigned)bad[i].cat);
	}
	CHECK(btaptx_packet_fits(512) && !btaptx_packet_fits(511), "a packet is 512 bytes");
}

static void apt_neg_world(void)
{
	world(2, 1);
	btaac_neg.want_aptx = 1;
}

static void t_apt_negotiation(void)
{
	uint8_t *conn = slot(2);
	uint8_t *rec = conn + BTAAC_C_REC_B;

	apt_neg_world();
	start_negotiation(conn);
	CHECK(btaac_neg.state == BTAAC_N_PROBING && !btaac_neg.have, "probing");
	all_caps(conn);   /* a headset: vendor 7 (aptX), AAC 6, SBC 5, vendor 9 */
	CHECK(btaac_neg.have && btaac_neg.aptx && btaac_neg.seid == 0x1C && btaac_neg.n_chosen == 1, "endpoint 7 (byte 0x1C) is chosen, as aptX: seid %02x", (unsigned)btaac_neg.seid);
	stock_narrows(conn);
	btaac_h2_after(conn, 8, conn + BTAAC_C_REC_B + 6, 14);
	CHECK(btaac_neg.state == BTAAC_N_PENDING && rec[5] == 0x1C && rec[4] == 0x04 && get32(rec, 0x18) == 13 && !memcmp(rec + 6, k_apt_cfg, 13) && rec[0x20] == 0x30,
	      "the record: endpoint %02x, length %u, the 13 aptX bytes", rec[5], (unsigned)get32(rec, 0x18));
	CHECK(btaac_neg.bps == 384000 && btaac_neg.config_len == 13 && btaac_neg.sbc_seid == 0x14 && btaac_neg.sbc_len == 14, "384 kb/s; the SBC choice kept for the fallback");
	CHECK(send_disp(conn, 2, 3, (const uint8_t *)"", 0) == g_disp_ret && btaac_neg.state == BTAAC_N_CONFIGURED && g_l2.calls == 0, "the sink accepts: configured as aptX");
	/* the same headset with AAC wanted: the AAC endpoint (6), as before */
	world(2, 1);
	start_negotiation(conn);
	all_caps(conn);
	CHECK(btaac_neg.have && !btaac_neg.aptx && btaac_neg.seid == 0x18, "AAC wanted: endpoint 6, not aptX");
	/* aptX wanted by a headset that has only SBC and AAC: nothing found, SBC */
	apt_neg_world();
	start_negotiation(conn);
	ask_caps(conn, 2, k_caps_aac6, sizeof(k_caps_aac6));
	ask_caps(conn, 4, k_caps_sbc5, sizeof(k_caps_sbc5));
	ask_caps(conn, 6, k_caps_vendor9, sizeof(k_caps_vendor9));
	stock_narrows(conn);
	btaac_h2_after(conn, 8, conn + BTAAC_C_REC_B + 6, 14);
	CHECK(!btaac_neg.have && btaac_neg.state == BTAAC_N_NONE && !btaac_neg.aptx && !memcmp(rec + 6, k_sbc_cfg, 14) && get32(rec, 0x18) == 14, "no aptX endpoint: the code's SBC is untouched");
	/* a headset that lists only a 44.1 kHz aptX: not used */
	apt_neg_world();
	start_negotiation(conn);
	ask_caps(conn, 0, k_caps_apt_44, sizeof(k_caps_apt_44));
	CHECK(!btaac_neg.have, "44.1 kHz only: not offered");
	/* the sink rejects: the SBC configuration again, the device is not offered a codec again this boot */
	apt_neg_world();
	start_negotiation(conn);
	all_caps(conn);
	stock_narrows(conn);
	btaac_h2_after(conn, 8, conn + BTAAC_C_REC_B + 6, 14);
	CHECK(send_disp(conn, 3, 3, (const uint8_t *)"\x01\x29", 2) == g_disp_ret && btaac_neg.state == BTAAC_N_FALLBACK && g_l2.calls == 1 && g_l2.acp == 0x14 && g_l2.len == 14 &&
	      !memcmp(g_l2.caps, k_sbc_cfg, 14) && btaac_banned((uintptr_t)conn), "rejected: SBC sent again, byte for byte; the device marked");
	/* a new connection of the same device is not offered aptX while it is marked */
	start_negotiation(conn);
	CHECK(btaac_neg.state == BTAAC_N_NONE, "marked: SBC");
}

static void t_apt_acceptor(void)
{
	uint8_t *conn;

	acc_world(1);
	btaac_neg.want_aptx = 1;
	conn = g_aconn;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 2) == 0 && g_stock.calls == 0 && g_nrep == 1 && g_rep[0].b == 6 && !memcmp(g_rep[0].data, "\x04\x00\x08\x08\x10\x00", 6),
	      "DISCOVER: the code's endpoints 1 and 2, then endpoint 4 (an aptX source, byte 0x10); the code does not see it");
	CHECK(btaac_neg.state == BTAAC_N_OFFERED && btaac_neg.acceptor && btaac_neg.aptx && btaac_neg.ep_byte == 0x10, "offered: aptX, endpoint byte 0x10");
	CHECK(cmd1(6, 2, 0x0C) == g_disp_ret && g_stock.calls == 1 && g_stock.n_unknown == 1 && g_nrep == 1, "endpoint 3 (AAC) is not offered: the code rejects it as unknown");
	CHECK(cmd1(8, 2, 0x10) == 0 && g_stock.calls == 1 && g_nrep == 2 && g_rep[1].sig == 2 && g_rep[1].b == 13 && !memcmp(g_rep[1].data, k_apt_cfg, 13), "endpoint 4's capabilities: answered here");
	{
		BtAacSink sk;

		CHECK(btaptx_sep_scan(g_rep[1].data + 2, 11, &sk), "the reply scans as aptX, 48 kHz stereo");
	}
	/* a configuration we do not offer: rejected here, the code never sees it */
	{
		uint8_t bad[13];

		memcpy(bad, k_apt_cfg, 13);
		bad[12] = 0x22;
		CHECK(send_conf(9, 0x10, 0x14, bad, 13) == 0 && g_stock.calls == 1 && g_nrep == 3 && g_rep[2].sig == 3 && g_rep[2].fmt == 0x3456 && g_rep[2].a == 7 && g_rep[2].b == 0x29 && REC_STATE() == 1 && btaac_neg.state == BTAAC_N_OFFERED,
		      "44.1 kHz refused with a reject; the record untouched");
		CHECK(send_conf(9, 0x10, 0x14, k_aac_cfg, 12) == 0 && g_nrep == 4 && g_stock.calls == 1 && g_rep[3].a == 7 && g_rep[3].b == 0x29, "an AAC configuration for endpoint 4 is refused too");
	}
	CHECK(send_conf(9, 0x10, 0x14, k_apt_cfg, 13) == g_disp_ret && g_stock.calls == 2 && g_stock.n_conf == 1 && g_stock.msg[2] == 0x04 && g_stock.msg[3] == 0x14, "the good one: the code ran on a substitute for endpoint 1");
	{
		const uint8_t *rec = conn + BTAAC_C_REC_B;

		CHECK(get32(rec, 0) == 5 && rec[5] == 0x14 && get32(rec, 0x18) == 13 && !memcmp(rec + 6, k_apt_cfg, 13), "the record: configured, the sink's endpoint, the 13 aptX bytes");
		CHECK(btaac_neg.state == BTAAC_N_CONFIGURED && btaac_neg.aptx && btaac_neg.bps == 384000 && btaac_neg.config_len == 13 && !memcmp((const void *)btaac_neg.config, k_apt_cfg, 13) &&
		      btaac_neg.n_acc_config == 1, "configured as aptX");
	}
	CHECK(cmd1(0xA, 6, 0x10) == g_disp_ret && g_stock.msg[2] == 4 && REC_STATE() == 6, "OPEN reaches the code as endpoint 1");
	put32(conn, BTAAC_C_REC_B, 8);
	CHECK(cmd1(0xB, 7, 0x10) == g_disp_ret && g_stock.msg[2] == 4 && REC_STATE() == 10, "START: streaming");
	CHECK(cmd1(0xC, 8, 0x10) == g_disp_ret && REC_STATE() == 1 && g_stock.n_badstate == 0, "CLOSE");
	CHECK(cmd1(0xD, 7, 0x0C) == g_disp_ret && g_stock.ptr == g_msg, "a command for endpoint 3 is the code's own on an aptX connection");
	/* aptX not wanted: endpoint 4 is not offered, and a sink that remembers it gets the code's reject; the AAC offer is as it was */
	acc_world(1);
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	btaac_disp_hook(g_aconn, g_chan, g_msg, 2);
	CHECK(!memcmp(g_rep[0].data, "\x04\x00\x08\x08\x0C\x00", 6) && !btaac_neg.aptx, "AAC wanted: endpoint 3 listed, as before");
	CHECK(cmd1(8, 2, 0x10) == g_disp_ret && g_stock.n_unknown == 1 && g_nrep == 1, "endpoint 4 is the code's to reject");
	/* the master switch off or the device marked: the code's own SBC-only answer */
	acc_world(0);
	btaac_neg.want_aptx = 1;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	CHECK(btaac_disp_hook(g_aconn, g_chan, g_msg, 2) == g_disp_ret && g_stock.n_discover == 1 && g_nrep == 0, "not wanted: the code answers");
}

/* the media hook: the sender's hook leaves the block, the media hook encodes and sends it */
static void apt_media_world(uint32_t mtu)
{
	media_world(mtu, BTAAC_N_CONFIGURED);
	btaac_neg.aptx = 1;
	btaac_neg.want_aptx = 1;
	apt_up();
}
static int16_t g_blk[1024];
static void blk_fill(int16_t first) { for (int i = 0; i < 1024; i++) g_blk[i] = (int16_t)(first + i); }

static void t_apt_media(void)
{
	uint8_t *conn = slot(1);
	uint8_t want[512];

	apt_media_world(672);
	/* no block left by the sender: nothing is sent and the encoder is not asked */
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0 && g_apt.encodes == 0 && btaac_neg.n_no_au == 1, "no block: an error number, nothing sent, no encode");
	/* a block: sent whole, 512 bytes, no header, the flag as it came, the code's own SBC buffer never */
	blk_fill(0x1234);
	btaac_pcm_block(g_blk);
	CHECK(btaac_neg.apt_ready && btaac_neg.n_apt_blocks == 1 && !memcmp(btaac_apt_pcm, g_blk, sizeof(g_blk)), "the sender's block is kept (copied)");
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_calls == 1 && g_apt.encodes == 1, "sent");
	for (int i = 0; i < 512; i++)
		want[i] = (uint8_t)(i + 0x34);
	CHECK(g_send_last.len == 512 && g_send_last.flag == 1 && g_send_last.buf != g_sbc && g_npkts == 1 && g_pkts[0].len == 512 && !memcmp(g_pkts[0].data, want, 512),
	      "one packet: the encoder's 512 bytes and nothing else (no RTP header, no payload header)");
	CHECK(btaac_neg.n_sent == 1 && btaac_neg.last_au_len == 512 && !btaac_neg.apt_ready, "counted; the block is used up");
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 1 && g_apt.encodes == 1, "a second call with no new block sends nothing");
	/* one block per call, in order, the flag passed on as the code gave it */
	for (int k = 0; k < 5; k++) {
		blk_fill((int16_t)(0x100 * (k + 1)));
		btaac_pcm_block(g_blk);
		CHECK(btaac_send_hook(conn, g_sbc, 600, (uint32_t)(k & 1)) == g_send_ret, "block %d", k);
		CHECK(g_send_last.len == 512 && g_send_last.flag == (uint32_t)(k & 1) && g_pkts[g_npkts - 1].data[0] == (uint8_t)(0x100 * (k + 1)), "block %d: its own bytes, flag %d", k, k & 1);
	}
	CHECK(g_apt.encodes == 6 && btaac_neg.n_sent == 6 && btaac_neg.n_pieces == 6, "six blocks, six encodes, six packets");
	/* a block the code drops (congestion: the media hook is not called) is overwritten by the next, counted, and never encoded */
	g_apt.encodes = 0;
	blk_fill(1); btaac_pcm_block(g_blk);
	blk_fill(2); btaac_pcm_block(g_blk);
	CHECK(btaac_neg.n_apt_late == 1 && btaac_neg.n_apt_blocks == 8 && g_apt.encodes == 0, "a block that was never sent is replaced, counted, never encoded");
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_apt.encodes == 1 && g_pkts[g_npkts - 1].data[0] == 2, "the newest block goes");
	/* the encoder refuses: nothing sent, counted */
	g_apt.encode_fail = 1;
	blk_fill(5); btaac_pcm_block(g_blk);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && btaac_neg.n_wrap_err == 1, "the encoder refused: nothing sent");
	g_apt.encode_fail = 0;
	/* the original's own error is passed back and counted, not sent twice */
	g_send_fail_at = g_send_calls;
	blk_fill(6); btaac_pcm_block(g_blk);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && btaac_neg.n_send_err == 1, "the code's send failed: its error number comes back");
	g_send_fail_at = -1;
	/* a link whose packet limit is under 512: a packet of this codec cannot be cut, nothing is sent */
	apt_media_world(300);
	blk_fill(7); btaac_pcm_block(g_blk);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0 && g_apt.encodes == 0 && btaac_neg.n_over == 1, "a limit of 300: nothing sent, not encoded");
	/* a limit that cannot be read is the default, 672 */
	apt_media_world(672);
	*(uintptr_t *)(conn + BTAAC_C_MEDIA_CH) = 0;
	blk_fill(8); btaac_pcm_block(g_blk);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_calls == 1, "no channel to read: the default limit, sent");
	/* the stream record not open: the routine's own condition */
	apt_media_world(672);
	put32(conn, BTAAC_C_REC_B, 8);
	blk_fill(9); btaac_pcm_block(g_blk);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0, "a stream record that is not open: nothing");
	/* the sender's hook leaves a block only while the gates are up, the state is configured as aptX and the stream is started */
	apt_media_world(672);
	btaac_neg.pcm_gate = 0;
	blk_fill(1); btaac_pcm_block(g_blk);
	CHECK(!btaac_neg.apt_ready, "gate down: no block kept");
	btaac_neg.pcm_gate = 1;
	put32(conn, BTAAC_C_PROFILES, 0);
	btaac_pcm_block(g_blk);
	CHECK(!btaac_neg.apt_ready, "not started: no block kept");
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	btaac_neg.state = BTAAC_N_PENDING;
	btaac_pcm_block(g_blk);
	CHECK(!btaac_neg.apt_ready, "not configured: no block kept");
	/* an AAC stream still sends AAC: the block goes to the engine's ring, not the stash */
	apt_media_world(672);
	btaac_neg.aptx = 0;
	btaac_pcm_block(g_blk);
	CHECK(!btaac_neg.apt_ready && btaac_neg.n_apt_blocks == 0, "an AAC stream: the aptX stash is not used");
	/* the code's SBC packet is never sent on an aptX stream with no session */
	apt_media_world(672);
	btaac_neg.send_gate = 0;
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0 && btaac_neg.n_swallowed == 1, "no session: the code's SBC packet is dropped");
}

static void apt_stream_world(void)
{
	stream_world();
	btaac_link_api = 0;   /* aptX works with the AAC module absent */
	apt_up();
	g_bteq_in = 1;
}
static void apt_up_stream(uint8_t *conn)
{
	btaac_neg.conn = (uintptr_t)conn;
	btaac_neg.state = BTAAC_N_CONFIGURED;
	btaac_neg.aptx = 1;
	btaac_neg.bps = 384000;
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	put32(conn, BTAAC_C_PROFILES, 0);
	*(uintptr_t *)(conn + BTAAC_C_MEDIA_CH) = (uintptr_t)g_chan;
	put32(g_chan, 8, 672);
}
static void apt_pass(void) { btt_aac_stream_pass(1, BTT_CODEC_APTX, 1); }

static void t_apt_worker(void)
{
	uint8_t *conn = sslot(2);

	stream_world();
	btaac_link_api = 0;
	btaptx_link_api = 0;
	/* no add-on: aptX wanted means SBC, and nothing is installed */
	apt_pass();
	CHECK(g_ninst == 1 && g_inst[0].hook == btaac_disp_hook && !btaac_neg.enable && !g_installed, "no aptX module: no codec is offered, only the reader of the codec lists is in");
	CHECK(btt_codec_effective(BTT_CODEC_APTX) == BTT_CODEC_SBC && btt_aptx_avail() == 0, "the effective codec is SBC, nothing available");
	/* the add-on there but its self-test not passed, or held */
	apt_up();
	g_apt.selftest = BTAPTX_ST_NOTRUN;
	CHECK(btt_codec_effective(BTT_CODEC_APTX) == BTT_CODEC_SBC && btt_aptx_avail() == BTT_APTX_AVAIL_MODULE, "module there, not tested: SBC");
	g_apt.selftest = BTAPTX_ST_FAIL;
	CHECK(btt_codec_effective(BTT_CODEC_APTX) == BTT_CODEC_SBC, "self-test failed: SBC");
	g_apt.selftest = BTAPTX_ST_PASS;
	g_apt.hold = 1;
	CHECK(btt_codec_effective(BTT_CODEC_APTX) == BTT_CODEC_SBC && btt_aptx_avail() == (BTT_APTX_AVAIL_MODULE | BTT_APTX_AVAIL_TESTED | BTT_APTX_AVAIL_HELD), "held: SBC, the bits say so");
	g_apt.hold = 0;
	CHECK(btt_codec_effective(BTT_CODEC_APTX) == BTT_CODEC_APTX && btt_aptx_avail() == (BTT_APTX_AVAIL_MODULE | BTT_APTX_AVAIL_TESTED), "usable");
	CHECK(btt_codec_effective(BTT_CODEC_SBC) == BTT_CODEC_SBC && btt_codec_effective(BTT_CODEC_AAC) == BTT_CODEC_SBC, "SBC stays SBC; AAC without its module is SBC");
	/* wanted: the hooks go in, the next headphones are offered aptX */
	apt_pass();
	CHECK(g_ninst == 2 && g_installed && btaac_neg.enable && btaac_neg.want_aptx, "installed; aptX offered");
	apt_up_stream(conn);
	apt_pass();
	CHECK(btt_aac_stream_fast() && !btt_aac_stream_is_aac() && g_apt.starts == 0 && !c_gates(), "an aptX stream not started: watched closely, no session; not AAC (the SBC send queue limits apply)");
	/* the stream starts: the session, then the gates (blocks first) */
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	apt_pass();
	CHECK(g_apt.starts == 1 && c_gates() && log_has("aptX is playing"), "session started once, gates up");
	apt_pass();
	CHECK(g_apt.starts == 1, "not started twice");
	/* a pause under 3 s keeps it, over 3 s ends it: gates down first, then the record cleared */
	put32(conn, BTAAC_C_PROFILES, 0);
	g_us += 2u * 1000000u;
	apt_pass();
	CHECK(c_gates() && g_apt.stops == 0, "2 s: kept");
	g_us += 1500000u;
	apt_pass();
	CHECK(!c_gates() && g_apt.stops == 1 && log_has("aptX session ended"), "over 3 s: ended");
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	apt_pass();
	CHECK(g_apt.starts == 2 && c_gates(), "started again: a new session");
	/* the link goes */
	put32(conn, BTAAC_C_REC_B, 0);
	apt_pass();
	CHECK(!c_gates() && g_apt.stops == 2 && log_has("aptX stream is gone"), "the record closed: ended");
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	apt_pass();
	CHECK(g_apt.starts == 3 && c_gates(), "back: a third session");
	/* standby: the session is over before the handler returns */
	CHECK(g_sev_handler && g_sev_handler(1, 0x20F, NULL, NULL) == 0 && c_gates(), "a wake-up changes nothing");
	CHECK(g_sev_handler(0, 0x101, NULL, NULL) == 0 && c_gates() && g_apt.stops == 2, "a cancelled attempt leaves it");
	CHECK(g_sev_handler(0, 0x20F, NULL, NULL) == 0 && !c_gates() && g_apt.stops == 3, "standby: the session ended, gates down");
	/* the add-on refuses a session: no sound, the device marked */
	apt_up();
	g_apt.start_ret = BTAPTX_ERR_HELD;
	apt_pass();
	CHECK(btaac_neg.state == BTAAC_N_BROKEN && !c_gates() && btaac_banned((uintptr_t)conn) && log_has("would not start"), "a refused session: broken, marked\n%s", g_log);
	CHECK(btaac_ban_bits(btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_LO), btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_HI)) == 0 &&
	      btaac_local_bits(btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_LO), btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_HI)) == BTAAC_BAN_APTX &&
	      log_has("not offered this start: the aptX add-on could not start a session"), "and the mark is the module's own: not a refusal by the headphones\n%s", g_log);
	/* module stop */
	apt_up();
	btaac_neg.state = BTAAC_N_CONFIGURED;
	btaac_neg.send_gate = btaac_neg.pcm_gate = 1;
	g_apt.starts = 0;
	apt_pass();
	btt_aac_stream_restore();
	CHECK(!c_gates() && !btaac_neg.enable && g_ninst >= 2 && g_nrel == 2, "module stop: gates down, hooks released");
}

static void t_apt_worker_switch(void)
{
	uint8_t *conn = sslot(2);

	/* an AAC connection ends and an aptX one begins: the AAC session of the first ends, whatever the new negotiation found */
	engine_up();
	run_selftest();
	stream_world();
	apt_up();
	pass(0);
	stream_up(conn);
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	pass_until(c_gates, 0, "the AAC gates");
	btaac_neg.aptx = 1;   /* the next connection negotiated aptX (neg_begin closed the AAC gates) */
	btaac_neg.send_gate = btaac_neg.pcm_gate = 0;
	apt_pass();
	wait_for(c_idle, "the AAC session to end");
	CHECK(g_apt.starts == 1 && c_gates(), "the AAC session ended, the aptX one started");
	engine_down();
}

static void t_apt_link_info(void)
{
	BtLinkIn in;
	BttLinkInfo k;

	memset(&in, 0, sizeof(in));
	in.on = 1; in.recognised = 1; in.connected = 1; in.streaming = 1; in.aptx_stream = 1; in.aac_bps = 123;
	btlink_compose(&in, &k);
	CHECK(k.codec == BTT_LINK_CODEC_APTX && k.bitrate_kbps == 384 && k.sample_rate == 48000 && k.accel == 0 && k.sbc_bitpool == 0, "aptX: codec, 384 kb/s, 48 kHz");
	in.aptx_stream = 0;
	in.aac_stream = 1; in.aac_bps = 256000;
	btlink_compose(&in, &k);
	CHECK(k.codec == BTT_LINK_CODEC_AAC && k.bitrate_kbps == 256, "AAC as before");
}

static void t_apt_link_find(void)
{
	/* the table is looked up once, and a table that does not match is refused */
	btaptx_link_api = 0;
	g_apt_find = 0;
	btt_aptx_link_find();
	CHECK(!btaptx_link_api && log_has("aptX module is not loaded"), "absent: logged once");
	g_looked = 0;
	g_apt_find = 2;
	btt_aptx_link_find();
	CHECK(!btaptx_link_api && log_has("does not match this build"), "a table of another version is refused");
	g_looked = 0;
	g_apt_find = 1;
	apt_up();
	btaptx_link_api = 0;
	btt_aptx_link_find();
	CHECK(btaptx_link_api == &k_apt_api && log_has("aptX module is loaded"), "found");
	btt_aptx_link_find();
	btt_aptx_link_sync(1, BTT_CODEC_APTX);
	CHECK(g_apt.syncs == 1, "the self-test is asked for through the table");
	btaptx_link_api = 0;
	btt_aptx_link_sync(1, BTT_CODEC_APTX);
	btt_aptx_link_sync(1, BTT_CODEC_APTX);
	CHECK(log_has("aptX module is not loaded: SBC is sent"), "aptX wanted, no module: said once");
}

/* ---- aptX HD (experimental): Vita-initiated only ---------------------------------------------------------------------------- */

/* A headset's HD endpoint: vendor 0x000000D7, codec 0x0024, 44.1 and 48 kHz stereo, the four reserved bytes, then the content protection offer. */
static const uint8_t k_caps_hd[21] = { 0x01, 0x00, 0x07, 0x0D, 0x00, 0xFF, 0xD7, 0x00, 0x00, 0x00, 0x24, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x04, 0x02, 0x02, 0x00 };
static const uint8_t k_hd_cfg[17] = { 0x01, 0x00, 0x07, 0x0D, 0x00, 0xFF, 0xD7, 0x00, 0x00, 0x00, 0x24, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00 };

static void t_apt_hd_sep(void)
{
	BtAacSink s;
	uint8_t c[40], cfg[BTAPTX_HD_CONFIG_LEN], pkt[BTAPTX_HD_RTP_HDR];

	CHECK(btaptx_sep_scan_hd(k_caps_hd, sizeof(k_caps_hd), &s) && s.object == 0 && s.max_bps == 0, "an aptX HD endpoint is found");
	CHECK(!btaptx_sep_scan_hd(k_caps_vendor7, sizeof(k_caps_vendor7), &s), "classic aptX is not HD");
	CHECK(!btaptx_sep_scan(k_caps_hd, sizeof(k_caps_hd), &s), "HD is not classic aptX (its length and codec id differ)");
	CHECK(!btaptx_sep_scan_hd(k_caps_vendor9, sizeof(k_caps_vendor9), &s), "another vendor codec (codec id 0xAD) is not HD");
	CHECK(!btaptx_sep_scan_hd(k_caps_aac6, sizeof(k_caps_aac6), &s) && !btaptx_sep_scan_hd(k_caps_sbc5, sizeof(k_caps_sbc5), &s), "AAC and SBC are not HD");
	memcpy(c, k_caps_hd, sizeof(k_caps_hd));
	c[12] = 0x22;
	CHECK(!btaptx_sep_scan_hd(c, sizeof(k_caps_hd), &s), "44.1 kHz only: no 48 kHz bit");
	c[12] = 0x31;
	CHECK(!btaptx_sep_scan_hd(c, sizeof(k_caps_hd), &s), "mono only");
	CHECK(!btaptx_sep_scan_hd(k_caps_hd, 14, &s) && !btaptx_sep_scan_hd(NULL, 0, &s), "truncated or empty");
	memcpy(c, k_caps_hd, sizeof(k_caps_hd));
	c[3] = 0x30;
	CHECK(!btaptx_sep_scan_hd(c, sizeof(k_caps_hd), &s), "an element running past the reply");
	btaptx_sep_config_hd(cfg);
	CHECK(!memcmp(cfg, k_hd_cfg, 17) && BTAPTX_HD_CONFIG_LEN == 17 && BTAPTX_HD_CONFIG_LEN <= BTAAC_CAPS_BUF && BTAPTX_HD_CONFIG_LEN <= BTAAC_CONFIG_BUF,
	      "the configuration: 01 00 07 0D 00 FF D7 00 00 00 24 00 12 00 00 00 00 (17 bytes, 48 kHz stereo, reserved bytes zero)");
	CHECK(BTAPTX_HD_BLOCK_FRAMES == 128 && BTAPTX_HD_BLOCK_BYTES == 768 && BTAPTX_HD_PACKET_MAX == 780 && BTAPTX_HD_KBPS == 576, "a block: 128 frames of 6 bytes, 768; whole, 780 with the header; 576 kb/s");
	/* how a block is cut into packets of whole frames, by the limit */
	CHECK(btaptx_hd_pieces(780) == 1 && btaptx_hd_pieces(1021) == 1 && btaptx_hd_pieces(779) == 2, "780 takes the block whole; 779 takes two packets");
	CHECK(btaptx_hd_pieces(672) == 2, "the default limit 672: two packets");
	CHECK(btaptx_hd_pieces(12 + 6 * 43) == 3 && btaptx_hd_pieces(12 + 6 * 42) == 4 && btaptx_hd_pieces(12 + 6 * 16) == 8 && btaptx_hd_pieces(12 + 6 * 15) == 0, "smaller limits: 3, 4, 8 packets, then none (under 16 frames a packet)");
	CHECK(btaptx_hd_pieces(0) == 0 && btaptx_hd_pieces(12) == 0 && btaptx_hd_pieces(17) == 0, "no room: none");
	for (uint32_t mtu = 100; mtu < 1100; mtu++) {
		uint32_t n = btaptx_hd_pieces(mtu), tot = 0;

		for (uint32_t k = 0; k < n; k++) {
			uint32_t f = btaptx_hd_piece_frames(k, n);

			CHECK(BTAPTX_HD_RTP_HDR + f * BTAPTX_HD_FRAME_BYTES <= mtu && f >= 1, "mtu %u: piece %u of %u has %u frames and fits", mtu, k, n, f);
			tot += f;
		}
		CHECK(n == 0 || tot == 128, "mtu %u: %u pieces carry the 128 frames exactly (%u)", mtu, n, tot);
	}
	CHECK(btaptx_hd_piece_frames(0, 3) == 43 && btaptx_hd_piece_frames(1, 3) == 43 && btaptx_hd_piece_frames(2, 3) == 42 && btaptx_hd_piece_frames(0, 2) == 64, "even pieces, the first take the extra");
	btaptx_hd_rtp(pkt, 0x1234, 0xA1B2C3D4u);
	CHECK(!memcmp(pkt, "\x80\x60\x12\x34\xA1\xB2\xC3\xD4\x00\x00\x00\x00", 12), "the RTP header: version 2, payload type 96, sequence and timestamp big endian, source 0");
}

static void apt_hd_neg_world(void)
{
	world(2, 1);
	btaac_neg.want_aptx = 1;
	btaac_neg.want_aptx_hd = 1;
	apt_hd_up();
}

static void t_apt_hd_negotiation(void)
{
	uint8_t *conn = slot(2);
	uint8_t *rec = conn + BTAAC_C_REC_B;

	/* a headset with classic aptX first and HD later: HD is preferred, the 17 bytes go into the record */
	apt_hd_neg_world();
	start_negotiation(conn);
	ask_caps(conn, 0, k_caps_vendor7, sizeof(k_caps_vendor7));
	CHECK(btaac_neg.have && btaac_neg.aptx && !btaac_neg.apt_hd && btaac_neg.seid == 0x1C, "classic aptX found first");
	ask_caps(conn, 2, k_caps_hd, sizeof(k_caps_hd));
	CHECK(btaac_neg.have && btaac_neg.aptx && btaac_neg.apt_hd && btaac_neg.seid == 0x18 && btaac_neg.n_chosen == 2, "HD found later replaces it: that endpoint (byte 0x18)");
	ask_caps(conn, 4, k_caps_sbc5, sizeof(k_caps_sbc5));
	stock_narrows(conn);
	btaac_h2_after(conn, 8, conn + BTAAC_C_REC_B + 6, 14);
	CHECK(btaac_neg.state == BTAAC_N_PENDING && rec[5] == (uint8_t)btaac_neg.seid && get32(rec, 0x18) == 17 && !memcmp(rec + 6, k_hd_cfg, 17), "the record: the HD endpoint, length 17, the HD bytes");
	CHECK(btaac_neg.bps == 576000 && btaac_neg.config_len == 17 && btaac_neg.sbc_seid == 0x14 && btaac_neg.sbc_len == 14, "576 kb/s; the SBC choice kept for the fallback");
	CHECK(send_disp(conn, 2, 3, (const uint8_t *)"", 0) == g_disp_ret && btaac_neg.state == BTAAC_N_CONFIGURED, "accepted: configured as aptX HD");
	/* HD first, then classic: HD stays */
	apt_hd_neg_world();
	start_negotiation(conn);
	ask_caps(conn, 0, k_caps_hd, sizeof(k_caps_hd));
	ask_caps(conn, 2, k_caps_vendor7, sizeof(k_caps_vendor7));
	CHECK(btaac_neg.apt_hd && btaac_neg.n_chosen == 1, "HD first: classic aptX later does not replace it");
	/* HD not wanted (its self-test not passed, or turned off): classic aptX, as in 0.3.0 without HD */
	apt_hd_neg_world();
	btaac_neg.want_aptx_hd = 0;
	start_negotiation(conn);
	ask_caps(conn, 0, k_caps_hd, sizeof(k_caps_hd));
	CHECK(!btaac_neg.have, "HD offered but not wanted, no classic endpoint: nothing found");
	ask_caps(conn, 2, k_caps_vendor7, sizeof(k_caps_vendor7));
	CHECK(btaac_neg.have && btaac_neg.aptx && !btaac_neg.apt_hd, "classic aptX used");
	stock_narrows(conn);
	btaac_h2_after(conn, 8, conn + BTAAC_C_REC_B + 6, 14);
	CHECK(get32(rec, 0x18) == 13 && !memcmp(rec + 6, k_apt_cfg, 13) && btaac_neg.bps == 384000, "the 13 classic bytes, 384 kb/s");
	/* AAC wanted: HD never considered */
	world(2, 1);
	btaac_neg.want_aptx_hd = 1;
	start_negotiation(conn);
	ask_caps(conn, 0, k_caps_hd, sizeof(k_caps_hd));
	CHECK(!btaac_neg.have && !btaac_neg.apt_hd, "AAC wanted: an HD endpoint is not an AAC one");
	/* the sink rejects the HD configuration: SBC again, HD turned off for the boot, the device is NOT marked (it keeps classic aptX) */
	apt_hd_neg_world();
	start_negotiation(conn);
	ask_caps(conn, 0, k_caps_vendor7, sizeof(k_caps_vendor7));
	ask_caps(conn, 2, k_caps_hd, sizeof(k_caps_hd));
	stock_narrows(conn);
	btaac_h2_after(conn, 8, conn + BTAAC_C_REC_B + 6, 14);
	CHECK(send_disp(conn, 3, 3, (const uint8_t *)"\x01\x29", 2) == g_disp_ret && btaac_neg.state == BTAAC_N_FALLBACK && g_l2.calls == 1 && !memcmp(g_l2.caps, k_sbc_cfg, 14) &&
	      btaac_neg.hd_off && !btaac_banned((uintptr_t)conn), "rejected: SBC sent again, HD off for the boot, the device not marked");
	/* the next connection: the worker no longer wants HD, so classic aptX is used */
	btaac_neg.want_aptx = 1;
	btaac_neg.want_aptx_hd = btaac_neg.want_aptx && !btaac_neg.hd_off && btt_aptx_hd_usable();
	start_negotiation(conn);
	ask_caps(conn, 0, k_caps_vendor7, sizeof(k_caps_vendor7));
	ask_caps(conn, 2, k_caps_hd, sizeof(k_caps_hd));
	CHECK(btaac_neg.have && btaac_neg.aptx && !btaac_neg.apt_hd, "after an HD failure the same headset gets classic aptX");
	/* a classic rejection still marks the device */
	apt_hd_neg_world();
	btaac_neg.want_aptx_hd = 0;
	start_negotiation(conn);
	ask_caps(conn, 0, k_caps_vendor7, sizeof(k_caps_vendor7));
	stock_narrows(conn);
	btaac_h2_after(conn, 8, conn + BTAAC_C_REC_B + 6, 14);
	CHECK(send_disp(conn, 3, 3, (const uint8_t *)"\x01\x29", 2) == g_disp_ret && btaac_banned((uintptr_t)conn) && !btaac_neg.hd_off, "classic aptX rejected: the device marked, as before");
}

static void t_apt_hd_avail(void)
{
	apt_up();
	CHECK(btt_aptx_avail() == (BTT_APTX_AVAIL_MODULE | BTT_APTX_AVAIL_TESTED) && !btt_aptx_hd_usable(), "classic only: no HD bit");
	apt_hd_up();
	CHECK(btt_aptx_avail() == (BTT_APTX_AVAIL_MODULE | BTT_APTX_AVAIL_TESTED | BTT_APTX_AVAIL_HD) && btt_aptx_hd_usable(), "both self-tests passed: HD usable");
	g_apt.hd_ctx = 0;
	CHECK(!btt_aptx_hd_usable(), "no HD context: not usable");
	apt_hd_up();
	g_apt.hold = 1;
	CHECK(!btt_aptx_hd_usable(), "held off: not usable");
	apt_hd_up();
	g_apt.selftest_hd = BTAPTX_ST_FAIL;
	CHECK(!btt_aptx_hd_usable() && btt_codec_effective(BTT_CODEC_APTX) == BTT_CODEC_APTX, "the HD self-test failed: classic aptX stays offered");
	btaptx_link_api = 0;
	CHECK(!btt_aptx_hd_usable(), "no module: not usable");
}

/* the media hook for an HD stream: the block goes out as RTP packets of whole frames, as many as the media channel's limit needs, all in the one call */
static void apt_hd_media_world(uint32_t mtu)
{
	apt_media_world(mtu);
	apt_hd_up();
	btaac_neg.apt_hd = 1;
	btaac_neg.seq = 0xFFFE;
	btaac_neg.ts = 1000;
}

static void t_apt_hd_media(void)
{
	uint8_t *conn = slot(1);
	uint8_t want[768];
	uint32_t off;

	/* a limit of 780 or more: the whole block in one packet */
	apt_hd_media_world(780);
	blk_fill(0x1234);
	btaac_pcm_block(g_blk);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_calls == 1 && g_apt.hd_encodes == 1 && g_apt.encodes == 0, "sent, by the HD encoder only");
	for (int i = 0; i < 768; i++)
		want[i] = (uint8_t)(i * 3 + 0x34);
	CHECK(g_npkts == 1 && g_pkts[0].len == 780 && !memcmp(g_pkts[0].data, "\x80\x60\xFF\xFE\x00\x00\x03\xE8\x00\x00\x00\x00", 12) && !memcmp(g_pkts[0].data + 12, want, 768) && g_pkts[0].flag == 1,
	      "one packet: 12 header bytes (sequence 0xFFFE, timestamp 1000) and the 768 bytes, the flag as given");
	CHECK(btaac_neg.seq == 0xFFFF && btaac_neg.ts == 1512 && btaac_neg.n_sent == 1 && btaac_neg.n_pieces == 1 && btaac_neg.last_au_len == 768, "the timeline: 512 frames of sound, one packet");
	/* the default limit 672: two packets of 64 frames, the flag on the last only, numbered and timed in order */
	apt_hd_media_world(672);
	blk_fill(0x0102);
	btaac_pcm_block(g_blk);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_calls == 2 && g_npkts == 2, "672: two packets in the one call");
	CHECK(g_pkts[0].len == 12 + 384 && g_pkts[1].len == 12 + 384 && g_pkts[0].flag == 0 && g_pkts[1].flag == 1, "64 frames each; the flag only on the last");
	CHECK(!memcmp(g_pkts[0].data, "\x80\x60\xFF\xFE\x00\x00\x03\xE8", 8) && !memcmp(g_pkts[1].data, "\x80\x60\xFF\xFF\x00\x00\x04\xE8", 8),
	      "sequence 0xFFFE, 0xFFFF; timestamps 1000 and 1256 (256 frames of sound each)");
	for (int i = 0; i < 768; i++)
		want[i] = (uint8_t)(i * 3 + 0x02);
	CHECK(!memcmp(g_pkts[0].data + 12, want, 384) && !memcmp(g_pkts[1].data + 12, want + 384, 384), "together, the block's 768 bytes in order, split on a frame boundary");
	CHECK(btaac_neg.seq == 0 && btaac_neg.ts == 1512 && btaac_neg.n_sent == 1 && btaac_neg.n_pieces == 2 && btaac_neg.n_back == 1, "the sequence wrapped; one block, two pieces");
	/* an odd limit: three packets of 43, 43 and 42 frames */
	apt_hd_media_world(12 + 6 * 43);
	blk_fill(5);
	btaac_pcm_block(g_blk);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 0) == g_send_ret && g_npkts == 3 && g_pkts[0].len == 12 + 258 && g_pkts[1].len == 12 + 258 && g_pkts[2].len == 12 + 252, "43, 43, 42 frames");
	off = 0;
	for (int k = 0; k < 3; k++)
		off += g_pkts[k].len - 12;
	CHECK(off == 768, "the packets carry all 768 bytes");
	/* the smallest limit the module believes (256) still carries a block, in four packets; a smaller one is not believed: the default */
	apt_hd_media_world(256);
	blk_fill(7);
	btaac_pcm_block(g_blk);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_npkts == 4 && g_pkts[0].len <= 256 && g_pkts[3].len <= 256 && g_pkts[0].len == 12 + 32 * 6, "a limit of 256: four packets of 32 frames, each within it");
	apt_hd_media_world(100);
	blk_fill(7);
	btaac_pcm_block(g_blk);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_npkts == 2 && btaac_neg.last_mtu == 672, "a limit of 100 is not believed: the default, two packets");
	/* a limit that cannot be read is the default (two packets) */
	apt_hd_media_world(780);
	*(uintptr_t *)(conn + BTAAC_C_MEDIA_CH) = 0;
	blk_fill(8);
	btaac_pcm_block(g_blk);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_npkts == 2, "no channel to read: the default limit, two packets");
	/* the encoder refuses */
	apt_hd_media_world(780);
	g_apt.encode_fail = 1;
	blk_fill(9);
	btaac_pcm_block(g_blk);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0 && btaac_neg.n_wrap_err == 1, "the encoder refused: nothing sent");
	g_apt.encode_fail = 0;
	/* the code's send fails on the first piece: the second is not sent, the error comes back */
	apt_hd_media_world(672);
	g_send_fail_at = g_send_calls;
	blk_fill(10);
	btaac_pcm_block(g_blk);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && btaac_neg.n_send_err == 1 && g_npkts == 0 && btaac_neg.n_sent == 0, "a failed send stops the block");
	g_send_fail_at = -1;
	/* the sender's hook keeps the 512-frame block as for classic aptX */
	apt_hd_media_world(780);
	blk_fill(0x20);
	btaac_pcm_block(g_blk);
	CHECK(btaac_neg.apt_ready && !memcmp(btaac_apt_pcm, g_blk, sizeof(g_blk)), "the block is kept, copied");
}

static void t_apt_hd_worker(void)
{
	uint8_t *conn = sslot(2);

	stream_world();
	btaac_link_api = 0;
	apt_hd_up();
	g_bteq_in = 1;
	apt_pass();
	CHECK(btaac_neg.enable && btaac_neg.want_aptx && btaac_neg.want_aptx_hd, "aptX chosen and the HD self-test passed: HD preferred");
	apt_up();
	apt_pass();
	CHECK(btaac_neg.want_aptx && !btaac_neg.want_aptx_hd, "HD self-test not passed: classic only");
	apt_hd_up();
	btaac_neg.hd_off = 1;
	apt_pass();
	CHECK(!btaac_neg.want_aptx_hd, "HD turned off for the boot");
	btaac_neg.hd_off = 0;
	/* aptX HD When Available: off keeps standard aptX whatever the self-test says; on (the default) prefers HD again; the classic leaf is still chosen */
	btt_aac_stream_set_aptx_hd(0);
	apt_pass();
	CHECK(btaac_neg.enable && btaac_neg.want_aptx && !btaac_neg.want_aptx_hd, "aptX HD When Available off: classic aptX offered, HD not");
	btt_aac_stream_set_aptx_hd(1);
	apt_pass();
	CHECK(btaac_neg.want_aptx && btaac_neg.want_aptx_hd, "aptX HD When Available on: HD preferred again");
	btt_aac_stream_set_aptx_hd(0);
	btt_aac_stream_pass(1, BTT_CODEC_SBC, 1);
	btt_aac_stream_set_aptx_hd(1);
	/* an HD connection: the session is asked for in HD mode */
	apt_up_stream(conn);
	btaac_neg.apt_hd = 1;
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	apt_pass();
	CHECK(g_apt.starts == 1 && g_apt.last_hd == 1 && c_gates() && log_has("aptX HD is playing"), "the add-on's session starts in HD mode");
	/* the add-on will not start it: broken, HD off, the device keeps classic aptX */
	apt_hd_up();
	g_apt_run = 0;
	btaac_neg.send_gate = btaac_neg.pcm_gate = 0;
	g_apt.start_ret = BTAPTX_ERR_HELD;
	apt_pass();
	CHECK(btaac_neg.state == BTAAC_N_BROKEN && btaac_neg.hd_off && !btaac_banned((uintptr_t)conn), "a refused HD session: broken, HD off, the device not marked");
}

static void t_apt_hd_link_info(void)
{
	BtLinkIn in;
	BttLinkInfo k;

	memset(&in, 0, sizeof(in));
	in.on = 1; in.recognised = 1; in.connected = 1; in.streaming = 1; in.aptx_stream = 1; in.aptx_hd = 1;
	btlink_compose(&in, &k);
	CHECK(k.codec == BTT_LINK_CODEC_APTX && k.bitrate_kbps == 576 && k.sample_rate == 48000, "aptX HD: the codec stays aptX, 576 kb/s");
	in.aptx_hd = 0;
	btlink_compose(&in, &k);
	CHECK(k.codec == BTT_LINK_CODEC_APTX && k.bitrate_kbps == 384, "classic: 384 kb/s");
}
