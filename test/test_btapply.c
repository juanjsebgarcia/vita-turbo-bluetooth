/*
 * test_btapply.c - host test of the settings that apply at once: the decisions (kernel/btapply_core.h) and their system side (kernel/btapply.c),
 * compiled as they are, with the Bluetooth code's calls, the audio link's readings and the clock replaced by fakes. One tick is half a second,
 * as the worker's pass.
 *
 *   make test      (cc -Itest/host test/test_btapply.c, then run)
 *
 * What is proved:
 *   - a change of Bluetooth Codec, AAC Bit Rate, Hardware Accelerated AAC or SBC Audio Quality on a connected link waits 1.5 s after the LAST change, then
 *     holds the no-mute mode, disconnects, waits for "no link" and one second more, connects, waits for the link and releases the mode after the tail: one
 *     disconnect call and one connect call, in that order, with the headphones' address;
 *   - scrolling through values (a change every half second) is one reconnect, and a list put back where it was is none;
 *   - no reconnect, with the reason logged, for: a bit rate or acceleration change on an SBC link; a quality change on an AAC link; a rate the headphones
 *     already get (their maximum); an acceleration change with NEON not allowed or the encoder not running; AAC asked of headphones that were refused or
 *     marked; AAC asked when the AAC module is not usable; the master switch off; no link; the voice link having the link (also when it starts during the
 *     hold); Reconnect After Standby at work; no address; the calls not found; a link that never settles; a change on an unrelated setting;
 *   - the no-mute mode is asked for before the disconnect and held until the tail is over; if it never comes in nothing is called;
 *   - failures are one attempt, logged and left: the disconnect refused, the link not going down, the connect refused, the link not coming back; the
 *     mode is still held for the tail, then released;
 *   - aptX HD When Available: on an aptX link, a change of the effective choice (HD to standard, or standard to HD when the headphones list HD and the HD
 *     encoder is usable) is one reconnect; none when the headphones do not list HD, when HD was offered at this connection and they came up on standard
 *     aptX, when the HD encoder is not usable or HD is off for this start, on a link that is not aptX, or for a scroll that ends where it began;
 *   - AAC asked of headphones that come back on SBC is remembered (not repeated) and the apply is still counted as done;
 *   - a change while an apply runs is noted and compared once the new link is settled (applied again only if it still differs);
 *   - Refresh Codecs (the device page's button): for headphones whose codec list is not known it is a forced apply with no setting changed (the same
 *     calls, in the same order, the no-mute mode held), logged at the press and at the end (the list read, or not); and none (logged, nothing called) when
 *     the list is known, when a reconnect is already pending or running (one at a time; a second press is refused at once), during a call or microphone,
 *     with no headphones, with Bluetooth Turbo off, during a Reconnect After Standby, with no address or no calls; a press after the one that ran
 *     is over works again;
 */
#define BTT_HOST_TEST 1
#include <btt_host.h>
#include <stdlib.h>

#include "../include/bluetoothturbo.h"
#include "../kernel/btlink.h"

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

/* ---- fakes ---------------------------------------------------------------------------------- */

static uint64_t g_now;   /* microseconds */
uint64_t ksceKernelGetSystemTimeWide(void) { return g_now; }
static char g_log[64][200];
static int g_nlog;
void btt_klog_line(const char *line)
{
	if (g_nlog < 64)
		snprintf(g_log[g_nlog++], sizeof(g_log[0]), "%s", line);
	if (getenv("BTT_TEST_VERBOSE"))
		printf("    [%6.1f] %s\n", (double)g_now / 1e6, line);
}
static int log_has(const char *needle)
{
	for (int i = 0; i < g_nlog; i++)
		if (strstr(g_log[i], needle))
			return 1;
	return 0;
}

/* the audio link, as btlink.c would publish it */
static BttLinkInfo g_li;
static BtLinkExtra g_x;
void btt_link_read(BttLinkInfo *out) { *out = g_li; }
void btt_link_extra(BtLinkExtra *out) { *out = g_x; }
static int g_avail, g_standby, g_disc_in;
int btt_aac_avail(void) { return g_avail; }
int btt_aac_codec_effective(int codec)
{
	return codec == BTT_CODEC_AAC && (g_avail & BTT_AAC_AVAIL_MODULE) && (g_avail & BTT_AAC_AVAIL_TESTED) && !(g_avail & BTT_AAC_AVAIL_HELD) ? BTT_CODEC_AAC : BTT_CODEC_SBC;
}
static int g_apt_ok;
int btt_aptx_codec_effective(int codec) { return codec == BTT_CODEC_APTX && g_apt_ok ? BTT_CODEC_APTX : BTT_CODEC_SBC; }
static int g_hd_ok;   /* the HD encoder's self-test passed and it is not held off */
int btt_aptx_hd_usable(void) { return g_hd_ok; }
static int g_ld_ok;
int btt_ldac_codec_effective(int codec) { return codec == BTT_CODEC_LDAC && g_ld_ok ? BTT_CODEC_LDAC : BTT_CODEC_SBC; }
int btt_btreconn_busy(void) { return g_standby; }
int btt_btdisc_in(void) { return g_disc_in; }

/* the Bluetooth code's calls */
static int g_calls_found = 1, g_disc_ret, g_conn_ret, g_state, g_conn_busy;   /* g_conn_busy: the next connect calls answer "busy" (0x802F0204) this many times */
static int g_ncalls, g_order[8];
static uint64_t g_conn_at[8];   /* when each connect call was made (us), by its number from 1 */
static uint32_t g_call_lo[8], g_call_hi[8];
static int g_disc_calls, g_conn_calls, g_lookups;
int btcall_find(void) { return g_calls_found ? 0 : -1; }
int btcall_find_disconnect(void) { g_lookups++; return g_calls_found ? 0 : -1; }
int btcall_up(void) { return 1; }
int btcall_state(uint32_t lo, uint32_t hi) { return g_state; }
int btcall_connect(uint32_t lo, uint32_t hi)
{
	CHECK(g_ncalls < 8, "too many calls");
	g_order[g_ncalls] = 2; g_call_lo[g_ncalls] = lo; g_call_hi[g_ncalls] = hi; g_ncalls++; g_conn_calls++;
	g_conn_at[g_conn_calls < 8 ? g_conn_calls : 7] = g_now;
	if (g_conn_busy > 0) {
		g_conn_busy--;
		return (int)0x802F0204;
	}
	return g_conn_ret;
}
int btcall_disconnect(uint32_t lo, uint32_t hi)
{
	CHECK(g_ncalls < 8, "too many calls");
	g_order[g_ncalls] = 1; g_call_lo[g_ncalls] = lo; g_call_hi[g_ncalls] = hi; g_ncalls++; g_disc_calls++;
	return g_disc_ret;
}

#include "../kernel/btaac_neg.c"
#include "../kernel/btapply.c"

/* ---- a headset on a console --------------------------------------------------------------- */

#define MAC_LO 0x11223344u
#define MAC_HI 0x5566u

static int g_on, g_codec, g_kbps, g_accel, g_quality, g_hd_set = 1;   /* the settings; aptX HD When Available is on by default */
static unsigned g_up_kbps;   /* the bit rate a link that comes back reports (aptX HD 576, aptX 384, 0 for the others) */
static unsigned g_list_on_up;   /* the codec list the console reads when it opens the next stream (0: none) */

static void link_up(int codec)
{
	memset(&g_li, 0, sizeof(g_li));
	memset(&g_x, 0, sizeof(g_x));
	g_li.connected = 1;
	g_li.codec = (unsigned)codec;
	g_li.codec_mask = g_list_on_up;
	g_x.mac_ok = 1; g_x.mac_lo = MAC_LO; g_x.mac_hi = MAC_HI;
	if (codec == BTT_LINK_CODEC_AAC) {
		g_x.bps = 256000; g_x.max_bps = 264600; g_x.aac_running = 1;
		g_li.accel = 1;
	}
	g_state = 5;
}
static void link_down(void)
{
	memset(&g_li, 0, sizeof(g_li));
	memset(&g_x, 0, sizeof(g_x));
	g_state = BTCALL_STATE_NO_LINK;
}

static void reset(void)
{
	g_now = 1000ull * 1000000;
	g_nlog = 0; g_ncalls = 0; g_disc_calls = g_conn_calls = 0;
	g_avail = BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_TESTED | BTT_AAC_AVAIL_NEON;
	g_apt_ok = 1;
	g_hd_ok = 1;
	g_standby = 0; g_disc_in = 0; g_calls_found = 1; g_disc_ret = 0; g_conn_ret = 0; g_conn_busy = 0;
	memset(&ga, 0, sizeof(ga));
	ga_calls = -1; ga_logged = 0; ga_manual = 0; ga_refresh = 0; g_list_on_up = 0;
	memset((void *)&btaac_neg, 0, sizeof(btaac_neg));
	btaac_neg.enable = 1;   /* the stream code is offering the wanted codec (its hooks are in): a link that comes up on SBC is then the headphones' doing */
	g_on = 1; g_codec = BTT_CODEC_SBC; g_kbps = 256; g_accel = 1; g_quality = BTT_Q_STANDARD; g_hd_set = 1; g_up_kbps = 0;
}

/* a local mark of the stream code for the headphones with this address (btaac_local_for takes a link; the test makes the table entry the same way) */
static void btaac_local_for_test(uint32_t lo, uint32_t hi, uint32_t bit)
{
	uint32_t i = btaac_neg.ban_n;

	btaac_neg.ban_lo[i] = lo; btaac_neg.ban_hi[i] = hi; btaac_neg.ban_cdc[i] = bit << BTAAC_LOCAL_SHIFT; btaac_neg.ban_n = i + 1;
}

/* one worker pass: half a second. The "no mute" mode follows the hold as the worker's btdisc_sync does (one pass later). */
static int g_disc_follow;
static void tick(void)
{
	g_now += 500000;
	btt_btapply_pass(g_on, g_codec, g_kbps, g_accel, g_quality, g_hd_set);
	g_disc_in = g_disc_follow ? (btt_btapply_hold() ? BTT_DISC_SPEAKER : 0) : g_disc_in;
}
static void ticks(int n) { while (n-- > 0) tick(); }

/* A link that has been up 30 s, settled, with the settings as given. */
static void settled(int codec)
{
	g_codec = codec == BTT_LINK_CODEC_AAC ? BTT_CODEC_AAC : BTT_CODEC_SBC;   /* the settings match the link */
	link_up(codec);
	ticks(60);
}

/* What the console does after our disconnect and connect, when they work: the link goes down at once, comes back 2 s after the connect. */
static void run_apply(int new_codec, int secs_to_down, int secs_to_back)
{
	int t = 0, down = 0, up = 0, connected_at = -1;

	for (int i = 0; i < 400; i++) {
		tick();
		t++;
		if (g_disc_calls && !down && t >= secs_to_down * 2) { link_down(); down = 1; }
		if (g_conn_calls && connected_at < 0) connected_at = t;
		if (connected_at >= 0 && !up && t >= connected_at + secs_to_back * 2) { link_up(new_codec); g_li.bitrate_kbps = g_up_kbps; up = 1; }
		if (ga.phase == BTAPPLY_PH_IDLE && !ga.pending && (ga.n_ok || ga.n_fail || ga.n_skip) && i > 10 && !btt_btapply_hold())
			break;
	}
}

/* ---- scenarios --------------------------------------------------------------------------- */

static int scenario_codec_to_aac(void)
{
	reset();
	g_disc_follow = 1;
	settled(BTT_LINK_CODEC_SBC);
	g_codec = BTT_CODEC_AAC;
	tick();
	CHECK(ga.pending, "the change was not noted");
	ticks(2);
	CHECK(ga.phase == BTAPPLY_PH_IDLE && g_ncalls == 0, "started inside the debounce (phase %d, calls %d)", ga.phase, g_ncalls);
	run_apply(BTT_LINK_CODEC_AAC, 1, 2);
	CHECK(g_ncalls == 2 && g_order[0] == 1 && g_order[1] == 2, "calls: %d, order %d %d", g_ncalls, g_order[0], g_order[1]);
	CHECK(g_call_lo[0] == MAC_LO && g_call_hi[0] == MAC_HI && g_call_lo[1] == MAC_LO && g_call_hi[1] == MAC_HI, "wrong address");
	CHECK(ga.n_start == 1 && ga.n_ok == 1 && ga.n_fail == 0, "start %u ok %u fail %u", ga.n_start, ga.n_ok, ga.n_fail);
	CHECK(!btt_btapply_hold() && g_disc_in == 0, "the no-mute mode was not released");
	CHECK(log_has("setting is in force"), "no log line for the end");
	return 0;
}

/* aptX: the same apply as AAC, with a codec that has no rate, no accelerated build and no SBC settings */
static void settled_codec(int link_codec, int setting)
{
	link_up(link_codec);
	g_codec = setting;
	ticks(60);
}

static int scenario_ldac(void)
{
	reset();
	g_ld_ok = 1;
	g_disc_follow = 1;
	/* SBC link, LDAC chosen: a reconnect, and the link comes back as LDAC */
	settled_codec(BTT_LINK_CODEC_SBC, BTT_CODEC_SBC);
	g_codec = BTT_CODEC_LDAC;
	tick();
	CHECK(ga.pending, "the change to LDAC was not noted");
	run_apply(BTT_LINK_CODEC_LDAC, 1, 2);
	CHECK(g_ncalls == 2 && g_order[0] == 1 && g_order[1] == 2 && ga.n_start == 1 && ga.n_ok == 1 && ga.last == BTAPPLY_R_OK, "SBC -> LDAC: %d calls, ok %u last %d", g_ncalls, ga.n_ok, ga.last);
	/* on an LDAC link nothing is reconnected for a setting: the LDAC bit rate goes to the running stream at once, the AAC rate, the accelerated build and the SBC quality are not used */
	reset();
	g_ld_ok = 1;
	settled_codec(BTT_LINK_CODEC_LDAC, BTT_CODEC_LDAC);
	g_kbps = 320; g_accel = 0; g_quality = BTT_Q_HIGH;
	ticks(8);
	CHECK(g_ncalls == 0 && ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "an LDAC link was reconnected for a setting it does not use (last %d)", ga.last);
	/* LDAC link, SBC, AAC or aptX chosen: a reconnect */
	g_codec = BTT_CODEC_SBC;
	g_disc_follow = 1;
	run_apply(BTT_LINK_CODEC_SBC, 1, 2);
	CHECK(g_ncalls == 2 && ga.n_ok == 1, "LDAC -> SBC: %d calls", g_ncalls);
	reset();
	g_ld_ok = 1;
	g_apt_ok = 1;
	settled_codec(BTT_LINK_CODEC_LDAC, BTT_CODEC_LDAC);
	g_codec = BTT_CODEC_APTX;
	g_disc_follow = 1;
	run_apply(BTT_LINK_CODEC_APTX, 1, 2);
	CHECK(g_ncalls == 2 && ga.n_ok == 1, "LDAC -> aptX: %d calls", g_ncalls);
	/* LDAC chosen but the module is not usable: nothing to apply (the effective codec is SBC) */
	reset();
	g_ld_ok = 0;
	settled_codec(BTT_LINK_CODEC_SBC, BTT_CODEC_SBC);
	g_codec = BTT_CODEC_LDAC;
	ticks(8);
	CHECK(g_ncalls == 0 && ga.n_start == 0, "LDAC chosen with no usable module reconnected the headphones");
	/* headphones that came up on SBC although LDAC was wanted (no LDAC endpoint, or a limit under 679): not reconnected again for it */
	reset();
	g_ld_ok = 1;
	g_codec = BTT_CODEC_LDAC;
	link_down();
	tick();   /* the machinery is running with LDAC ready when the headphones connect: this is their doing, not an early connection */
	link_up(BTT_LINK_CODEC_SBC);
	ticks(60);
	g_kbps = 128;
	ticks(8);
	CHECK(ga.n_start == 0 && ga.last == BTAPPLY_R_DECLINED, "a setting that has no effect on SBC reconnected headphones that did not take LDAC (last %d)", ga.last);
	/* a marked device is not offered it again */
	reset();
	g_ld_ok = 1;
	settled_codec(BTT_LINK_CODEC_SBC, BTT_CODEC_SBC);
	btaac_neg.ban_n = 1; btaac_neg.ban_lo[0] = MAC_LO; btaac_neg.ban_hi[0] = MAC_HI; btaac_neg.ban_cdc[0] = BTAAC_BAN_LDAC;
	g_codec = BTT_CODEC_LDAC;
	ticks(8);
	CHECK(g_ncalls == 0 && ga.n_start == 0, "a marked device was reconnected for LDAC");
	g_ld_ok = 0;
	return 0;
}

static int scenario_aptx(void)
{
	reset();
	g_disc_follow = 1;
	/* SBC link, aptX chosen: a reconnect, and the link comes back as aptX */
	settled_codec(BTT_LINK_CODEC_SBC, BTT_CODEC_SBC);
	g_codec = BTT_CODEC_APTX;
	tick();
	CHECK(ga.pending, "the change to aptX was not noted");
	run_apply(BTT_LINK_CODEC_APTX, 1, 2);
	CHECK(g_ncalls == 2 && g_order[0] == 1 && g_order[1] == 2 && ga.n_start == 1 && ga.n_ok == 1 && ga.last == BTAPPLY_R_OK, "SBC -> aptX: %d calls, ok %u last %d", g_ncalls, ga.n_ok, ga.last);
	/* on an aptX link nothing but the codec matters: the AAC bit rate, the accelerated setting and the SBC quality change nothing */
	reset();
	settled_codec(BTT_LINK_CODEC_APTX, BTT_CODEC_APTX);
	g_kbps = 320; g_accel = 0; g_quality = BTT_Q_HIGH;
	ticks(8);
	CHECK(g_ncalls == 0 && ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "an aptX link was reconnected for a setting it does not use (last %d)", ga.last);
	/* aptX link, SBC chosen: a reconnect */
	g_codec = BTT_CODEC_SBC;
	g_disc_follow = 1;
	run_apply(BTT_LINK_CODEC_SBC, 1, 2);
	CHECK(g_ncalls == 2 && ga.n_ok == 1, "aptX -> SBC: %d calls", g_ncalls);
	/* aptX link, AAC chosen: a reconnect (and the reverse) */
	reset();
	settled_codec(BTT_LINK_CODEC_APTX, BTT_CODEC_APTX);
	g_codec = BTT_CODEC_AAC;
	g_disc_follow = 1;
	run_apply(BTT_LINK_CODEC_AAC, 1, 2);
	CHECK(g_ncalls == 2 && ga.n_ok == 1, "aptX -> AAC: %d calls", g_ncalls);
	reset();
	settled_codec(BTT_LINK_CODEC_AAC, BTT_CODEC_AAC);
	g_codec = BTT_CODEC_APTX;
	g_disc_follow = 1;
	run_apply(BTT_LINK_CODEC_APTX, 1, 2);
	CHECK(g_ncalls == 2 && ga.n_ok == 1, "AAC -> aptX: %d calls", g_ncalls);
	/* aptX chosen but the module is not usable: nothing to apply (the effective codec is SBC) */
	reset();
	g_apt_ok = 0;
	settled_codec(BTT_LINK_CODEC_SBC, BTT_CODEC_SBC);
	g_codec = BTT_CODEC_APTX;
	ticks(8);
	CHECK(g_ncalls == 0 && ga.n_start == 0, "aptX chosen with no usable module reconnected the headphones");
	/* headphones that came up on SBC although aptX was wanted: not reconnected again for it; only an SBC quality change can differ */
	reset();
	g_codec = BTT_CODEC_APTX;
	link_down(); tick();   /* the machinery runs, the codec ready, when the headphones connect: their doing, not an early link */
	link_up(BTT_LINK_CODEC_SBC);
	ticks(60);
	g_quality = BTT_Q_HIGH;
	g_codec = BTT_CODEC_APTX;
	ticks(8);
	CHECK(ga.n_start == 1, "headphones that took SBC with aptX wanted: an SBC quality change is a real change (start %u, last %d)", ga.n_start, ga.last);
	reset();
	g_codec = BTT_CODEC_APTX;
	link_down(); tick();   /* the machinery runs, the codec ready, when the headphones connect: their doing, not an early link */
	link_up(BTT_LINK_CODEC_SBC);
	ticks(60);
	g_kbps = 128;
	ticks(8);
	CHECK(ga.n_start == 0 && ga.last == BTAPPLY_R_DECLINED, "a setting that has no effect on SBC reconnected headphones that did not take aptX (last %d)", ga.last);
	/* a marked device (rejected or broke a codec this start) is not offered it again */
	reset();
	settled_codec(BTT_LINK_CODEC_SBC, BTT_CODEC_SBC);
	btaac_neg.ban_n = 1; btaac_neg.ban_lo[0] = MAC_LO; btaac_neg.ban_hi[0] = MAC_HI; btaac_neg.ban_cdc[0] = BTAAC_BAN_APTX;
	g_codec = BTT_CODEC_APTX;
	ticks(8);
	CHECK(ga.n_start == 0 && ga.last == BTAPPLY_R_DECLINED, "aptX for marked headphones reconnected (last %d)", ga.last);
	return 0;
}

/* aptX HD When Available: an aptX link (HD at 576 kb/s or standard at 384) that the headphones offer HD for or not (the list they gave), `key` the setting the link was made under */
#define HC_APTX_LIST   (BTT_HC_SBC | BTT_HC_APTX)
#define HC_APTX_HD_LIST (BTT_HC_SBC | BTT_HC_APTX | BTT_HC_APTX_HD)
static void settled_aptx(int hd, unsigned list, int key)
{
	reset();
	g_disc_follow = 1;
	g_list_on_up = list;
	g_hd_set = key;
	g_codec = BTT_CODEC_APTX;
	link_up(BTT_LINK_CODEC_APTX);
	g_li.bitrate_kbps = hd ? 576u : 384u;
	ticks(60);
}

static int scenario_aptx_hd(void)
{
	/* HD link, the setting turned off: a reconnect, and standard aptX comes back */
	settled_aptx(1, HC_APTX_HD_LIST, 1);
	g_hd_set = 0;
	tick();
	CHECK(ga.pending, "the change of aptX HD When Available was not noted");
	g_up_kbps = 384;
	run_apply(BTT_LINK_CODEC_APTX, 1, 2);
	CHECK(g_ncalls == 2 && g_order[0] == 1 && g_order[1] == 2 && ga.n_start == 1 && ga.n_ok == 1 && ga.last == BTAPPLY_R_OK, "HD off on an HD link: %d calls, ok %u last %d", g_ncalls, ga.n_ok, ga.last);
	/* standard link made with the setting off, the headphones offer HD, the setting turned on: a reconnect, and HD comes back */
	settled_aptx(0, HC_APTX_HD_LIST, 0);
	g_hd_set = 1;
	g_up_kbps = 576;
	tick();
	run_apply(BTT_LINK_CODEC_APTX, 1, 2);
	CHECK(g_ncalls == 2 && ga.n_start == 1 && ga.n_ok == 1 && ga.last == BTAPPLY_R_OK, "HD on, headphones offer it: %d calls, ok %u last %d", g_ncalls, ga.n_ok, ga.last);
	/* HD came back, then standard again: an apply again (the new link is compared as it is) */
	g_hd_set = 0;
	g_up_kbps = 384;
	g_ncalls = g_disc_calls = g_conn_calls = 0;
	run_apply(BTT_LINK_CODEC_APTX, 1, 2);
	CHECK(g_ncalls == 2 && ga.n_start == 2 && ga.n_ok == 2, "HD off again after the apply: start %u ok %u", ga.n_start, ga.n_ok);
	/* the headphones do not list aptX HD: the setting has no effect on this link, either way */
	settled_aptx(0, HC_APTX_LIST, 0);
	g_hd_set = 1;
	ticks(8);
	CHECK(g_ncalls == 0 && ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "HD on for headphones that do not list it reconnected (last %d)", ga.last);
	/* HD offered when the link was made and the headphones came up on standard aptX (they did not take it): turning the setting off and on again reconnects nothing */
	settled_aptx(0, 0, 1);
	g_hd_set = 0;
	ticks(8);
	CHECK(g_ncalls == 0 && ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "off on a standard link reconnected (last %d)", ga.last);
	g_hd_set = 1;
	ticks(8);
	CHECK(g_ncalls == 0 && ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "HD on again for headphones that did not take it reconnected (last %d)", ga.last);
	settled_aptx(0, HC_APTX_HD_LIST, 1);
	g_hd_set = 0;
	ticks(8);
	g_hd_set = 1;
	ticks(8);
	CHECK(g_ncalls == 0 && ga.n_start == 0, "HD offered at the connection and refused: a reconnect for it (starts %u)", ga.n_start);
	/* a scroll through off and on within the debounce is none; a setting put back is none */
	settled_aptx(1, HC_APTX_HD_LIST, 1);
	g_hd_set = 0; tick(); g_hd_set = 1; tick(); g_hd_set = 0; tick(); g_hd_set = 1;
	ticks(10);
	CHECK(g_ncalls == 0 && ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "a scroll that ends where it began reconnected (last %d)", ga.last);
	/* the HD encoder not usable (its self-test failed, or HD is off for this start): the effective choice is standard aptX whatever the setting says */
	settled_aptx(0, HC_APTX_HD_LIST, 0);
	g_hd_ok = 0;
	g_hd_set = 1;
	ticks(8);
	CHECK(g_ncalls == 0 && ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "HD on with no usable HD encoder reconnected (last %d)", ga.last);
	g_hd_ok = 1;
	btaac_neg.hd_off = 1;
	g_hd_set = 0; tick(); g_hd_set = 1;
	ticks(8);
	CHECK(g_ncalls == 0 && ga.n_start == 0, "HD turned off for this start: a reconnect for the setting (starts %u)", ga.n_start);
	/* on a link that is not aptX the setting changes nothing: SBC, AAC and LDAC links, with aptX or another codec chosen */
	g_ld_ok = 1;
	for (int c = 0; c < 4; c++) {
		static const int link[4] = { BTT_LINK_CODEC_SBC, BTT_LINK_CODEC_AAC, BTT_LINK_CODEC_LDAC, BTT_LINK_CODEC_SBC };
		static const int set[4] = { BTT_CODEC_SBC, BTT_CODEC_AAC, BTT_CODEC_LDAC, BTT_CODEC_APTX };

		reset();
		g_ld_ok = 1;
		g_list_on_up = HC_APTX_HD_LIST;
		g_codec = set[c];
		link_down(); tick();   /* the headphones connect with the codec ready: not an early link */
		link_up(link[c]);
		ticks(60);
		g_hd_set = 0;
		ticks(8);
		CHECK(g_ncalls == 0 && ga.n_start == 0, "the setting changed on a link of codec %d with codec %d chosen: a reconnect (starts %u)", link[c], set[c], ga.n_start);
	}
	g_ld_ok = 0;
	/* aptX HD When Available is not what chooses the codec: aptX chosen on an SBC link still reconnects, as before */
	reset();
	g_disc_follow = 1;
	settled_codec(BTT_LINK_CODEC_SBC, BTT_CODEC_SBC);
	g_codec = BTT_CODEC_APTX;
	g_up_kbps = 576;
	run_apply(BTT_LINK_CODEC_APTX, 1, 2);
	CHECK(g_ncalls == 2 && ga.n_ok == 1, "SBC -> aptX with the setting on: %d calls", g_ncalls);
	return 0;
}

static int scenario_hold_before_disconnect(void)
{
	reset();
	g_disc_follow = 0;   /* the mode never comes in */
	settled(BTT_LINK_CODEC_SBC);
	g_codec = BTT_CODEC_AAC;
	ticks(7);
	CHECK(btt_btapply_hold() && ga.phase == BTAPPLY_PH_HOLD && g_ncalls == 0, "no call before the mode is in (phase %d, calls %d)", ga.phase, g_ncalls);
	ticks(8);
	CHECK(g_ncalls == 0 && ga.n_start == 1 && ga.phase == BTAPPLY_PH_IDLE && ga.last == BTAPPLY_R_HOLD, "the hold failed: calls %d phase %d last %d", g_ncalls, ga.phase, ga.last);
	CHECK(!btt_btapply_hold(), "the mode is still asked for");
	CHECK(log_has("kept from muting"), "no log line");
	/* the mode comes in: the call follows, with the mode in at the moment of the call */
	reset();
	g_disc_follow = 1;
	settled(BTT_LINK_CODEC_SBC);
	g_codec = BTT_CODEC_AAC;
	for (int i = 0; i < 30 && !g_disc_calls; i++) {
		tick();
		if (g_disc_calls)
			CHECK(g_disc_in == BTT_DISC_SPEAKER && btt_btapply_hold(), "the disconnect was made without the mode");
	}
	CHECK(g_disc_calls == 1, "no disconnect");
	return 0;
}

static int scenario_scroll(void)
{
	reset();
	g_disc_follow = 1;
	settled(BTT_LINK_CODEC_AAC);
	/* a scroll through the bit rates: a change every half second, ending on 320 */
	int v[] = { 128, 192, 256, 320 };
	for (int i = 0; i < 4; i++) { g_kbps = v[i]; tick(); }
	ticks(2);
	CHECK(g_ncalls == 0, "reconnected during the scroll");
	run_apply(BTT_LINK_CODEC_AAC, 1, 2);
	CHECK(g_disc_calls == 1 && g_conn_calls == 1, "one reconnect expected: %d %d", g_disc_calls, g_conn_calls);
	/* a scroll that ends where it began changes nothing */
	reset();
	settled(BTT_LINK_CODEC_AAC);
	g_kbps = 192; tick(); g_kbps = 128; tick(); g_kbps = 256; tick();
	ticks(10);
	CHECK(g_ncalls == 0 && ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "a scroll back reconnected (last %d)", ga.last);
	return 0;
}

static int scenario_bitrate_cases(void)
{
	/* AAC link at 256 kb/s: 320 is an apply (264.6 is what the headset takes), 192 too; 256 -> 264.6 is a real change */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_AAC); g_kbps = 320; ticks(8);
	CHECK(ga.n_start == 1, "256 -> 320 on a headset taking 264.6 is a change");
	/* the headset's maximum is 256: 320 changes nothing */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_AAC); g_x.max_bps = 256000; g_kbps = 320; ticks(8);
	CHECK(ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "a rate the headset already gets reconnected (last %d)", ga.last);
	/* bit rate on an SBC link */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_kbps = 320; ticks(8);
	CHECK(ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "a bit rate on SBC reconnected");
	return 0;
}

static int scenario_accel_cases(void)
{
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_AAC); g_accel = 0; ticks(8);
	CHECK(ga.n_start == 1, "turning the acceleration off while NEON runs");
	/* NEON not allowed: the encoder runs the C build either way */
	reset(); g_disc_follow = 1; g_avail &= ~BTT_AAC_AVAIL_NEON; settled(BTT_LINK_CODEC_AAC); g_li.accel = 0; g_accel = 0; ticks(8);
	CHECK(ga.n_start == 0, "no NEON, accel off: nothing differs");
	reset(); g_disc_follow = 1; g_avail &= ~BTT_AAC_AVAIL_NEON; settled(BTT_LINK_CODEC_AAC); g_li.accel = 0; g_accel = 0; ticks(2); g_accel = 1; ticks(8);
	CHECK(ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "accel on with NEON not allowed reconnected");
	/* the encoder is not running (paused for a while): it takes the setting at its next start */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_AAC); g_x.aac_running = 0; g_li.accel = 0; g_accel = 0; ticks(8);
	CHECK(ga.n_start == 0, "accel change with the encoder idle reconnected");
	/* acceleration on an SBC link */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_accel = 0; ticks(8);
	CHECK(ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "accel on SBC reconnected");
	return 0;
}

static int scenario_quality_cases(void)
{
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_quality = BTT_Q_STABLE; ticks(8);
	CHECK(ga.n_start == 1, "SBC quality change on an SBC link");
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_AAC); g_quality = BTT_Q_STABLE; ticks(8);
	CHECK(ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "SBC quality on an AAC link reconnected");
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_quality = BTT_Q_HIGH; ticks(2); g_quality = BTT_Q_STANDARD; ticks(8);
	CHECK(ga.n_start == 0, "quality put back reconnected");
	return 0;
}

static int scenario_codec_cases(void)
{
	/* AAC -> SBC */
	reset(); g_disc_follow = 1; g_codec = BTT_CODEC_AAC; settled(BTT_LINK_CODEC_AAC); g_codec = BTT_CODEC_SBC; ticks(8);
	CHECK(ga.n_start == 1, "AAC to SBC");
	/* connected with AAC wanted and up on SBC: they do not offer it. A bit rate or acceleration change does nothing, a quality change reconnects */
	reset(); g_disc_follow = 1; g_codec = BTT_CODEC_AAC; link_down(); tick(); link_up(BTT_LINK_CODEC_SBC); ticks(60); g_kbps = 320; ticks(8);
	CHECK(ga.n_start == 0 && ga.last == BTAPPLY_R_DECLINED, "bit rate for headphones that came up on SBC with AAC wanted reconnected (last %d)", ga.last);
	g_quality = BTT_Q_STABLE; ticks(8);
	CHECK(ga.n_start == 1, "a quality change on that SBC link did not reconnect");
	/* AAC asked of headphones that were refused */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); btaac_neg.ban_lo[0] = MAC_LO; btaac_neg.ban_hi[0] = MAC_HI; btaac_neg.ban_cdc[0] = BTAAC_BAN_AAC; btaac_neg.ban_n = 1; g_codec = BTT_CODEC_AAC; ticks(8);
	CHECK(ga.n_start == 0 && ga.last == BTAPPLY_R_DECLINED, "AAC for marked headphones reconnected (last %d)", ga.last);
	/* AAC asked, the module is not usable */
	reset(); g_disc_follow = 1; g_avail = 0; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC; ticks(8);
	CHECK(ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP, "AAC without the module reconnected (last %d)", ga.last);
	/* the self-test has not passed */
	reset(); g_disc_follow = 1; g_avail = BTT_AAC_AVAIL_MODULE; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC; ticks(8);
	CHECK(ga.n_start == 0, "AAC before the self-test reconnected");
	return 0;
}

static int scenario_gates(void)
{
	/* master switch off, a change */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_on = 0; g_codec = BTT_CODEC_AAC; ticks(8);
	CHECK(g_ncalls == 0 && ga.n_start == 0, "reconnected with Bluetooth Turbo off");
	/* no link */
	reset(); g_disc_follow = 1; link_down(); ticks(4); g_codec = BTT_CODEC_AAC; ticks(8);
	CHECK(g_ncalls == 0 && !ga.pending, "a change with no headphones is waiting");
	/* the headphones leave before the debounce is over */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC; tick(); link_down(); ticks(8);
	CHECK(g_ncalls == 0 && ga.last == BTAPPLY_R_LINK_GONE, "headphones left: last %d", ga.last);
	/* a microphone is open */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_x.voice = 1; g_codec = BTT_CODEC_AAC; ticks(8);
	CHECK(g_ncalls == 0 && ga.last == BTAPPLY_R_VOICE && !btt_btapply_hold(), "reconnected during a call (last %d)", ga.last);
	/* a microphone opens during the hold */
	reset(); g_disc_follow = 0; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC; ticks(5);
	CHECK(ga.phase == BTAPPLY_PH_HOLD, "not holding (phase %d)", ga.phase);
	g_x.voice = 1; tick();
	CHECK(g_ncalls == 0 && ga.phase == BTAPPLY_PH_IDLE && !btt_btapply_hold() && ga.last == BTAPPLY_R_VOICE, "the mic during the hold: phase %d", ga.phase);
	/* Reconnect After Standby at work */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_standby = 1; g_codec = BTT_CODEC_AAC; ticks(8);
	CHECK(g_ncalls == 0 && ga.last == BTAPPLY_R_STANDBY, "reconnected during a standby reconnect");
	/* no address */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_x.mac_ok = 0; g_codec = BTT_CODEC_AAC; ticks(8);
	CHECK(g_ncalls == 0 && ga.last == BTAPPLY_R_NO_ADDR, "no address");
	/* calls missing */
	reset(); g_disc_follow = 1; g_calls_found = 0; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC; ticks(8);
	CHECK(g_ncalls == 0 && ga.last == BTAPPLY_R_NO_CALLS && g_lookups > 0 && log_has("not found"), "calls missing (last %d)", ga.last);
	/* the link has only just come up: wait for it to settle, then do it */
	reset(); g_disc_follow = 1; link_up(BTT_LINK_CODEC_SBC); ticks(2); g_codec = BTT_CODEC_AAC; ticks(4);
	CHECK(g_ncalls == 0 && ga.pending, "did not wait for the link to settle");
	ticks(10);
	CHECK(ga.n_start == 1, "never started after the link settled");
	/* a link that never settles: the change is dropped */
	reset(); g_disc_follow = 1; link_up(BTT_LINK_CODEC_SBC); tick(); g_codec = BTT_CODEC_AAC; tick();
	for (int i = 0; i < 60; i++) { ga.link_since = g_now / 1000u; tick(); }
	CHECK(ga.last == BTAPPLY_R_UNSETTLED && !ga.pending && g_ncalls == 0, "unsettled: last %d", ga.last);
	/* a setting that is not one of the four */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); ticks(8);
	CHECK(g_ncalls == 0 && !ga.pending && ga.n_skip == 0, "nothing changed but something happened");
	return 0;
}

static int scenario_failures(void)
{
	/* the disconnect is refused */
	reset(); g_disc_follow = 1; g_disc_ret = (int)0x802F0205; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC; ticks(80);
	CHECK(g_disc_calls == 1 && g_conn_calls == 0 && ga.n_fail == 1 && ga.last == BTAPPLY_R_DOWN_ERR, "disconnect refused: %d %d last %d", g_disc_calls, g_conn_calls, ga.last);
	CHECK(!btt_btapply_hold() && ga.phase == BTAPPLY_PH_IDLE, "mode not released after a refused disconnect");
	CHECK(log_has("FAILED"), "failure not logged");
	/* the link does not go down */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC;
	ticks(8);
	CHECK(g_disc_calls == 1, "no disconnect");
	g_state = 5;
	ticks(40);
	CHECK(g_conn_calls == 0 && ga.n_fail == 1 && ga.last == BTAPPLY_R_DOWN_TIMEOUT, "link not down: last %d", ga.last);
	CHECK(btt_btapply_hold() || ga.phase == BTAPPLY_PH_COOL || ga.phase == BTAPPLY_PH_IDLE, "phase");
	ticks(20);
	CHECK(!btt_btapply_hold(), "mode kept for ever");
	/* the connect is refused for good (an answer that is not "busy"): one attempt only */
	reset(); g_disc_follow = 1; g_conn_ret = (int)0x802F020A; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC;
	for (int i = 0, d = 0; i < 100; i++) { tick(); if (g_disc_calls && !d) { link_down(); d = 1; } }
	CHECK(g_disc_calls == 1 && g_conn_calls == 1 && ga.n_fail == 1 && ga.last == BTAPPLY_R_UP_ERR, "connect refused: conn calls %d last %d", g_conn_calls, ga.last);
	/* the link does not come back */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC;
	for (int i = 0, d = 0; i < 160; i++) { tick(); if (g_disc_calls && !d) { link_down(); d = 1; } }
	CHECK(g_conn_calls == 1 && ga.n_fail == 1 && ga.last == BTAPPLY_R_UP_TIMEOUT && !btt_btapply_hold(), "no link back: last %d", ga.last);
	CHECK(log_has("did not come back"), "not logged");
	/* the headphones came back by themselves before the connect call: no connect call is made, and it is counted as done */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC;
	for (int i = 0, d = -1; i < 120; i++) {
		tick();
		if (g_disc_calls && d < 0) { link_down(); d = i; }       /* "no link" for one pass ... */
		if (d >= 0 && i == d + 1) g_state = 3;                  /* ... then connecting again, within the second before the connect call */
		if (d >= 0 && i == d + 8) link_up(BTT_LINK_CODEC_AAC);
	}
	CHECK(g_disc_calls == 1 && g_conn_calls == 0 && ga.n_ok == 1 && log_has("by themselves"), "came back by themselves: %d connect calls, %u ok", g_conn_calls, ga.n_ok);
	return 0;
}

static int scenario_remember_sbc(void)
{
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC;
	run_apply(BTT_LINK_CODEC_SBC, 1, 2);   /* they come back on SBC */
	CHECK(ga.n_ok == 1 && ga.last == BTAPPLY_R_OK_SBC && ga.no_aac_n == 1, "not remembered: last %d n %d", ga.last, ga.no_aac_n);
	CHECK(log_has("do not take that codec"), "not logged");
	/* the bit rate changes, the codec is still AAC for those headphones: nothing */
	int calls = g_ncalls;
	g_kbps = 192; ticks(10);
	CHECK(g_ncalls == calls && ga.last == BTAPPLY_R_DECLINED, "tried AAC again (last %d)", ga.last);
	return 0;
}

/* A failure case: the stream code's hooks were refused by its own check (a local failure), the headphones came back on SBC, and the apply wrote "they do not take AAC": every
 * later change was then refused for them. SBC is the headphones' doing only when the codec was on offer. */
static int scenario_local_failure(void)
{
	/* the codec is not on offer (the hooks are not in): AAC wanted, a bit rate change, the link SBC: no effect, and not "refused" */
	reset(); g_disc_follow = 1; btaac_neg.enable = 0; g_codec = BTT_CODEC_AAC; link_up(BTT_LINK_CODEC_SBC); ticks(60);
	g_kbps = 128; ticks(8);
	CHECK(ga.n_start == 0 && ga.last == BTAPPLY_R_NOOP && !ga.aac_at_link, "AAC not on offer: no reconnect, not blamed on the headphones (last %d, at link %d)", ga.last, ga.aac_at_link);
	/* a forced apply (Refresh Codecs, or the development port's) with the codec not on offer: back on SBC, and nothing remembered about the headphones */
	reset(); g_disc_follow = 1; btaac_neg.enable = 0; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC; ga_manual = 1;
	run_apply(BTT_LINK_CODEC_SBC, 1, 2);
	CHECK(ga.n_ok == 1 && ga.last == BTAPPLY_R_OK_LOCAL && ga.no_aac_n == 0 && log_has("not on offer") && !log_has("do not take"), "nothing remembered: last %d, marks %d", ga.last, ga.no_aac_n);
	/* the hooks go in later (an off and on): AAC is tried again, the headphones were never refused */
	btaac_neg.enable = 1;
	g_kbps = 192; ticks(8);
	CHECK(ga.n_start == 2 && ga.last != BTAPPLY_R_DECLINED, "the next change reconnects for AAC: starts %u, last %d", ga.n_start, ga.last);
	run_apply(BTT_LINK_CODEC_AAC, 1, 2);
	CHECK(ga.n_ok == 2 && ga.last == BTAPPLY_R_OK && ga.no_aac_n == 0, "and AAC comes: last %d", ga.last);
	/* the offer going out in the middle of an apply (a hook that stops being in) is local too: no mark, however it ends */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC;
	for (int i = 0, d = 0, e = 0; i < 400; i++) {
		tick();
		if (g_disc_calls && !d) { link_down(); d = 1; }
		if (g_conn_calls && !e) { e = i + 4; }
		if (e && i == e) { link_up(BTT_LINK_CODEC_SBC); btaac_neg.enable = 0; }
		if (ga.phase == BTAPPLY_PH_IDLE && ga.n_ok && !btt_btapply_hold())
			break;
	}
	CHECK(ga.n_ok == 1 && ga.last == BTAPPLY_R_OK_LOCAL && ga.no_aac_n == 0, "offer lost during the apply: last %d, marks %d", ga.last, ga.no_aac_n);
	return 0;
}

/* The module's own engine or add-on could not start a session for these headphones (a local mark of the stream code, btaac_local_for): the codec is not offered to them this start, but it
 * is not the headphones' refusal: the apply reads it as "not on offer" (no reconnect that cannot give the codec, no "they do not take it" written down, other codecs still tried). */
static int scenario_local_mark(void)
{
	/* the headphones' mark for AAC counts as declined; the module's own mark for AAC does not (the result is the no-effect one, not "declined"), and neither reconnects */
	reset(); g_disc_follow = 1; g_apt_ok = 1; g_ld_ok = 1; settled(BTT_LINK_CODEC_SBC);
	btaac_local_for_test(MAC_LO, MAC_HI, BTAAC_BAN_AAC);
	CHECK(btaac_ban_bits(MAC_LO, MAC_HI) == 0 && btaac_local_bits(MAC_LO, MAC_HI) == BTAAC_BAN_AAC, "set up: a local mark for AAC only");
	g_codec = BTT_CODEC_AAC; ticks(8);
	CHECK(ga.n_start == 0 && g_ncalls == 0 && ga.last == BTAPPLY_R_NOOP && ga.no_aac_n == 0, "AAC locally off: no reconnect, no refusal recorded (last %d, marks %d)", ga.last, ga.no_aac_n);
	/* another codec of the same headphones still reconnects */
	g_codec = BTT_CODEC_APTX; ticks(8);
	CHECK(ga.n_start == 1, "aptX for headphones with a local mark for AAC: a reconnect (starts %u, last %d)", ga.n_start, ga.last);
	/* a link made while AAC was locally off and which is SBC is not read as "these headphones did not take AAC" later, when the mark is gone */
	reset(); g_disc_follow = 1; g_codec = BTT_CODEC_AAC; btaac_local_for_test(MAC_LO, MAC_HI, BTAAC_BAN_AAC); link_down(); tick(); link_up(BTT_LINK_CODEC_SBC); ticks(60);
	CHECK(!ga.aac_at_link, "a link made with AAC locally off: AAC was not on offer for it");
	memset((void *)&btaac_neg.ban_cdc[0], 0, sizeof(btaac_neg.ban_cdc)); btaac_neg.ban_n = 0;
	g_kbps = 192; ticks(8);
	CHECK(ga.n_start == 1 && ga.last != BTAPPLY_R_DECLINED, "the mark gone (a new start): AAC is tried again, not refused (starts %u, last %d)", ga.n_start, ga.last);
	/* the mark appears in the middle of an apply (the add-on fails on the new connection): back on SBC, nothing remembered about the headphones */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC;
	for (int i = 0, d = 0, e = 0; i < 400; i++) {
		tick();
		if (g_disc_calls && !d) { link_down(); d = 1; }
		if (g_conn_calls && !e) { e = i + 4; }
		if (e && i == e) { link_up(BTT_LINK_CODEC_SBC); btaac_local_for_test(MAC_LO, MAC_HI, BTAAC_BAN_AAC); }
		if (ga.phase == BTAPPLY_PH_IDLE && ga.n_ok && !btt_btapply_hold())
			break;
	}
	CHECK(ga.n_ok == 1 && ga.last == BTAPPLY_R_OK_LOCAL && ga.no_aac_n == 0, "the module's own failure during the apply: last %d, marks %d", ga.last, ga.no_aac_n);
	return 0;
}

/* A refusal is for one codec: AAC refused does not stop aptX or LDAC, and aptX refused does not stop LDAC. */
static int scenario_per_codec(void)
{
	/* marked by the stream code for AAC (btaac_ban): an aptX change still reconnects, an AAC one does not */
	reset(); g_disc_follow = 1; g_apt_ok = 1; g_ld_ok = 1; settled(BTT_LINK_CODEC_SBC);
	btaac_neg.ban_n = 1; btaac_neg.ban_lo[0] = MAC_LO; btaac_neg.ban_hi[0] = MAC_HI; btaac_neg.ban_cdc[0] = BTAAC_BAN_AAC;
	g_codec = BTT_CODEC_AAC; ticks(8);
	CHECK(ga.n_start == 0 && ga.last == BTAPPLY_R_DECLINED, "AAC for headphones refused AAC");
	g_codec = BTT_CODEC_APTX; ticks(8);
	CHECK(ga.n_start == 1, "aptX for headphones refused AAC: a reconnect (starts %u, last %d)", ga.n_start, ga.last);
	/* the same from a mark the apply itself made: AAC came back on SBC, then aptX is asked */
	reset(); g_disc_follow = 1; g_apt_ok = 1; g_ld_ok = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC;
	run_apply(BTT_LINK_CODEC_SBC, 1, 2);
	CHECK(ga.last == BTAPPLY_R_OK_SBC && ga.no_aac_n == 1 && ga.no_aac_cdc[0] == (1u << BTFALL_C_AAC), "AAC marked for these headphones: %d, bits %x", ga.no_aac_n, ga.no_aac_cdc[0]);
	g_codec = BTT_CODEC_APTX; ticks(8);
	CHECK(ga.n_start == 2, "aptX after an AAC refusal: a reconnect (starts %u, last %d)", ga.n_start, ga.last);
	run_apply(BTT_LINK_CODEC_APTX, 1, 2);
	CHECK(ga.last == BTAPPLY_R_OK && ga.no_aac_n == 1, "aptX comes: last %d, marks %d (one headset, one entry)", ga.last, ga.no_aac_n);
	CHECK(btapply_no_aac(&ga, MAC_LO, MAC_HI, 1u << BTFALL_C_AAC) && !btapply_no_aac(&ga, MAC_LO, MAC_HI, 1u << BTFALL_C_APTX) && !btapply_no_aac(&ga, MAC_LO, MAC_HI, 1u << BTFALL_C_LDAC),
	      "AAC is still remembered as refused, aptX and LDAC are not");
	/* aptX refused as well: one entry, two bits; LDAC is not affected */
	reset(); g_disc_follow = 1; g_apt_ok = 1; g_ld_ok = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC;
	run_apply(BTT_LINK_CODEC_SBC, 1, 2);
	g_codec = BTT_CODEC_APTX; ticks(8);
	run_apply(BTT_LINK_CODEC_SBC, 1, 2);
	CHECK(ga.no_aac_n == 1 && ga.no_aac_cdc[0] == ((1u << BTFALL_C_AAC) | (1u << BTFALL_C_APTX)), "two codecs marked for one headset: %d entries, bits %x", ga.no_aac_n, ga.no_aac_cdc[0]);
	g_codec = BTT_CODEC_LDAC; ticks(8);
	CHECK(ga.n_start == 3, "LDAC after AAC and aptX refusals: a reconnect (starts %u)", ga.n_start);
	/* another headset is not marked by this one's refusals */
	reset(); g_disc_follow = 1; g_apt_ok = 1; settled(BTT_LINK_CODEC_SBC);
	btaac_neg.ban_n = 1; btaac_neg.ban_lo[0] = MAC_LO + 1; btaac_neg.ban_hi[0] = MAC_HI; btaac_neg.ban_cdc[0] = BTAAC_BAN_AAC | BTAAC_BAN_APTX;
	g_codec = BTT_CODEC_AAC; ticks(8);
	CHECK(ga.n_start == 1, "a mark of another headset does not count");
	return 0;
}

/* The connect call answers "busy" (0x802F0204: the Bluetooth code's driver is still finishing the disconnect): it is asked again, after 0.5, 1, 2, 2 s, and each is logged. */
static int scenario_connect_busy(void)
{
	/* busy twice, then it goes: three calls, 0.5 s then 1 s apart, the headphones come back, done */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC; g_conn_busy = 2;
	run_apply(BTT_LINK_CODEC_AAC, 1, 2);
	CHECK(g_conn_calls == 3 && ga.n_ok == 1 && ga.n_fail == 0 && ga.last == BTAPPLY_R_OK, "busy twice: %d calls, ok %u, fail %u, last %d", g_conn_calls, ga.n_ok, ga.n_fail, ga.last);
	CHECK(g_conn_at[2] - g_conn_at[1] == 500000 && g_conn_at[3] - g_conn_at[2] == 1000000, "the waits are 0.5 s and 1 s: %llu %llu", (unsigned long long)(g_conn_at[2] - g_conn_at[1]),
	      (unsigned long long)(g_conn_at[3] - g_conn_at[2]));
	CHECK(log_has("returned 0x802f0204 (attempt 1 of 5): the Bluetooth code is busy, asking again in 500 ms") && log_has("(attempt 2 of 5): the Bluetooth code is busy, asking again in 1000 ms") &&
	      log_has("returned 0x00000000 (attempt 3)"), "every attempt is logged");
	CHECK(!btt_btapply_hold(), "the no-mute mode is released");
	/* busy for ever: five calls, 0.5, 1, 2, 2 s apart, then FAILED as before */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC; g_conn_busy = 100;
	for (int i = 0, d = 0; i < 200; i++) { tick(); if (g_disc_calls && !d) { link_down(); d = 1; } }
	CHECK(g_conn_calls == BTAPPLY_CONNECT_TRIES && ga.n_fail == 1 && ga.last == BTAPPLY_R_UP_ERR, "busy through every attempt: %d calls, last %d", g_conn_calls, ga.last);
	CHECK(g_conn_at[2] - g_conn_at[1] == 500000 && g_conn_at[3] - g_conn_at[2] == 1000000 && g_conn_at[4] - g_conn_at[3] == 2000000 && g_conn_at[5] - g_conn_at[4] == 2000000, "0.5, 1, 2, 2 s apart");
	CHECK(log_has("(attempt 5 of 5)") && log_has("the connect was refused") && !btt_btapply_hold(), "the last attempt and the failure are logged, the mode released");
	/* the headphones come back by themselves while the code is busy: no more calls */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC; g_conn_busy = 100;
	for (int i = 0, d = 0, e = 0; i < 200; i++) {
		tick();
		if (g_disc_calls && !d) { link_down(); d = 1; }
		if (g_conn_calls == 1 && !e) { g_state = 3; e = i; }
		if (e && i == e + 6) link_up(BTT_LINK_CODEC_AAC);
	}
	CHECK(g_conn_calls == 1 && ga.n_ok == 1 && ga.n_fail == 0 && log_has("by themselves"), "back by themselves: %d connect calls, ok %u fail %u", g_conn_calls, ga.n_ok, ga.n_fail);
	/* a link that exists (0x802F020E) or is being made (0x802F0203) is "not now" as well; an unregistered address (0x802F0202) is final */
	CHECK(btapply_busy((int)0x802F0204) && btapply_busy((int)0x802F0203) && btapply_busy((int)0x802F020E) && !btapply_busy((int)0x802F0202) && !btapply_busy(0) && !btapply_busy(-1),
	      "which answers are busy");
	return 0;
}

/* One reconnect as the console does it: the link goes down at once after the disconnect call and comes back 2 s after the connect call, as `back_codec`
 * (with `back_bps` when AAC), then the passes run on until the headphones' tail is over. `at_tick` runs `fn` once at that tick of the cycle. */
static void cycle(int back_codec, uint32_t back_bps, int at_tick, void (*fn)(void))
{
	int down = 0, up = 0, t_conn = -1;

	for (int i = 0; i < 120; i++) {
		tick();
		if (i == at_tick && fn)
			fn();
		if (g_disc_calls && !down) { link_down(); down = 1; }
		if (g_conn_calls && t_conn < 0) t_conn = i;
		if (t_conn >= 0 && !up && i >= t_conn + 4) {
			link_up(back_codec);
			if (back_codec == BTT_LINK_CODEC_AAC)
				g_x.bps = back_bps;
			up = 1;
		}
	}
}
static void change_to_192(void) { g_kbps = 192; }
static void change_to_320(void) { g_kbps = 320; }

static int scenario_change_during_apply(void)
{
	/* a change while the headphones are down (the first pass after the disconnect call): the new connection is negotiated with it, so nothing differs afterwards */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC;
	cycle(BTT_LINK_CODEC_AAC, 192000, 16, change_to_192);
	CHECK(g_disc_calls == 1 && g_conn_calls == 1 && ga.n_start == 1 && ga.n_ok == 1 && !ga.pending, "a change while down: %d starts, %d ok, pending %d", ga.n_start, ga.n_ok, ga.pending);
	/* a change while the apply holds the no-mute mode, the link still up and not yet down: noted; the new link was made with the older value, so it is applied again */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_AAC); g_kbps = 128; ticks(2);
	g_kbps = 192;   /* the first apply's goal: 192 (the link is at 256) */
	ticks(4);
	CHECK(ga.phase == BTAPPLY_PH_HOLD || ga.phase == BTAPPLY_PH_DOWN, "not in an apply (phase %d)", ga.phase);
	g_kbps = 320; ticks(1);
	CHECK(ga.pending, "the change during the apply was not noted");
	cycle(BTT_LINK_CODEC_AAC, 192000, 0, 0);
	CHECK(ga.n_start == 2 && g_disc_calls >= 2, "the older value on the new link was not applied again: %d starts", ga.n_start);
	/* a change after the connect call, while the headphones are still away, whose value the new link did not get: applied again */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_AAC); g_kbps = 192; ticks(5);
	cycle(BTT_LINK_CODEC_AAC, 192000, 4, change_to_320);   /* 320 lands after the connect call, before the link flag returns; the link comes back at 192 */
	CHECK(ga.n_start == 2, "a change made while the headphones were away was lost: %d starts", ga.n_start);
	/* the same, but the new link has the newest value: no second apply */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_AAC); g_kbps = 192; ticks(5);
	g_kbps = 320; ticks(1);
	CHECK(ga.pending, "the change during the apply was not noted");
	cycle(BTT_LINK_CODEC_AAC, 264600, 0, 0);
	CHECK(ga.n_start == 1 && g_disc_calls == 1 && !ga.pending && ga.last == BTAPPLY_R_NOOP, "a second apply for a value already in force: %d starts, last %d", ga.n_start, ga.last);
	return 0;
}

/* Refresh Codecs: the button is a request (btt_btapply_request_refresh, from the syscall), taken by the next pass. */
static int press(void) { return btt_btapply_request_refresh(); }

static int scenario_refresh(void)
{
	/* the list is not known: a forced apply, the same calls as any apply, nothing in the settings changed */
	reset(); g_disc_follow = 1;
	settled(BTT_LINK_CODEC_SBC);
	CHECK(g_li.codec_mask == 0, "the fake link already has a list");
	g_list_on_up = BTT_HC_SBC | BTT_HC_AAC;   /* the console reads the list when it opens the stream after the reconnect */
	CHECK(press() == BTT_OK, "the request was refused");
	CHECK(press() == BTT_ERR_BUSY, "a second request before the pass took the first was accepted");
	tick();
	CHECK(log_has("Refresh Codecs pressed (list not known)"), "the press was not logged");
	CHECK(ga.refresh && ga.phase == BTAPPLY_PH_HOLD && ga.n_start == 1, "no forced apply began (refresh %d phase %d)", ga.refresh, ga.phase);   /* no debounce: a setting did not change */
	CHECK(press() == BTT_OK, "a request after the pass took the first was refused");   /* the pass that takes it answers busy */
	tick();
	CHECK(ga.n_refresh == 1 && ga.n_skip == 1 && log_has("a reconnect is already pending or running"), "the second press during the one in hand was not refused (refresh %u skip %u)", ga.n_refresh, ga.n_skip);
	run_apply(BTT_LINK_CODEC_SBC, 1, 2);
	CHECK(g_ncalls == 2 && g_order[0] == 1 && g_order[1] == 2, "calls: %d, order %d %d", g_ncalls, g_order[0], g_order[1]);
	CHECK(g_call_lo[0] == MAC_LO && g_call_hi[0] == MAC_HI && g_call_lo[1] == MAC_LO && g_call_hi[1] == MAC_HI, "wrong address");
	CHECK(ga.n_start == 1, "start %u", ga.n_start);
	ticks(40);
	CHECK(ga.n_ok == 1 && ga.n_fail == 0 && !btt_btapply_hold() && g_disc_in == 0, "ok %u fail %u hold %d", ga.n_ok, ga.n_fail, btt_btapply_hold());
	CHECK(log_has("their codecs were read"), "the end was not logged");
	CHECK(!ga.refresh, "still marked a refresh");
	/* known now: another press does nothing, logged */
	g_ncalls = 0;
	CHECK(press() == BTT_OK, "request refused after the one that ran");
	ticks(8);
	CHECK(g_ncalls == 0 && ga.last != BTAPPLY_R_START && log_has("already known"), "reconnected for a known list");

	/* the headphones are back but gave no list */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC);
	press();
	run_apply(BTT_LINK_CODEC_SBC, 1, 2);
	ticks(40);
	CHECK(g_ncalls == 2 && ga.n_ok == 1 && log_has("did not give their codecs"), "no-list end (calls %d ok %u)", g_ncalls, ga.n_ok);

	/* a list that is known: no reconnect at all */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_li.codec_mask = BTT_HC_SBC;
	press(); ticks(10);
	CHECK(g_ncalls == 0 && ga.n_start == 0 && !ga.pending && ga.n_skip == 1 && log_has("already known"), "known list: calls %d", g_ncalls);

	/* the gates, each logged and nothing called: a call or microphone, no headphones, the master switch, standby, no address, no calls, an unsettled link */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_x.voice = 1; press(); ticks(10);
	CHECK(g_ncalls == 0 && ga.last == BTAPPLY_R_VOICE && !btt_btapply_hold(), "a microphone is open: last %d", ga.last);
	reset(); g_disc_follow = 1; link_down(); ticks(4); press(); ticks(10);
	CHECK(g_ncalls == 0 && ga.last == BTAPPLY_R_LINK_GONE && !ga.pending, "no headphones: last %d", ga.last);
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_on = 0; press(); ticks(10);
	CHECK(g_ncalls == 0 && ga.last == BTAPPLY_R_DISABLED, "off: last %d", ga.last);
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_standby = 1; press(); ticks(10);
	CHECK(g_ncalls == 0 && ga.last == BTAPPLY_R_STANDBY, "standby: last %d", ga.last);
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_x.mac_ok = 0; press(); ticks(10);
	CHECK(g_ncalls == 0 && ga.last == BTAPPLY_R_NO_ADDR, "no address: last %d", ga.last);
	reset(); g_disc_follow = 1; g_calls_found = 0; settled(BTT_LINK_CODEC_SBC); press(); ticks(10);
	CHECK(g_ncalls == 0 && ga.last == BTAPPLY_R_NO_CALLS, "no calls: last %d", ga.last);
	reset(); g_disc_follow = 1; link_up(BTT_LINK_CODEC_SBC); ticks(2); press(); ticks(4);
	CHECK(g_ncalls == 0 && ga.pending, "did not wait for the link to settle");
	ticks(20);
	CHECK(ga.n_start == 1, "never started after the link settled");
	/* a microphone opens during the hold: nothing was touched yet */
	reset(); g_disc_follow = 0; settled(BTT_LINK_CODEC_SBC); press(); ticks(5);
	CHECK(ga.phase == BTAPPLY_PH_HOLD, "not holding (phase %d)", ga.phase);
	g_x.voice = 1; tick();
	CHECK(g_ncalls == 0 && ga.phase == BTAPPLY_PH_IDLE && !ga.refresh && !btt_btapply_hold(), "the mic during the hold");

	/* a press while a setting's reconnect is pending or running does not disturb it */
	reset(); g_disc_follow = 1; settled(BTT_LINK_CODEC_SBC); g_codec = BTT_CODEC_AAC; tick();
	CHECK(ga.pending && !ga.force, "the setting change is not pending");
	press(); tick();
	CHECK(ga.pending && !ga.force && !ga.refresh && log_has("already pending or running"), "the press changed a pending setting apply");
	run_apply(BTT_LINK_CODEC_AAC, 1, 2);
	CHECK(g_ncalls == 2 && ga.n_start == 1 && ga.n_ok == 1, "the setting's apply did not run once (start %u ok %u)", ga.n_start, ga.n_ok);

	/* a failure ends it with the failure logged, not as a read list; a later press works */
	reset(); g_disc_follow = 1; g_disc_ret = (int)0x802F0205; settled(BTT_LINK_CODEC_SBC); press(); ticks(80);
	CHECK(g_disc_calls == 1 && g_conn_calls == 0 && ga.n_fail == 1 && ga.last == BTAPPLY_R_DOWN_ERR && !ga.refresh && !btt_btapply_hold(), "disconnect refused: last %d", ga.last);
	CHECK(log_has("FAILED") && !log_has("their codecs were read"), "a failure logged as a success");
	g_disc_ret = 0; g_nlog = 0;
	press(); ticks(10);
	CHECK(ga.n_start == 2, "a press after a failure did not start (starts %u)", ga.n_start);
	return 0;
}

/* ---- the upgrade of an early SBC connection ----------------------------------------------- */

/* Headphones that connected on their own at power-on, before the codec was ready (the stream code's hooks not in): up on SBC with `setting` chosen, and
 * ten seconds of passes in which nothing may happen. The machinery has run with the codec not ready, so the link is early. `list` is the codec list they
 * have stored (0 = none). */
static int g_early_hd = 1;   /* aptX HD When Available for the next early_link */
static void early_link(int setting, unsigned list)
{
	reset();
	g_hd_set = g_early_hd;
	g_ld_ok = 1;
	g_disc_follow = 1;
	g_codec = setting;
	btaac_neg.enable = 0;
	link_down();
	tick();
	g_list_on_up = list;
	link_up(BTT_LINK_CODEC_SBC);
	ticks(20);
}

static int upgraded(void) { return log_has("upgrading the connection from SBC to"); }

static int scenario_early_upgrade(void)
{
	/* the codec is ready after 45 s: AAC, aptX and LDAC are each upgraded once, with the same disconnect and connect as an apply, the no-mute mode held, the codec in the log */
	static const struct { int setting, link; unsigned list; const char *text; } k[3] = {
		{ BTT_CODEC_AAC,  BTT_LINK_CODEC_AAC,  BTT_HC_SBC | BTT_HC_AAC,  "from SBC to AAC now that it is ready" },
		{ BTT_CODEC_APTX, BTT_LINK_CODEC_APTX, BTT_HC_SBC | BTT_HC_APTX, "from SBC to aptX now that it is ready" },
		{ BTT_CODEC_LDAC, BTT_LINK_CODEC_LDAC, BTT_HC_SBC | BTT_HC_LDAC, "from SBC to LDAC now that it is ready" },
	};

	for (int i = 0; i < 3; i++) {
		early_link(k[i].setting, k[i].list);
		CHECK(g_ncalls == 0 && ga.n_start == 0 && !ga.pending && !upgraded(), "%s: something happened while the codec was not ready", k[i].text);
		btaac_neg.enable = 1;   /* the hooks go in */
		tick();
		CHECK(ga.n_start == 1 && ga.phase == BTAPPLY_PH_HOLD && btt_btapply_hold() && g_ncalls == 0, "%s: not begun in the pass the codec was ready (start %u, phase %d)", k[i].text, ga.n_start, ga.phase);
		CHECK(log_has(k[i].text) && !log_has("applying: the headphones"), "%s: the log line is missing or is the plain one", k[i].text);
		run_apply(k[i].link, 1, 2);
		CHECK(g_ncalls == 2 && g_order[0] == 1 && g_order[1] == 2 && g_call_lo[0] == MAC_LO && g_call_hi[1] == MAC_HI, "%s: calls %d order %d %d", k[i].text, g_ncalls, g_order[0], g_order[1]);
		ticks(40);
		CHECK(ga.n_start == 1 && ga.n_ok == 1 && ga.n_fail == 0 && ga.last == BTAPPLY_R_OK && !btt_btapply_hold() && g_disc_in == 0, "%s: start %u ok %u last %d", k[i].text, ga.n_start, ga.n_ok, ga.last);
	}

	/* the codec's self-test finishes later than the hooks go in: the same, from the pass it is usable */
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	btaac_neg.enable = 1;
	g_avail = BTT_AAC_AVAIL_MODULE;   /* no verdict yet */
	ticks(10);
	CHECK(ga.n_start == 0 && g_ncalls == 0, "upgraded before the self-test passed");
	g_avail = BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_TESTED;
	tick();
	CHECK(ga.n_start == 1 && upgraded(), "not upgraded when the codec became usable");

	/* the list: unknown is asked (the reconnect reads it); known and listing the codec is asked; known without it is not; aptX HD's entry counts when HD is offered */
	early_link(BTT_CODEC_AAC, 0);
	btaac_neg.enable = 1; ticks(4);
	CHECK(ga.n_start == 1 && upgraded(), "no list: not upgraded");
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_APTX | BTT_HC_LDAC);
	btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0 && !upgraded() && !ga.pending, "the list does not offer AAC: upgraded (start %u)", ga.n_start);
	early_link(BTT_CODEC_APTX, BTT_HC_SBC | BTT_HC_AAC);
	btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0, "the list does not offer aptX: upgraded");
	early_link(BTT_CODEC_APTX, BTT_HC_SBC | BTT_HC_APTX_HD);
	btaac_neg.enable = 1; ticks(4);
	CHECK(ga.n_start == 1 && upgraded(), "aptX HD listed alone with HD offered: not upgraded");
	g_early_hd = 0;
	early_link(BTT_CODEC_APTX, BTT_HC_SBC | BTT_HC_APTX_HD);
	g_early_hd = 1;
	btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0, "aptX HD listed alone with HD not offered: upgraded");
	early_link(BTT_CODEC_LDAC, BTT_HC_SBC | BTT_HC_AAC);
	btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0, "the list does not offer LDAC: upgraded");

	/* marked or refused for the codec: not asked; a mark for another codec does not stop it */
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	btaac_neg.ban_n = 1; btaac_neg.ban_lo[0] = MAC_LO; btaac_neg.ban_hi[0] = MAC_HI; btaac_neg.ban_cdc[0] = BTAAC_BAN_AAC;
	btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0 && !upgraded(), "headphones refused AAC: upgraded");
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	btapply_mark(&ga, MAC_LO, MAC_HI, 1u << BTFALL_C_AAC);
	btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0, "headphones marked for AAC by an earlier apply: upgraded");
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	btapply_mark(&ga, MAC_LO, MAC_HI, 1u << BTFALL_C_LDAC);
	btaac_neg.enable = 1; ticks(4);
	CHECK(ga.n_start == 1, "a mark for LDAC stopped the upgrade to AAC");
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	btaac_local_for_test(MAC_LO, MAC_HI, BTAAC_BAN_AAC);   /* the module's own failure for them: not offered */
	btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0, "the module's own failure for these headphones: upgraded");

	/* a call or microphone: not now, and the upgrade is not used up: it comes when the microphone closes */
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	g_x.voice = 1;
	btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0 && !ga.pending && !upgraded() && ga.n_skip == 0, "upgraded during a microphone (start %u skip %u)", ga.n_start, ga.n_skip);
	g_x.voice = 0;
	tick();
	CHECK(ga.n_start == 1 && upgraded(), "not upgraded after the microphone closed");
	/* the same for Reconnect After Standby, no address and no calls (nothing logged: nothing was asked) */
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	g_standby = 1; btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0 && ga.n_skip == 0, "upgraded during Reconnect After Standby");
	g_standby = 0; tick();
	CHECK(ga.n_start == 1, "not upgraded after Reconnect After Standby");
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	g_x.mac_ok = 0; btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0 && ga.n_skip == 0, "upgraded with no address");
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	ga_calls = 0; btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0 && ga.n_skip == 0, "upgraded with the Bluetooth code's calls not found");

	/* Bluetooth Turbo off: nothing; on again: upgraded */
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	g_on = 0; btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0 && !upgraded() && !ga.pending && ga.n_skip == 0, "upgraded or asked with Bluetooth Turbo off (skips %u)", ga.n_skip);
	g_on = 1; ticks(4);
	CHECK(ga.n_start == 1, "not upgraded when Bluetooth Turbo came on");

	/* the codec chosen is SBC, or the link is not SBC, or the module is not usable: nothing */
	early_link(BTT_CODEC_SBC, BTT_HC_SBC | BTT_HC_AAC);
	btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0, "SBC chosen: upgraded");
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	link_up(BTT_LINK_CODEC_AAC); btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0, "an AAC link was upgraded");
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	g_avail = BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_TESTED | BTT_AAC_AVAIL_HELD;
	btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 0 && g_ncalls == 0, "upgraded to a codec the crash record holds off");
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	ticks(20);   /* the hooks never go in */
	CHECK(ga.n_start == 0 && g_ncalls == 0, "upgraded without the hooks");

	/* the real first pass: the worker's apply pass runs for the first time at 45 s, in the pass the hooks went in, and finds headphones already connected on SBC:
	 * they connected before the hooks, so this is an early connection too */
	reset();
	g_disc_follow = 1; g_codec = BTT_CODEC_AAC; g_list_on_up = BTT_HC_SBC | BTT_HC_AAC;
	link_up(BTT_LINK_CODEC_SBC); ticks(4);
	CHECK(ga.n_start == 0 && g_ncalls == 0 && !ga.pending, "upgraded or asked for a link that was not settled");
	ticks(6);
	CHECK(ga.n_start == 1 && upgraded(), "headphones found already connected on SBC in the first pass were not upgraded");

	/* a connection made with the codec ready is the headphones' doing: SBC there is not upgraded (the plain apply and the marks are the ones that deal with it) */
	reset();
	g_disc_follow = 1; g_codec = BTT_CODEC_AAC; g_list_on_up = BTT_HC_SBC | BTT_HC_AAC;
	link_down(); ticks(2);
	link_up(BTT_LINK_CODEC_SBC); ticks(60);
	CHECK(ga.n_start == 0 && g_ncalls == 0 && !upgraded(), "a connection made with the codec ready was upgraded");
	/* ... and so is one when the codec was chosen after it connected (the apply for the setting is the one that handles it) */
	early_link(BTT_CODEC_SBC, BTT_HC_SBC | BTT_HC_AAC);
	btaac_neg.enable = 1; ticks(2);
	g_codec = BTT_CODEC_AAC; tick();
	CHECK(ga.pending && !ga.force && ga.upgrade == 0, "the setting change was not a plain apply");
	ticks(2);
	CHECK(!upgraded(), "a codec chosen after the connection was logged as an upgrade");

	/* the stream: not while the headset's start exchange may be going on (a stream started less than 3 s ago); idle or started long ago is fine */
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	g_li.streaming = 1;
	btaac_neg.enable = 1; ticks(4);   /* seen to start 1.5 s ago */
	CHECK(ga.n_start == 0 && g_ncalls == 0, "upgraded in the first two seconds of the stream");
	ticks(3);
	CHECK(ga.n_start == 1, "not upgraded after the stream had run 3 s");
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	g_li.streaming = 1; ticks(4);   /* streaming for 2 s while the codec is not ready ... */
	btaac_neg.enable = 1; tick();
	CHECK(ga.n_start == 0, "upgraded a stream that started 2.5 s ago");
	ticks(2);
	CHECK(ga.n_start == 1, "not upgraded once the stream had run 3 s");
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	g_li.streaming = 1; ticks(12);   /* ... long ago */
	btaac_neg.enable = 1; tick();
	CHECK(ga.n_start == 1, "a stream that had run for 6 s was not upgraded at once");
	/* a link that is not settled yet (4 s): not before */
	reset(); g_ld_ok = 1; g_disc_follow = 1; g_codec = BTT_CODEC_AAC; btaac_neg.enable = 0; link_down(); tick();
	g_list_on_up = BTT_HC_SBC | BTT_HC_AAC; link_up(BTT_LINK_CODEC_SBC); ticks(2);
	btaac_neg.enable = 1; tick();
	CHECK(ga.n_start == 0 && g_ncalls == 0 && !ga.pending, "upgraded or asked for a link 1.5 s old");
	ticks(6);
	CHECK(ga.n_start == 1, "not upgraded once the link was settled");

	/* once for each headset in a start, whatever the answer: it came back on AAC; then the headphones connect again early (the module let go and back) */
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	btaac_neg.enable = 1;
	run_apply(BTT_LINK_CODEC_AAC, 1, 2);
	ticks(40);
	CHECK(ga.n_start == 1 && ga.n_ok == 1 && ga.up_n == 1, "the first upgrade: start %u ok %u", ga.n_start, ga.n_ok);
	btaac_neg.enable = 0; link_down(); tick();
	link_up(BTT_LINK_CODEC_SBC); ticks(20);
	btaac_neg.enable = 1; ticks(20);
	CHECK(ga.n_start == 1 && ga.up_n == 1, "the same headphones were upgraded twice in a start (starts %u)", ga.n_start);
	/* ... and another headset is */
	g_x.mac_lo = MAC_LO + 1; btaac_neg.enable = 0; link_down(); tick();
	link_up(BTT_LINK_CODEC_SBC); g_x.mac_lo = MAC_LO + 1; ticks(20);
	btaac_neg.enable = 1; ticks(4);
	CHECK(ga.n_start == 2 && ga.up_n == 2, "a second headset was not upgraded (starts %u)", ga.n_start);

	/* headphones that come back on SBC are marked as for any apply (not asked again), and the upgrade is still counted once */
	early_link(BTT_CODEC_AAC, BTT_HC_SBC | BTT_HC_AAC);
	btaac_neg.enable = 1;
	run_apply(BTT_LINK_CODEC_SBC, 1, 2);
	ticks(40);
	CHECK(ga.n_start == 1 && ga.last == BTAPPLY_R_OK_SBC && btapply_no_aac(&ga, MAC_LO, MAC_HI, 1u << BTFALL_C_AAC), "SBC after the upgrade: last %d", ga.last);
	g_ld_ok = 0;
	return 0;
}

static int run_all(void)
{
	static const struct { const char *name; int (*fn)(void); } s[] = {
		{ "codec SBC -> AAC: debounce, hold, disconnect, connect, tail", scenario_codec_to_aac },
		{ "LDAC: to and from SBC and the other codecs, nothing else matters on an LDAC link, no module, marked headphones", scenario_ldac },
		{ "aptX: to and from SBC and AAC, nothing else matters on an aptX link, no module, marked headphones", scenario_aptx },
		{ "aptX HD When Available: HD to standard and back, headphones that do not list HD or did not take it, put back, not usable, other codecs", scenario_aptx_hd },
		{ "the no-mute mode is in before the disconnect, and without it nothing is called", scenario_hold_before_disconnect },
		{ "scrolling through values is one reconnect, putting it back none", scenario_scroll },
		{ "bit rate: a real change, the headset's maximum, an SBC link", scenario_bitrate_cases },
		{ "acceleration: on and off, no NEON, an idle encoder, SBC", scenario_accel_cases },
		{ "quality: SBC link, AAC link, put back", scenario_quality_cases },
		{ "codec: AAC to SBC, marked headphones, no module, no self-test", scenario_codec_cases },
		{ "gates: off, no link, a microphone, standby reconnect, no address, no calls, settling", scenario_gates },
		{ "failures are one attempt, logged, mode released", scenario_failures },
		{ "AAC asked of headphones that come back on SBC is remembered", scenario_remember_sbc },
		{ "a local failure (the codec not on offer) never marks the headphones", scenario_local_failure },
		{ "the module's own failure is a local mark: not a refusal, not offered", scenario_local_mark },
		{ "a refusal is for one codec: AAC refused does not stop aptX or LDAC", scenario_per_codec },
		{ "the connect asked again while the Bluetooth code is busy", scenario_connect_busy },
		{ "a change during an apply", scenario_change_during_apply },
		{ "Refresh Codecs: a forced reconnect when the list is not known, one at a time, the same gates", scenario_refresh },
		{ "an early SBC connection is upgraded once the codec is ready: the list, marks, microphone, once, Turbo off, the stream", scenario_early_upgrade },
	};

	for (unsigned i = 0; i < sizeof(s) / sizeof(s[0]); i++) {
		printf("  %s\n", s[i].name);
		if (s[i].fn())
			return 1;
	}
	return 0;
}

#ifndef BTT_APPLY_TEST_NO_MAIN
int main(void)
{
	if (run_all())
		return 1;
	printf("test_btapply: ok\n");
	return 0;
}
#endif
