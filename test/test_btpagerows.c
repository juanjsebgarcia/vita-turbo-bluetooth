/*
 * test_btpagerows.c - host test of the link rows on the page of one Bluetooth device (settings_app/bluetoothturbo_devrows.h), compiled as it is,
 * with the real translation table:
 *   - the splice of the four rows into the page: the system's page for one device, a page another plugin has already grown (nested lists, 50 KB), a
 *     second pass, a page that is not the device page, no closing tag, a buffer too small or overlapping;
 *   - the ids: unique on the page, apart from Sony's and from the product's own page, and the title ids the text hook must answer;
 *   - which device: the 12-hex-digit id of a device row against the kernel module's address words, the cases
 *     where the link is somebody else's, not there, or the kernel module is the older one;
 *   - the values in every language: Codec, Sample Rate, Bluetooth Latency, Headset Battery, and the one-line titles of the fallback;
 *   - the Signal row: the five words (Measuring, Weak, Fair, Good, Excellent) in all 20 languages, all different, and no value from a kernel module that has no signal fields;
 *   - the Refresh Codecs button: the page with it (right after Headset Codecs, a plain button row) and without it, when the list is known or unknown,
 *     its press (handed to the kernel call, consumed whether or not it was accepted, every other id left for the page's own handler), and its
 *     texts in all 20 languages;
 *   - the wrapper of the page plugin's label-value function on a fake structure: the original always runs first and keeps its result, only the
 *     rows' ids get a value, the text lands in the string object at +0x18 and nothing around it is touched.
 *
 *   make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../settings_app/bluetoothturbo_devrows.h"
#include "../settings_app/bluetoothturbo_i18n_table.h"

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); exit(1); } } while (0)

static int g_lang = BTT_LANG_ENGLISH_US;
static const char *str_now(int id) { return g_tbl[id][g_lang] ? g_tbl[id][g_lang] : g_tbl[id][BTT_LANG_ENGLISH_US]; }

/* The system's page for one device. */
static const char SONY_PAGE[] =
	"<system_settings version=\"1.0\" plugin=\"bluetooth_settings_plugin\">\n"
	"  <setting_list id=\"id_bluetooth_settings_menu\" title=\"\" style=\"edit\" rotate=\"full\">\n"
	"    <label id=\"id_bt_device_name\" title=\"msg_bt_device_name\" />\n"
	"    <label id=\"id_bt_device_type\" title=\"msg_bt_type\" />\n"
	"    <label id=\"id_bt_profile\"     title=\"msg_bt_profile\" />\n"
	"  </setting_list>\n"
	"</system_settings>\n";

/* Sony's device list page: same plugin, no root menu id. */
static const char LIST_PAGE[] =
	"<system_settings version=\"1.0\" plugin=\"bluetooth_settings_plugin\">\n"
	"  <setting_list id=\"id_bluetooth_settings\" title=\"msg_bt\">\n"
	"  </setting_list>\n"
	"</system_settings>\n";

static int count_of(const char *h, int hn, const char *n)
{
	int c = 0, nn = (int)strlen(n);

	for (int i = 0; i + nn <= hn; i++)
		if (!memcmp(h + i, n, (size_t)nn))
			c++;
	return c;
}

/* the ids found in a page as id="..." */
static int ids_of(const char *p, int n, char ids[][64], int max)
{
	int c = 0;

	for (int i = 0; i + 4 < n; i++) {
		if ((i == 0 || p[i - 1] == ' ') && !memcmp(p + i, "id=\"", 4) && c < max) {
			int j = i + 4, k = 0;

			while (j < n && p[j] != '"' && k < 63)
				ids[c][k++] = p[j++];
			ids[c][k] = 0;
			c++;
		}
	}
	return c;
}

/* ---- the page ------------------------------------------------------------------------------------------ */

static void t_splice_sony(void)
{
	static char out[4096];
	int size = (int)sizeof(SONY_PAGE) - 1, rows = (int)sizeof(btt_devrows_xml) - 1;
	int r = btt_devrows_splice(SONY_PAGE, size, out, (int)sizeof(out));

	CHECK(r == size + rows, "grown by the rows: %d", r);
	CHECK(count_of(out, r, "<label id=\"btt_lk_codec\"") == 1 && count_of(out, r, "<label id=\"btt_lk_rate\"") == 1 &&
	      count_of(out, r, "<label id=\"btt_lk_latency\"") == 1 && count_of(out, r, "<label id=\"btt_lk_battery\"") == 1 && count_of(out, r, "<label id=\"btt_lk_signal\"") == 1,
	      "each row once, all labels like Sony's");
	CHECK(count_of(out, r, "</setting_list>") == count_of(SONY_PAGE, size, "</setting_list>"), "no tag lost or added");
	{
		const char *c = strstr(SONY_PAGE, "  </setting_list>");
		int head = (int)(c - SONY_PAGE) + 2;                   /* the tag itself, after its indent */

		CHECK(!memcmp(out, SONY_PAGE, (size_t)head), "the page before the splice point is as received");
		CHECK(!memcmp(out + head, btt_devrows_xml, (size_t)rows), "the rows");
		CHECK(!memcmp(out + head + rows, SONY_PAGE + head, (size_t)(size - head)), "the rest is as received");
	}
	CHECK(btt_devrows_splice(out, r, (char *)malloc(8192), 8192) == BTT_DR_DONE, "a second pass does not add them again");
}

static void t_splice_grown(void)
{
	/* another plugin put a nested list and rows before the root's end; the splice goes before the LAST closing tag */
	static const char GROWN_HEAD[] =
		"<system_settings version=\"1.0\" plugin=\"bluetooth_settings_plugin\">\n"
		"  <setting_list id=\"id_bluetooth_settings_menu\" title=\"\" style=\"edit\" rotate=\"full\">\n"
		"    <label id=\"id_bt_device_name\" title=\"msg_bt_device_name\" />\n"
		"    <setting_list id=\"other_group\" title=\"x\">\n";
	static const char GROWN_TAIL[] =
		"    </setting_list>\n"
		"    <label id=\"other_row\" title=\"y\" />\n"
		"  </setting_list>\n"
		"</system_settings>\n";
	char *page = malloc(80 * 1024), *out = malloc(160 * 1024);
	int n = 0, r;
	const char *last;

	memcpy(page, GROWN_HEAD, sizeof(GROWN_HEAD) - 1);
	n = (int)sizeof(GROWN_HEAD) - 1;
	for (int i = 0; n < 50 * 1024; i++)                       /* about 50 KB, as YAMT grows the Devices page */
		n += snprintf(page + n, 100, "      <toggle_switch id=\"other_%d\" title=\"t%d\" key=\"k\"/>\n", i, i);
	memcpy(page + n, GROWN_TAIL, sizeof(GROWN_TAIL));
	n += (int)sizeof(GROWN_TAIL) - 1;

	r = btt_devrows_splice(page, n, out, 160 * 1024);
	CHECK(r == n + (int)sizeof(btt_devrows_xml) - 1, "grown page spliced: %d", r);
	last = strstr(out, "<label id=\"btt_lk_codec\"");
	CHECK(last && last > strstr(out, "<label id=\"other_row\""), "the rows come after the other plugin's last row");
	CHECK(!strcmp(last + sizeof(btt_devrows_xml) - 1, "</setting_list>\n</system_settings>\n"), "only the root's close and the end follow the rows");
	/* an exact-size buffer works, one byte less does not and writes nothing */
	{
		char *tight = malloc((size_t)r);

		CHECK(btt_devrows_splice(page, n, tight, r) == r, "exact fit");
		memset(tight, 0x55, (size_t)r);
		CHECK(btt_devrows_splice(page, n, tight, r - 1) == BTT_DR_NOROOM && tight[0] == 0x55 && tight[r - 1] == 0x55, "too small: nothing written");
		free(tight);
	}
	free(page);
	free(out);
}

static void t_splice_refusals(void)
{
	static char out[4096];
	static char same[4096];
	static const char NOCLOSE[] = "<system_settings plugin=\"bluetooth_settings_plugin\"><setting_list id=\"id_bluetooth_settings_menu\">";
	int size = (int)sizeof(SONY_PAGE) - 1;

	CHECK(btt_devrows_splice(LIST_PAGE, (int)sizeof(LIST_PAGE) - 1, out, (int)sizeof(out)) == BTT_DR_NOMATCH, "the list page: not the device page");
	CHECK(btt_devrows_splice("<system_settings plugin=\"network_settings_plugin\"><setting_list id=\"id_bluetooth_settings_menu\"></setting_list>", 100,
	                         out, (int)sizeof(out)) == BTT_DR_NOMATCH, "another plugin's page");
	CHECK(btt_devrows_splice("", 0, out, (int)sizeof(out)) == BTT_DR_NOMATCH, "empty");
	CHECK(btt_devrows_splice("<system_settings plugin=bluetooth_settings_plugin><setting_list id=id_bluetooth_settings_menu></setting_list>", 100,
	                         out, (int)sizeof(out)) == BTT_DR_NOMATCH, "the names without the attributes around them are not the page");
	CHECK(btt_devrows_splice(NOCLOSE, (int)sizeof(NOCLOSE) - 1, out, (int)sizeof(out)) == BTT_DR_NOCLOSE, "no closing tag");
	memcpy(same, SONY_PAGE, (size_t)size);
	CHECK(btt_devrows_splice(same, size, same, (int)sizeof(same)) == BTT_DR_NOROOM, "the same buffer");
	CHECK(btt_devrows_splice(same, size, same + 10, (int)sizeof(same) - 10) == BTT_DR_NOROOM, "an overlapping buffer");
	CHECK(btt_devrows_splice(SONY_PAGE, size, 0, 100) == BTT_DR_NOROOM, "no buffer");
	CHECK(btt_devrows_splice(SONY_PAGE, size, out, size) == BTT_DR_NOROOM, "no room for the rows");
	CHECK(btt_devrows_is_menu(SONY_PAGE, size) && !btt_devrows_is_menu(LIST_PAGE, (int)sizeof(LIST_PAGE) - 1), "is_menu");
	CHECK(btt_devrows_skip_reason(BTT_DR_NOCLOSE) == BTT_SKIP_DEVROWS_CLOSE && btt_devrows_skip_reason(BTT_DR_NOROOM) == BTT_SKIP_DEVROWS_ROOM &&
	      !btt_devrows_skip_reason(BTT_DR_NOSEL) && !btt_devrows_skip_reason(BTT_DR_NOTLINK) && !btt_devrows_skip_reason(BTT_DR_DONE) &&
	      !btt_devrows_skip_reason(BTT_DR_NOMATCH) && !btt_devrows_skip_reason(BTT_DR_OLDKERNEL) && btt_devrows_skip_reason(1234) == 0,
	      "only a page of the right device left without its rows is reported (no closing tag, no room); the other results are silent");
	CHECK(!strcmp(btt_devrows_reason(BTT_DR_NOCLOSE), "no closing setting_list") && !strcmp(btt_devrows_reason(1234), "spliced"), "reasons read as words");
}

static void t_ids(void)
{
	static char out[4096];
	static const char *const sony[] = { "id_bluetooth_settings_menu", "id_bt_device_name", "id_bt_device_type", "id_bt_profile" };
	char ids[32][64];
	int r = btt_devrows_splice(SONY_PAGE, (int)sizeof(SONY_PAGE) - 1, out, (int)sizeof(out));
	int n = ids_of(out, r, ids, 32), dev = 0;

	CHECK(n == 10, "ten ids on the spliced page: %d", n);
	for (int i = 0; i < n; i++) {
		for (int j = i + 1; j < n; j++)
			CHECK(strcmp(ids[i], ids[j]), "id %s is repeated", ids[i]);
		if (!strncmp(ids[i], "btt_lk_", 7)) {
			dev++;
			for (unsigned j = 0; j < sizeof(sony) / sizeof(sony[0]); j++)
				CHECK(strcmp(ids[i], sony[j]), "%s is Sony's", ids[i]);
			CHECK(btt_devrows_is_value_id(ids[i]) && btt_devrows_row(ids[i]) >= 0, "%s is a row's own id", ids[i]);
		}
	}
	CHECK(dev == 6, "six rows carry btt_lk_ ids: %d", dev);
	/* each row's title id is one the text hook knows, and the two kinds of id name the same row */
	{
		static const char *const title[] = { "btt_lt_codec", "btt_lt_rate", "btt_lt_latency", "btt_lt_battery", "btt_lt_hcodecs", "btt_lt_signal" };
		static const char *const own[] = { "btt_lk_codec", "btt_lk_rate", "btt_lk_latency", "btt_lk_battery", "btt_lk_hcodecs", "btt_lk_signal" };

		for (int i = 0; i < 6; i++) {
			char want[64];

			snprintf(want, sizeof(want), "title=\"%s\"", title[i]);
			CHECK(strstr(out, want), "title id %s on the page", title[i]);
			CHECK(btt_devrows_row(title[i]) == i && btt_devrows_row(own[i]) == i && !btt_devrows_is_value_id(title[i]), "row %d", i);
		}
	}
	CHECK(btt_devrows_row("btt_lk_nothing") == -1 && btt_devrows_row("btt_lt_") == -1 && btt_devrows_row("btt_codec") == -1 && btt_devrows_row("id_bt_profile") == -1 &&
	      btt_devrows_row("btt_lk_codec_x") == -1 && btt_devrows_row("") == -1 && btt_devrows_row(0) == -1, "other ids are no row");
	/* the product's own page (settings_app/, read from the project root as `make test` runs) uses neither the rows' ids nor their title ids */
	{
		static char src[300 * 1024];
		FILE *f = fopen("settings_app/bluetoothturbo_settings.c", "rb");
		size_t len = f ? fread(src, 1, sizeof(src) - 1, f) : 0;

		CHECK(f && len > 1000, "read settings_app/bluetoothturbo_settings.c (run from the project root)");
		fclose(f);
		src[len] = 0;
		CHECK(!strstr(src, "id=\\\"btt_lk_") && !strstr(src, "id=\\\"btt_lt_"), "the product's page has no id of these rows");
	}
}

/* ---- which device ------------------------------------------------------------------------------------------ */

static void t_parse_id(void)
{
	uint32_t lo = 1, hi = 1;

	/* a device row's id: the address's 12 hex digits, high word first */
	CHECK(btt_devrows_parse_id("556611223344", &lo, &hi) && hi == 0x5566u && lo == 0x11223344u, "hi 5566, lo 11223344");
	CHECK(btt_devrows_parse_id("55661122334A", &lo, &hi) && hi == 0x5566u && lo == 0x1122334Au, "upper case");
	CHECK(btt_devrows_parse_id("000000000000", &lo, &hi) && !lo && !hi, "all zero is an address, not an error");
	lo = hi = 7;
	CHECK(!btt_devrows_parse_id("55661122334", &lo, &hi) && !btt_devrows_parse_id("556611223344a", &lo, &hi) && !btt_devrows_parse_id("", &lo, &hi) &&
	      !btt_devrows_parse_id("55661122334g", &lo, &hi) && !btt_devrows_parse_id("5566 1122334", &lo, &hi) && !btt_devrows_parse_id("id_button_disconnect", &lo, &hi) &&
	      !btt_devrows_parse_id(0, &lo, &hi), "not an address: nothing parsed");
	CHECK(lo == 7 && hi == 7, "a refused id leaves the words alone");
}

static void link_of(BttLinkInfo *li, unsigned codec, uint32_t lo, uint32_t hi)
{
	memset(li, 0, sizeof(*li));
	li->size = sizeof(*li);
	li->version = BTT_LINK_VERSION;
	li->connected = codec != 0;
	li->codec = codec;
	li->sample_rate = codec ? 48000 : 0;
	li->addr_lo = lo;
	li->addr_hi = hi;
}

static void t_gate(void)
{
	BttLinkInfo li;
	int why = 0;

	link_of(&li, BTT_LINK_CODEC_AAC, 0x11223344u, 0x5566u);
	CHECK(btt_devrows_is_link_device(0x11223344u, 0x5566u, &li, &why), "the headphones' own page");
	CHECK(btt_devrows_is_link_device(0x11223344u, 0xFFFF5566u, &li, &why), "the id's high word is 16 bits: the rest does not count");
	li.addr_hi = 0xABCD5566u;
	CHECK(btt_devrows_is_link_device(0x11223344u, 0x5566u, &li, &why), "nor does the kernel module's");
	link_of(&li, BTT_LINK_CODEC_SBC, 0x11223344u, 0x5566u);
	CHECK(btt_devrows_is_link_device(0x11223344u, 0x5566u, &li, &why), "SBC as well");
	CHECK(!btt_devrows_is_link_device(0x11223343u, 0x5566u, &li, &why) && why == BTT_DR_NOTLINK, "another device: low word off by one");
	CHECK(!btt_devrows_is_link_device(0x11223344u, 0x5567u, &li, &why) && why == BTT_DR_NOTLINK, "another device: high word");
	/* the controller's page while the headphones play */
	CHECK(!btt_devrows_is_link_device(0x99887766u, 0x0050u, &li, &why) && why == BTT_DR_NOTLINK, "a controller");
	link_of(&li, 0, 0x11223344u, 0x5566u);
	CHECK(!btt_devrows_is_link_device(0x11223344u, 0x5566u, &li, &why) && why == BTT_DR_NOTLINK, "no audio link now: no rows, even for the same device");
	link_of(&li, BTT_LINK_CODEC_AAC, 0, 0);
	CHECK(!btt_devrows_is_link_device(0, 0, &li, &why), "an unknown address never matches (the all-zero id included)");
	/* the kernel module of 0.2.0 before this one answers 44 bytes: the address is not there */
	link_of(&li, BTT_LINK_CODEC_AAC, 0x11223344u, 0x5566u);
	li.size = 44;
	CHECK(!btt_devrows_is_link_device(0x11223344u, 0x5566u, &li, &why) && why == BTT_DR_OLDKERNEL, "version 1: no address, no rows");
	li.size = 52;
	CHECK(btt_devrows_is_link_device(0x11223344u, 0x5566u, &li, &why), "version 2's 52 bytes");
	CHECK(BTT_DR_LINK_V2_SIZE == 52 && BTT_DR_LINK_V3_SIZE == 56 && BTT_DR_LINK_V4_SIZE == 72 && sizeof(BttLinkInfo) == 72,
	      "version 2 ends with the address, version 3 with the codec list, version 4 with the signal word");
}

/* ---- the values ------------------------------------------------------------------------------------------- */

static void snap_of(BttDevSnap *s, unsigned codec, unsigned kbps, unsigned bitpool, unsigned chmode, unsigned latency, int battery)
{
	BttLinkInfo li;

	link_of(&li, codec, 1, 1);
	li.bitrate_kbps = kbps;
	li.sbc_bitpool = bitpool;
	li.sbc_channel_mode = chmode;
	li.latency = latency;
	btt_devrows_snap(&li, battery, BTT_HC_SBC, str_now, s);
}

static const char *val(const BttDevSnap *s, int row)
{
	static char v[BTT_DR_VALMAX];

	btt_devrows_value(s, row, str_now, v, sizeof(v));
	return v;
}

static void t_values_english(void)
{
	BttDevSnap s;

	g_lang = BTT_LANG_ENGLISH_US;
	snap_of(&s, BTT_LINK_CODEC_AAC, 265, 0, 0, BTT_LAT_LOW, 70);
	CHECK(!strcmp(val(&s, BTT_DR_CODEC), "AAC, 265 kb/s"), "AAC: %s", val(&s, BTT_DR_CODEC));
	CHECK(!strcmp(val(&s, BTT_DR_RATE), "48 kHz"), "rate: %s", val(&s, BTT_DR_RATE));
	CHECK(!strcmp(val(&s, BTT_DR_LATENCY), "Low"), "latency: %s", val(&s, BTT_DR_LATENCY));
	CHECK(!strcmp(val(&s, BTT_DR_BATTERY), "70%"), "battery: %s", val(&s, BTT_DR_BATTERY));
	snap_of(&s, BTT_LINK_CODEC_AAC, 0, 0, 0, BTT_LAT_STANDARD, -1);
	CHECK(!strcmp(val(&s, BTT_DR_CODEC), "AAC"), "AAC, the rate not known: %s", val(&s, BTT_DR_CODEC));
	CHECK(!strcmp(val(&s, BTT_DR_LATENCY), "Standard") && !strcmp(val(&s, BTT_DR_BATTERY), "Not Reported"), "Standard, Not Reported");
	snap_of(&s, BTT_LINK_CODEC_APTX, 384, 0, 0, BTT_LAT_STANDARD, 50);
	CHECK(!strcmp(val(&s, BTT_DR_CODEC), "aptX, 384 kb/s"), "aptX: %s", val(&s, BTT_DR_CODEC));
	snap_of(&s, BTT_LINK_CODEC_APTX, 576, 0, 0, BTT_LAT_STANDARD, 50);
	CHECK(!strcmp(val(&s, BTT_DR_CODEC), "aptX HD, 576 kb/s"), "aptX HD: %s", val(&s, BTT_DR_CODEC));
	snap_of(&s, BTT_LINK_CODEC_LDAC, 660, 0, 0, BTT_LAT_STANDARD, 50);
	CHECK(!strcmp(val(&s, BTT_DR_CODEC), "LDAC, 660 kb/s"), "LDAC: %s", val(&s, BTT_DR_CODEC));
	snap_of(&s, BTT_LINK_CODEC_LDAC, 0, 0, 0, BTT_LAT_STANDARD, 50);
	CHECK(!strcmp(val(&s, BTT_DR_CODEC), "LDAC\xE2\x84\xA2"), "LDAC, the rate not known: %s", val(&s, BTT_DR_CODEC));
	snap_of(&s, BTT_LINK_CODEC_APTX, 0, 0, 0, BTT_LAT_STANDARD, 50);
	CHECK(!strcmp(val(&s, BTT_DR_CODEC), "aptX\xE2\x84\xA2"), "aptX, the rate not known: %s", val(&s, BTT_DR_CODEC));
	snap_of(&s, BTT_LINK_CODEC_SBC, 0, 53, BTT_LINK_CH_JOINT, BTT_LAT_LOWEST, 100);
	CHECK(!strcmp(val(&s, BTT_DR_CODEC), "SBC, bitpool 53, Joint Stereo"), "SBC: %s", val(&s, BTT_DR_CODEC));
	CHECK(!strcmp(val(&s, BTT_DR_LATENCY), "Lowest") && !strcmp(val(&s, BTT_DR_BATTERY), "100%"), "Lowest, 100%%");
	{
		static const char *const want[] = { "Mono", "Dual Channel", "Stereo", "Joint Stereo" };

		for (unsigned m = 1; m <= 4; m++) {
			char w[64];

			snap_of(&s, BTT_LINK_CODEC_SBC, 0, 37, m, 0, 0);
			snprintf(w, sizeof(w), "SBC, bitpool 37, %s", want[m - 1]);
			CHECK(!strcmp(val(&s, BTT_DR_CODEC), w), "channel mode %u: %s", m, val(&s, BTT_DR_CODEC));
		}
	}
	/* the SBC details are known together or not at all (the stream is not started yet) */
	snap_of(&s, BTT_LINK_CODEC_SBC, 0, 0, 0, 0, 5);
	CHECK(!strcmp(val(&s, BTT_DR_CODEC), "SBC") && !strcmp(val(&s, BTT_DR_BATTERY), "5%"), "SBC, details not yet: %s", val(&s, BTT_DR_CODEC));
	snap_of(&s, BTT_LINK_CODEC_SBC, 0, 53, 0, 0, 0);
	CHECK(!strcmp(val(&s, BTT_DR_CODEC), "SBC"), "a channel mode of 0 is not known");
	snap_of(&s, BTT_LINK_CODEC_SBC, 0, 53, 9, 0, 0);
	CHECK(!strcmp(val(&s, BTT_DR_CODEC), "SBC"), "nor is one the table does not have");
	/* the rate: whole kHz, or one decimal; a battery outside 0..100 is not reported */
	snap_of(&s, BTT_LINK_CODEC_AAC, 256, 0, 0, 0, 101);
	CHECK(!strcmp(val(&s, BTT_DR_BATTERY), "Not Reported"), "101 is not a level");
	s.rate = 44100;
	CHECK(!strcmp(val(&s, BTT_DR_RATE), "44.1 kHz"), "44.1: %s", val(&s, BTT_DR_RATE));
	s.rate = 32000;
	CHECK(!strcmp(val(&s, BTT_DR_RATE), "32 kHz"), "32");
	s.rate = 0;
	CHECK(!val(&s, BTT_DR_RATE)[0], "no rate, no text");
	/* a small buffer cuts the text, terminated */
	{
		char tiny[8];

		snap_of(&s, BTT_LINK_CODEC_AAC, 265, 0, 0, 0, 0);
		CHECK(btt_devrows_value(&s, BTT_DR_CODEC, str_now, tiny, sizeof(tiny)) == 7 && !strcmp(tiny, "AAC, 26"), "cut to the buffer: %s", tiny);
	}
}

/* ---- the Headset Codecs row ---------------------------------------------------------------------------------------------- */

static const struct { unsigned bit; const char *name; } k_hc[] = {   /* in the order the row lists them */
	{ BTT_HC_SBC, "SBC" }, { BTT_HC_AAC, "AAC" }, { BTT_HC_APTX, "aptX" }, { BTT_HC_APTX_HD, "aptX HD" }, { BTT_HC_LDAC, "LDAC" },
	{ BTT_HC_MP3, "MP3" }, { BTT_HC_ATRAC, "ATRAC" },
	{ BTT_HC_APTX_ADAPTIVE, "aptX Adaptive" }, { BTT_HC_APTX_LL, "aptX Low Latency" }, { BTT_HC_APTX_TWS, "aptX TWS" }, { BTT_HC_FASTSTREAM, "FastStream" },
	{ BTT_HC_LHDC_V1, "LHDC v1" }, { BTT_HC_LHDC_V2, "LHDC v2" }, { BTT_HC_LHDC_V3, "LHDC v3" }, { BTT_HC_LHDC_V4, "LHDC v4" }, { BTT_HC_LHDC_V5, "LHDC v5" },
	{ BTT_HC_LHDC_LL, "LHDC LL" }, { BTT_HC_LHDC_LLAC, "LLAC" }, { BTT_HC_LHDC, "LHDC" },
	{ BTT_HC_LC3PLUS, "LC3plus" }, { BTT_HC_OPUS, "Opus" }, { BTT_HC_SAMSUNG_SCALABLE, "Samsung Scalable" }, { BTT_HC_SAMSUNG_HD, "Samsung HD" },
};

static void hc_snap(BttDevSnap *s, unsigned mask, unsigned installed, int size)
{
	BttLinkInfo li;

	link_of(&li, BTT_LINK_CODEC_SBC, 1, 1);
	li.codec_mask = mask;
	li.size = (unsigned)size;
	btt_devrows_snap(&li, -1, installed, str_now, s);
}

static void t_headset_codecs(void)
{
	BttDevSnap s;
	char v[BTT_DR_VALMAX];

	g_lang = BTT_LANG_ENGLISH_US;
	/* a headset that lists SBC, AAC, aptX and aptX Adaptive, all three add-ons installed */
	hc_snap(&s, BTT_HC_SBC | BTT_HC_AAC | BTT_HC_APTX | BTT_HC_APTX_ADAPTIVE, btt_devrows_hc_installed(BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_TESTED, BTT_APTX_AVAIL_MODULE | BTT_APTX_AVAIL_TESTED | BTT_APTX_AVAIL_HD, BTT_LDAC_AVAIL_MODULE | BTT_LDAC_AVAIL_TESTED), 56);
	CHECK(!strcmp(val(&s, BTT_DR_HCODECS), "SBC, AAC, aptX, aptX Adaptive (unsupported)"), "all add-ons installed: %s", val(&s, BTT_DR_HCODECS));
	/* with none of them */
	hc_snap(&s, BTT_HC_SBC | BTT_HC_AAC | BTT_HC_APTX | BTT_HC_APTX_ADAPTIVE, btt_devrows_hc_installed(0, 0, 0), 56);
	CHECK(!strcmp(val(&s, BTT_DR_HCODECS), "SBC, AAC (add-on available), aptX (add-on available), aptX Adaptive (unsupported)"), "no add-ons: %s", val(&s, BTT_DR_HCODECS));
	/* every combination of installed add-ons against every list of the codecs they could send, and one the console cannot use, and an unknown one */
	for (unsigned inst = 0; inst < 8; inst++) {
		unsigned want_inst = BTT_HC_SBC | ((inst & 1) ? BTT_HC_AAC : 0) | ((inst & 2) ? BTT_HC_APTX | BTT_HC_APTX_HD : 0) | ((inst & 4) ? BTT_HC_LDAC : 0);

		CHECK(btt_devrows_hc_installed((inst & 1) ? BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_TESTED : 0, (inst & 2) ? BTT_APTX_AVAIL_MODULE | BTT_APTX_AVAIL_TESTED | BTT_APTX_AVAIL_HD : 0,
		                                 (inst & 4) ? BTT_LDAC_AVAIL_MODULE | BTT_LDAC_AVAIL_TESTED : 0) == want_inst, "installed bits for %u", inst);
		for (unsigned list = 1; list < 64; list++) {
			unsigned mask = BTT_HC_SBC, support[5] = { BTT_HC_AAC, BTT_HC_APTX, BTT_HC_APTX_HD, BTT_HC_LDAC, BTT_HC_LHDC };
			char want[BTT_DR_VALMAX];

			for (int b = 0; b < 5; b++)
				if (list & (1u << b))
					mask |= support[b];
			if (list & 32)
				mask |= BTT_HC_OTHER;
			want[0] = 0;
			for (unsigned i = 0; i < sizeof(k_hc) / sizeof(k_hc[0]); i++) {
				if (!(mask & k_hc[i].bit))
					continue;
				if (want[0])
					strcat(want, ", ");
				strcat(want, k_hc[i].name);
				if (!(want_inst & k_hc[i].bit))
					strcat(want, (k_hc[i].bit & (BTT_HC_AAC | BTT_HC_APTX | BTT_HC_APTX_HD | BTT_HC_LDAC)) ? " (add-on available)" : " (unsupported)");
			}
			if (mask & BTT_HC_OTHER)
				strcat(want, ", Other");
			hc_snap(&s, mask, want_inst, 56);
			CHECK(!strcmp(val(&s, BTT_DR_HCODECS), want), "installed %u, list 0x%x: '%s', wanted '%s'", inst, mask, val(&s, BTT_DR_HCODECS), want);
		}
	}
	/* every codec at once (no headset has so many; the row's own buffer would cut it, so a larger one here): the families' order, Other last, the names exactly */
	{
		static char big[4096], want[4096];
		static const char *const k_order[] = { "SBC", "AAC", "aptX", "aptX HD", "LDAC", "MP3", "ATRAC", "aptX Adaptive", "aptX Low Latency", "aptX TWS", "FastStream",
			"LHDC v1", "LHDC v2", "LHDC v3", "LHDC v4", "LHDC v5", "LHDC LL", "LLAC", "LHDC", "LC3plus", "Opus", "Samsung Scalable", "Samsung HD" };

		hc_snap(&s, 0xFFFFFFu, BTT_HC_SBC | BTT_HC_AAC | BTT_HC_APTX | BTT_HC_APTX_HD | BTT_HC_LDAC, 56);
		btt_devrows_value(&s, BTT_DR_HCODECS, str_now, big, sizeof(big));
		want[0] = 0;
		for (unsigned i = 0; i < sizeof(k_order) / sizeof(k_order[0]); i++) {
			strcat(want, i ? ", " : "");
			strcat(want, k_order[i]);
			if (i >= 5)
				strcat(want, " (unsupported)");
		}
		strcat(want, ", Other");
		CHECK(!strcmp(big, want), "the order of every codec:\n  %s\n  %s", big, want);
		for (unsigned i = 0; i < sizeof(k_hc) / sizeof(k_hc[0]); i++)
			CHECK(!strcmp(k_hc[i].name, k_order[i]), "k_hc follows the same order");
	}
	/* not known: never read, or a kernel module of version 2 (52 bytes: no list field) */
	hc_snap(&s, 0, BTT_HC_SBC, 56);
	CHECK(!strcmp(val(&s, BTT_DR_HCODECS), "Not Known Yet"), "never read");
	hc_snap(&s, BTT_HC_SBC | BTT_HC_AAC, BTT_HC_SBC, 52);
	CHECK(!strcmp(val(&s, BTT_DR_HCODECS), "Not Known Yet"), "an older kernel module: %s", val(&s, BTT_DR_HCODECS));
	/* "Other" alone is a list of one */
	hc_snap(&s, BTT_HC_OTHER, BTT_HC_SBC, 56);
	CHECK(!strcmp(val(&s, BTT_DR_HCODECS), "Other"), "only unknown ones");
	/* a failed or unreadable add-on state does not install anything */
	CHECK(btt_devrows_hc_installed(-1, -1, -1) == BTT_HC_SBC && btt_devrows_hc_installed(0, 0, 0) == BTT_HC_SBC, "no add-on, or an error: SBC only");
	CHECK(btt_devrows_hc_installed(BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_TESTED | BTT_AAC_AVAIL_HELD, 0, 0) == BTT_HC_SBC, "an add-on held off this start cannot be sent: not installed");
	CHECK(btt_devrows_hc_installed(BTT_AAC_AVAIL_MODULE, BTT_APTX_AVAIL_MODULE, BTT_LDAC_AVAIL_MODULE) == BTT_HC_SBC, "loaded but not tested yet: not usable");
	CHECK(btt_devrows_hc_installed(0, BTT_APTX_AVAIL_MODULE | BTT_APTX_AVAIL_TESTED, 0) == (BTT_HC_SBC | BTT_HC_APTX), "aptX without the HD encoder's test: aptX only");
	/* the title */
	{
		char t[96];

		hc_snap(&s, BTT_HC_SBC, BTT_HC_SBC, 56);
		btt_devrows_title(&s, BTT_DR_HCODECS, 0, str_now, t, sizeof(t));
		CHECK(!strcmp(t, "Headset Codecs"), "title: %s", t);
		CHECK(btt_devrows_row("btt_lk_hcodecs") == BTT_DR_HCODECS && btt_devrows_row("btt_lt_hcodecs") == BTT_DR_HCODECS && btt_devrows_is_value_id("btt_lk_hcodecs"), "the ids");
		CHECK(BTT_DR_HAS(btt_devrows_xml, (int)sizeof(btt_devrows_xml) - 1, "id=\"btt_lk_hcodecs\"") && BTT_DR_HAS(btt_devrows_xml, (int)sizeof(btt_devrows_xml) - 1, "title=\"btt_lt_hcodecs\""), "the row is on the page");
	}
	/* every language: a list of eight is whole (not cut by the buffer, no placeholder left, ends with the language's own "Other"), and the notes differ from English */
	for (g_lang = 0; g_lang < BTT_LANG_COUNT; g_lang++) {
		/* the most a headset can list is eight endpoints (the Bluetooth code keeps eight): the longest names, each with the longest note, and an unknown one */
		unsigned all = BTT_HC_SBC | BTT_HC_APTX_ADAPTIVE | BTT_HC_APTX_LL | BTT_HC_SAMSUNG_SCALABLE | BTT_HC_SAMSUNG_HD | BTT_HC_LC3PLUS | BTT_HC_LHDC | BTT_HC_OTHER;
		int n;

		hc_snap(&s, all, BTT_HC_SBC, 56);
		n = btt_devrows_value(&s, BTT_DR_HCODECS, str_now, v, sizeof(v));
		CHECK(n > 0 && n < BTT_DR_VALMAX - 1 && !strchr(v, '{') && !strchr(v, '}') && strstr(v, "Samsung HD") && strstr(v, "LHDC"), "language %d: the whole list fits: %d '%s'", g_lang, n, v);
		CHECK(strstr(v, str_now(BTT_I18N_DV_HC_OTHER)) && v[n - 1] == str_now(BTT_I18N_DV_HC_OTHER)[strlen(str_now(BTT_I18N_DV_HC_OTHER)) - 1], "language %d: ends with Other", g_lang);
		/* a headset lists at most eight endpoints, an LHDC one can name two codecs: the nine longest single entries of the row in this language, with their separators,
		 * all fit; and every codec the row can name together (not a list any headset has) is cut at the buffer's end, terminated, never past it */
		{
			int len[24], top[9] = { 0 }, sep = (int)strlen(str_now(BTT_I18N_DV_HC_SEP)), sum;

			for (int i = 0; i < 24; i++) {
				hc_snap(&s, 1u << i, BTT_HC_SBC, 56);
				len[i] = btt_devrows_value(&s, BTT_DR_HCODECS, str_now, v, sizeof(v));
				for (int k = 0; k < 9; k++)
					if (len[i] > top[k]) {
						int t = top[k];

						top[k] = len[i];
						len[i] = t;
					}
			}
			sum = 8 * sep;
			for (int k = 0; k < 9; k++)
				sum += top[k];
			CHECK(sum < BTT_DR_VALMAX - 1, "language %d: the nine longest entries (%d bytes) fit the row's buffer (%d)", g_lang, sum, BTT_DR_VALMAX);
		}
		hc_snap(&s, 0xFFFFFFu, BTT_HC_SBC, 56);
		n = btt_devrows_value(&s, BTT_DR_HCODECS, str_now, v, sizeof(v));
		CHECK(n == (int)strlen(v) && n < (int)sizeof(v) && n > 0 && strstr(v, "LHDC v1"), "language %d: every codec at once: bounded and terminated: %d", g_lang, n);
		hc_snap(&s, 0, BTT_HC_SBC, 56);
		CHECK(btt_devrows_value(&s, BTT_DR_HCODECS, str_now, v, sizeof(v)) > 0 && !strchr(v, '{'), "language %d: not known yet is said", g_lang);
		if (g_lang != BTT_LANG_ENGLISH_US && g_lang != BTT_LANG_ENGLISH_GB) {
			hc_snap(&s, BTT_HC_SBC | BTT_HC_AAC | BTT_HC_APTX_ADAPTIVE, BTT_HC_SBC, 56);
			btt_devrows_value(&s, BTT_DR_HCODECS, str_now, v, sizeof(v));
			CHECK(!strstr(v, "add-on") && !strstr(v, "unsupported"), "language %d: translated: '%s'", g_lang, v);
		}
	}
	g_lang = BTT_LANG_ENGLISH_US;
}

static void t_values_languages(void)
{
	BttDevSnap aac, sbc;
	static const char *const sample[] = { "ja", "en", "fr", "es", "de", "it", "nl", "pt-PT", "ru", "ko", "zh-T", "zh-S", "fi", "sv", "da", "no", "pl", "pt-BR", "en-GB", "tr" };

	/* the separator between a row's name and its value in the one-line fallback: full-width in Japanese and Chinese, spaced in French */
	static const char *const line_sep[] = { "\xef\xbc\x9a", ": ", " : ", ": ", ": ", ": ", ": ", ": ", ": ", ": ", "\xef\xbc\x9a", "\xef\xbc\x9a",
	                                        ": ", ": ", ": ", ": ", ": ", ": ", ": ", ": " };
	/* the languages that write a decimal comma */
	static const int comma[] = { 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1 };

	for (g_lang = 0; g_lang < BTT_LANG_COUNT; g_lang++) {
		char t[160], v[96];

		/* the snapshot renders its values in the language it is taken in */
		snap_of(&aac, BTT_LINK_CODEC_AAC, 265, 0, 0, BTT_LAT_LOW, 70);
		snap_of(&sbc, BTT_LINK_CODEC_SBC, 0, 53, BTT_LINK_CH_DUAL, BTT_LAT_LOWEST, -1);

		for (int row = 0; row < BTT_DR_COUNT; row++) {
			for (int k = 0; k < 2; k++) {
				const BttDevSnap *s = k ? &sbc : &aac;
				int n = btt_devrows_value(s, row, str_now, v, sizeof(v));

				CHECK(n > 0 && n < 90 && !strchr(v, '{') && !strchr(v, '}'), "%s: value of row %d is empty or has a placeholder left: '%s'", sample[g_lang], row, v);
				n = btt_devrows_title(s, row, 0, str_now, t, sizeof(t));
				CHECK(n > 0 && !strchr(t, ':') && !strchr(t, '{'), "%s: title of row %d: '%s'", sample[g_lang], row, t);
				{
					char plain[160], want[200];

					btt_devrows_title(s, row, 0, str_now, plain, sizeof(plain));
					btt_devrows_title(s, row, 1, str_now, t, sizeof(t));
					snprintf(want, sizeof(want), "%s%s%s", plain, line_sep[g_lang], v);
					CHECK(!strcmp(t, want), "%s: one-line title of row %d: '%s', wanted '%s'", sample[g_lang], row, t, want);
				}
			}
		}
		aac.rate = 44100;
		{
			char rv[96];

			btt_devrows_value(&aac, BTT_DR_RATE, str_now, rv, sizeof(rv));
			CHECK(strstr(rv, comma[g_lang] ? "44,1" : "44.1") && !strstr(rv, comma[g_lang] ? "44.1" : "44,1"), "%s: sample rate '%s'", sample[g_lang], rv);
		}
		aac.rate = 48000;
		CHECK(strstr(val(&aac, BTT_DR_CODEC), "265") && strstr(val(&aac, BTT_DR_CODEC), "AAC") && strstr(val(&aac, BTT_DR_RATE), "48"), "%s: figures", sample[g_lang]);
		CHECK(strstr(val(&sbc, BTT_DR_CODEC), "53") && strstr(val(&sbc, BTT_DR_CODEC), "SBC"), "%s: SBC figures", sample[g_lang]);
		CHECK(strstr(val(&aac, BTT_DR_BATTERY), "70"), "%s: battery figure", sample[g_lang]);
	}
	/* the languages that write the percent sign their own way keep it (the headset row's rule) */
	g_lang = BTT_LANG_FRENCH;
	CHECK(!strcmp(val(&aac, BTT_DR_BATTERY), "70 %"), "French: %s", val(&aac, BTT_DR_BATTERY));
	aac.rate = 44100;
	CHECK(!strcmp(val(&aac, BTT_DR_RATE), "44,1 kHz"), "French decimal comma: %s", val(&aac, BTT_DR_RATE));
	g_lang = BTT_LANG_JAPANESE;
	CHECK(!strcmp(val(&aac, BTT_DR_RATE), "44.1 kHz"), "Japanese decimal point: %s", val(&aac, BTT_DR_RATE));
	g_lang = BTT_LANG_TURKISH;
	CHECK(!strcmp(val(&aac, BTT_DR_BATTERY), "%70"), "Turkish: %s", val(&aac, BTT_DR_BATTERY));
	g_lang = BTT_LANG_ENGLISH_US;
}

static void t_titles(void)
{
	BttDevSnap s;
	char t[160];

	g_lang = BTT_LANG_ENGLISH_US;
	snap_of(&s, BTT_LINK_CODEC_AAC, 265, 0, 0, BTT_LAT_LOW, 70);
	btt_devrows_title(&s, BTT_DR_CODEC, 0, str_now, t, sizeof(t));
	CHECK(!strcmp(t, "Codec"), "%s", t);
	btt_devrows_title(&s, BTT_DR_RATE, 0, str_now, t, sizeof(t));
	CHECK(!strcmp(t, "Sample Rate"), "%s", t);
	btt_devrows_title(&s, BTT_DR_LATENCY, 0, str_now, t, sizeof(t));
	CHECK(!strcmp(t, "Bluetooth Latency"), "%s", t);
	btt_devrows_title(&s, BTT_DR_BATTERY, 0, str_now, t, sizeof(t));
	CHECK(!strcmp(t, "Headset Battery"), "%s", t);
	/* the fallback: the title is the whole line */
	btt_devrows_title(&s, BTT_DR_CODEC, 1, str_now, t, sizeof(t));
	CHECK(!strcmp(t, "Codec: AAC, 265 kb/s"), "%s", t);
	btt_devrows_title(&s, BTT_DR_BATTERY, 1, str_now, t, sizeof(t));
	CHECK(!strcmp(t, "Headset Battery: 70%"), "%s", t);
	btt_devrows_title(0, BTT_DR_RATE, 1, str_now, t, sizeof(t));
	CHECK(!strcmp(t, "Sample Rate"), "no snapshot: only the name: %s", t);
	CHECK(btt_devrows_title(&s, 9, 0, str_now, t, sizeof(t)) == 0 && !t[0], "no such row");
}

/* ---- the page plugin's label-value function, wrapped ----------------------------------------------------------- */

/* A fake of the structure the page plugin fills: a string object at +0x18 and bytes around it that must not change. */
typedef struct {
	char before[BTT_DR_OUT_STRING];
	BttPafString text;
	char after[24];
} FakeOut;

static char g_empty[] = "";                 /* the empty string's static data: never allocated, never freed */
static int g_allocs, g_orig_calls;
static int g_alloc_fail;
static const char *g_orig_id;

static void *fake_alloc(unsigned n)
{
	if (g_alloc_fail)
		return 0;
	g_allocs++;
	return malloc(n);
}

/* Sony's own callback, as far as it matters: a value for the ids it knows, nothing for any other, always 0 */
static int fake_orig(const char *id, void *out)
{
	FakeOut *o = (FakeOut *)out;

	g_orig_calls++;
	g_orig_id = id;
	if (!id || !out)                         /* the page plugin's own wrapper refuses these before the callback */
		return (int)0x80100801;
	if (!strcmp(id, "id_bt_device_type")) {
		o->text.data = malloc(14);
		strcpy(o->text.data, "Audio Device");
		o->text.len = 12;
	} else if (!strcmp(id, "id_bt_profile")) {
		o->text.data = malloc(16);
		strcpy(o->text.data, "A2DP,AVRCP");
		o->text.len = 10;
	}
	return 0;
}

/* Sony's callback for a call in which it writes nothing (the structure keeps what an earlier call left) */
static int fake_orig_silent(const char *id, void *out) { (void)id; (void)out; g_orig_calls++; return 0; }

static int fake_fail(const char *id, void *out) { (void)id; (void)out; return (int)0x80100801; }

static void fresh(FakeOut *o)
{
	memset(o, 0xA5, sizeof(*o));
	o->text.data = g_empty;
	o->text.len = 0;
}

static int untouched(const FakeOut *o)
{
	for (unsigned i = 0; i < sizeof(o->before); i++)
		if ((unsigned char)o->before[i] != 0xA5)
			return 0;
	for (unsigned i = 0; i < sizeof(o->after); i++)
		if ((unsigned char)o->after[i] != 0xA5)
			return 0;
	return 1;
}

static void t_wrapper(void)
{
	FakeOut o;
	BttDevSnap s;
	int r;

	CHECK(offsetof(FakeOut, text) == 0x18 && BTT_DR_OUT_STRING == 0x18, "the layout of the fake is the structure's: a string object at +0x18");
	g_lang = BTT_LANG_ENGLISH_US;
	snap_of(&s, BTT_LINK_CODEC_AAC, 265, 0, 0, BTT_LAT_LOW, 70);

	/* a row of ours: the original runs once with the same arguments, then the value is stored */
	fresh(&o);
	g_orig_calls = g_allocs = 0;
	r = btt_devrows_value_hook(fake_orig, "btt_lk_codec", &o, &s, fake_alloc);
	CHECK(r == 0 && g_orig_calls == 1 && !strcmp(g_orig_id, "btt_lk_codec"), "the original ran once for our id");
	CHECK(o.text.data != g_empty && !strcmp(o.text.data, "AAC, 265 kb/s") && o.text.len == 13 && o.text.data[13] == 0, "the text, its length, the terminator: '%s' %d", o.text.data, o.text.len);
	CHECK(g_allocs == 1 && untouched(&o), "one allocation, nothing around the string object changed");
	free(o.text.data);
	/* each row */
	{
		static const char *const id[] = { "btt_lk_codec", "btt_lk_rate", "btt_lk_latency", "btt_lk_battery", "btt_lk_signal" };
		static const char *const want[] = { "AAC, 265 kb/s", "48 kHz", "Low", "70%", "Measuring\xe2\x80\xa6" };

		for (int i = 0; i < 5; i++) {
			fresh(&o);
			btt_devrows_value_hook(fake_orig, id[i], &o, &s, fake_alloc);
			CHECK(!strcmp(o.text.data, want[i]) && o.text.len == (int)strlen(want[i]) && untouched(&o), "%s: '%s'", id[i], o.text.data);
			free(o.text.data);
		}
	}
	/* Sony's own rows: the original's value stays, nothing is allocated by us */
	fresh(&o);
	g_orig_calls = g_allocs = 0;
	r = btt_devrows_value_hook(fake_orig, "id_bt_device_type", &o, &s, fake_alloc);
	CHECK(r == 0 && g_orig_calls == 1 && g_allocs == 0 && !strcmp(o.text.data, "Audio Device") && o.text.len == 12 && untouched(&o), "Sony's row is Sony's");
	free(o.text.data);
	/* any other id: passed on, nothing written */
	fresh(&o);
	g_orig_calls = g_allocs = 0;
	r = btt_devrows_value_hook(fake_orig, "id_bt_device_name", &o, &s, fake_alloc);
	CHECK(r == 0 && g_orig_calls == 1 && g_allocs == 0 && o.text.data == g_empty && o.text.len == 0 && untouched(&o), "an unknown id: nothing written");
	fresh(&o);
	btt_devrows_value_hook(fake_orig, "btt_lk_nothing", &o, &s, fake_alloc);
	btt_devrows_value_hook(fake_orig, "btt_lt_codec", &o, &s, fake_alloc);     /* a title id is not asked for a value */
	CHECK(o.text.data == g_empty && g_allocs == 0, "an id that is no row's own: nothing written");
	/* no snapshot (not the link's device, or the one-line fallback): the original runs, nothing is written */
	fresh(&o);
	g_orig_calls = 0;
	btt_devrows_value_hook(fake_orig, "btt_lk_codec", &o, 0, fake_alloc);
	CHECK(g_orig_calls == 1 && o.text.data == g_empty && g_allocs == 0, "no snapshot: nothing written");
	/* the allocator fails: the structure is left as it was */
	fresh(&o);
	g_alloc_fail = 1;
	btt_devrows_value_hook(fake_orig, "btt_lk_codec", &o, &s, fake_alloc);
	btt_devrows_value_hook(fake_orig, "btt_lk_codec", &o, &s, 0);
	g_alloc_fail = 0;
	CHECK(o.text.data == g_empty && o.text.len == 0 && untouched(&o), "no memory: untouched");
	/* a value that is empty (no rate) is not written either: the empty string stays */
	fresh(&o);
	s.val[BTT_DR_RATE][0] = 0;                 /* a snapshot taken with no rate renders no text */
	btt_devrows_value_hook(fake_orig, "btt_lk_rate", &o, &s, fake_alloc);
	CHECK(o.text.data == g_empty && o.text.len == 0, "an empty value is not stored");
	snap_of(&s, BTT_LINK_CODEC_AAC, 265, 0, 0, BTT_LAT_LOW, 70);
	/* null arguments, and an original that is not there */
	g_orig_calls = 0;
	btt_devrows_value_hook(fake_orig, "btt_lk_codec", 0, &s, fake_alloc);
	btt_devrows_value_hook(fake_orig, 0, &o, &s, fake_alloc);
	CHECK(g_orig_calls == 2, "no crash on null arguments: the original still gets them (it refuses them itself)");
	fresh(&o);
	CHECK(btt_devrows_value_hook(0, "btt_lk_rate", &o, &s, fake_alloc) == 0 && !strcmp(o.text.data, "48 kHz"), "no original: ours still stored");
	free(o.text.data);
	/* the original's result is passed on, whatever it is */
	{
		fresh(&o);
		CHECK(btt_devrows_value_hook(fake_fail, "id_bt_profile", &o, &s, fake_alloc) == (int)0x80100801, "the original's result is returned");
	}
	/* a structure that already holds text (reused): replaced, the new text whole */
	fresh(&o);
	btt_devrows_value_hook(fake_orig, "btt_lk_rate", &o, &s, fake_alloc);
	{
		char *first = o.text.data;

		btt_devrows_value_hook(fake_orig, "btt_lk_latency", &o, &s, fake_alloc);
		CHECK(o.text.data != first && !strcmp(o.text.data, "Low") && o.text.len == 3, "a second value replaces the first");
		free(first);
		free(o.text.data);
	}
}

/* ---- the Signal row ------------------------------------------------------------------------------------------------------- */

/* The row's value is the word the kernel module reports (or "Measuring…" while it has none yet), in every language, and nothing at all from a kernel module that has no
 * signal fields. */
static void t_signal_row(void)
{
	static const char *const en[] = { "Measuring\xe2\x80\xa6", "Weak", "Fair", "Good", "Excellent" };   /* by BTT_LINK_SIGNAL_*: none, weak, fair, good, excellent */
	BttLinkInfo li;
	BttDevSnap s;
	char words[5][96];

	/* the row is on the page, last, after Headset Battery, an ordinary label */
	CHECK(strstr(btt_devrows_xml, "<label id=\"btt_lk_signal\" title=\"btt_lt_signal\"/>") && strstr(btt_devrows_xml, "btt_lk_signal") > strstr(btt_devrows_xml, "btt_lk_battery"),
	      "the Signal row follows Headset Battery");
	CHECK(btt_devrows_row("btt_lk_signal") == BTT_DR_SIGNAL && btt_devrows_row("btt_lt_signal") == BTT_DR_SIGNAL && btt_devrows_is_value_id("btt_lk_signal") && !btt_devrows_is_value_id("btt_lt_signal"), "the ids");
	g_lang = BTT_LANG_ENGLISH_US;
	for (unsigned lvl = BTT_LINK_SIGNAL_NONE; lvl <= BTT_LINK_SIGNAL_EXCELLENT; lvl++) {
		link_of(&li, BTT_LINK_CODEC_AAC, 1, 1);
		li.signal_level = lvl;
		li.link_quality = 200;
		li.rssi = -5;
		btt_devrows_snap(&li, -1, BTT_HC_SBC, str_now, &s);
		CHECK(!strcmp(val(&s, BTT_DR_SIGNAL), en[lvl]), "level %u: '%s', wanted '%s'", lvl, val(&s, BTT_DR_SIGNAL), en[lvl]);
	}
	btt_devrows_title(&s, BTT_DR_SIGNAL, 0, str_now, words[0], sizeof(words[0]));
	CHECK(!strcmp(words[0], "Signal"), "the title: %s", words[0]);
	btt_devrows_title(&s, BTT_DR_SIGNAL, 1, str_now, words[0], sizeof(words[0]));
	CHECK(!strcmp(words[0], "Signal: Excellent"), "the one-line fallback: %s", words[0]);
	/* a kernel module of version 3 (56 bytes) has no signal fields: no value, never a stale or invented word */
	link_of(&li, BTT_LINK_CODEC_AAC, 1, 1);
	li.size = BTT_DR_LINK_V3_SIZE;
	li.signal_level = BTT_LINK_SIGNAL_EXCELLENT;      /* whatever is there past the size is not the module's */
	btt_devrows_snap(&li, -1, BTT_HC_SBC, str_now, &s);
	CHECK(!s.sig_have && !val(&s, BTT_DR_SIGNAL)[0], "version 3: the Signal row has no value");
	/* the page's value function writes nothing for an empty value */
	/* every language: five words, all different from each other, none empty or with a placeholder, the title not empty */
	for (g_lang = 0; g_lang < BTT_LANG_COUNT; g_lang++) {
		for (unsigned lvl = 0; lvl <= BTT_LINK_SIGNAL_EXCELLENT; lvl++) {
			link_of(&li, BTT_LINK_CODEC_AAC, 1, 1);
			li.signal_level = lvl;
			btt_devrows_snap(&li, -1, BTT_HC_SBC, str_now, &s);
			snprintf(words[lvl], sizeof(words[lvl]), "%s", val(&s, BTT_DR_SIGNAL));
			CHECK(words[lvl][0] && !strchr(words[lvl], '{') && strlen(words[lvl]) < 40, "language %d, level %u: '%s'", g_lang, lvl, words[lvl]);
		}
		for (int a = 0; a < 5; a++)
			for (int b = a + 1; b < 5; b++)
				CHECK(strcmp(words[a], words[b]), "language %d: levels %d and %d read the same: '%s'", g_lang, a, b, words[a]);
		btt_devrows_title(&s, BTT_DR_SIGNAL, 0, str_now, words[0], sizeof(words[0]));
		CHECK(words[0][0] && !strchr(words[0], '{'), "language %d: the row's title: '%s'", g_lang, words[0]);
	}
	g_lang = BTT_LANG_ENGLISH_US;
}

/* Sony's Profiles row: ",HFP" or ",HSP" is added to the text the original wrote, for the device the snapshot is for, once, and never otherwise. */
static void t_profiles(void)
{
	FakeOut o;
	BttDevSnap s;
	char *old;

	snap_of(&s, BTT_LINK_CODEC_AAC, 265, 0, 0, BTT_LAT_LOW, 70);
	CHECK(s.voice == BTT_LINK_VOICE_NONE, "no voice profile in the link information: none in the snapshot");
	/* no hands-free link: Sony's text, untouched, nothing allocated */
	fresh(&o);
	g_orig_calls = g_allocs = 0;
	btt_devrows_value_hook(fake_orig, "id_bt_profile", &o, &s, fake_alloc);
	CHECK(g_orig_calls == 1 && g_allocs == 0 && !strcmp(o.text.data, "A2DP,AVRCP") && o.text.len == 10 && untouched(&o), "no hands-free link: Profiles is Sony's");
	free(o.text.data);
	/* the hands-free link: ",HFP", in Sony's comma style with no space, length and terminator right, the old text not freed and not changed */
	s.voice = BTT_LINK_VOICE_HFP;
	fresh(&o);
	g_orig_calls = g_allocs = 0;
	btt_devrows_value_hook(fake_orig, "id_bt_profile", &o, &s, fake_alloc);
	CHECK(g_orig_calls == 1 && g_allocs == 1 && !strcmp(o.text.data, "A2DP,AVRCP,HFP") && o.text.len == 14 && o.text.data[14] == 0 && untouched(&o), "hands-free: A2DP,AVRCP,HFP: '%s'", o.text.data);
	/* a second call that writes nothing must not add it again */
	old = o.text.data;
	btt_devrows_value_hook(fake_orig_silent, "id_bt_profile", &o, &s, fake_alloc);
	CHECK(o.text.data == old && !strcmp(o.text.data, "A2DP,AVRCP,HFP") && o.text.len == 14 && g_allocs == 1, "no double append when the structure is not rewritten");
	free(o.text.data);
	/* a call that rewrites the text each time (the page's own callback does) gets one append each time */
	fresh(&o);
	btt_devrows_value_hook(fake_orig, "id_bt_profile", &o, &s, fake_alloc);
	free(o.text.data);
	btt_devrows_value_hook(fake_orig, "id_bt_profile", &o, &s, fake_alloc);
	CHECK(!strcmp(o.text.data, "A2DP,AVRCP,HFP") && o.text.len == 14, "rewritten and appended again: still one HFP");
	free(o.text.data);
	/* the console's own headset link only: ",HSP" */
	s.voice = BTT_LINK_VOICE_HSP;
	fresh(&o);
	btt_devrows_value_hook(fake_orig, "id_bt_profile", &o, &s, fake_alloc);
	CHECK(!strcmp(o.text.data, "A2DP,AVRCP,HSP") && o.text.len == 14 && untouched(&o), "headset only: A2DP,AVRCP,HSP: '%s'", o.text.data);
	free(o.text.data);
	/* no snapshot (not the device of the link, or the one-line fallback): untouched */
	s.voice = BTT_LINK_VOICE_HFP;
	fresh(&o);
	g_allocs = 0;
	btt_devrows_value_hook(fake_orig, "id_bt_profile", &o, 0, fake_alloc);
	CHECK(g_allocs == 0 && !strcmp(o.text.data, "A2DP,AVRCP"), "another device's page: Profiles is Sony's");
	free(o.text.data);
	/* every other id is untouched, Sony's own device type included */
	fresh(&o);
	btt_devrows_value_hook(fake_orig, "id_bt_device_type", &o, &s, fake_alloc);
	CHECK(g_allocs == 0 && !strcmp(o.text.data, "Audio Device"), "another Sony row is not touched");
	free(o.text.data);
	/* an empty text, text that already names the profile, a failed allocation, a bad length: untouched */
	fresh(&o);
	btt_devrows_value_hook(fake_orig_silent, "id_bt_profile", &o, &s, fake_alloc);
	CHECK(o.text.data == g_empty && o.text.len == 0 && g_allocs == 0, "an empty Profiles text: nothing added");
	fresh(&o);
	o.text.data = strdup("A2DP,HSP,AVRCP");
	o.text.len = 14;
	btt_devrows_value_hook(fake_orig_silent, "id_bt_profile", &o, &s, fake_alloc);
	CHECK(!strcmp(o.text.data, "A2DP,HSP,AVRCP") && g_allocs == 0, "Sony's list already names a voice profile: nothing added");
	o.text.data[1] = 0;
	free(o.text.data);
	fresh(&o);
	o.text.data = strdup("A2DP,AVRCP");
	o.text.len = 10;
	g_alloc_fail = 1;
	btt_devrows_value_hook(fake_orig_silent, "id_bt_profile", &o, &s, fake_alloc);
	g_alloc_fail = 0;
	CHECK(!strcmp(o.text.data, "A2DP,AVRCP") && o.text.len == 10 && untouched(&o), "no memory: untouched");
	o.text.len = 9;                          /* the length does not match the text: not trusted */
	btt_devrows_value_hook(fake_orig_silent, "id_bt_profile", &o, &s, fake_alloc);
	CHECK(!strcmp(o.text.data, "A2DP,AVRCP") && o.text.len == 9, "a length that does not match the text: untouched");
	free(o.text.data);
	/* the link information fields reach the snapshot only from a kernel module that has them (version 4) */
	{
		BttLinkInfo li;

		link_of(&li, BTT_LINK_CODEC_AAC, 1, 1);
		li.voice_profile = BTT_LINK_VOICE_HFP;
		btt_devrows_snap(&li, -1, BTT_HC_SBC, str_now, &s);
		CHECK(s.voice == BTT_LINK_VOICE_HFP, "version 4: the voice profile");
		li.size = BTT_DR_LINK_V3_SIZE;
		btt_devrows_snap(&li, -1, BTT_HC_SBC, str_now, &s);
		CHECK(s.voice == BTT_LINK_VOICE_NONE, "version 3: none");
	}
}

/* ---- the Refresh Codecs button ------------------------------------------------------------------------------------------ */

static int g_ask_calls, g_ask_result;
static int fake_ask(void) { g_ask_calls++; return g_ask_result; }

static void t_refresh_button(void)
{
	static char out[4096], plain[4096];
	char ids[32][64];
	int size = (int)sizeof(SONY_PAGE) - 1, rows = (int)sizeof(btt_devrows_xml_refresh) - 1;
	int r = btt_devrows_splice_x(SONY_PAGE, size, out, (int)sizeof(out), 1);
	int rp = btt_devrows_splice_x(SONY_PAGE, size, plain, (int)sizeof(plain), 0);
	int n, hc, rate;
	BttLinkInfo li;

	/* the page with the button: one button, six labels, each id once, the button right after Headset Codecs and before the rest */
	CHECK(r == size + rows && rp == size + (int)sizeof(btt_devrows_xml) - 1 && r > rp, "grown by the rows and the button: %d / %d", r, rp);
	CHECK(count_of(out, r, "<button ") == 1 && count_of(out, r, "<label id=\"btt_lk_") == 6 && count_of(out, r, "<button id=\"btt_lk_refresh\" title=\"btt_lt_refresh\" hint=\"btt_lh_refresh\"/>") == 1,
	      "one button with its title and help, six labels");
	CHECK(count_of(plain, rp, "<button") == 0 && count_of(plain, rp, "btt_lk_refresh") == 0 && count_of(plain, rp, "btt_lh_") == 0, "the page without it has no button and no help id");
	CHECK(count_of(out, r, "</setting_list>") == count_of(SONY_PAGE, size, "</setting_list>"), "no tag lost or added");
	hc = (int)(strstr(out, "btt_lk_hcodecs") - out);
	rate = (int)(strstr(out, "btt_lk_rate") - out);
	CHECK(hc < (int)(strstr(out, "btt_lk_refresh") - out) && (int)(strstr(out, "btt_lk_refresh") - out) < rate, "the button follows Headset Codecs and comes before Sample Rate");
	/* the same page text otherwise: the plain rows are the button page minus the button */
	{
		const char *b = strstr(out, "<button id=\"btt_lk_refresh\"");
		const char *e = strstr(b, "/>") + 2;
		static char cut[4096];
		int k = (int)(b - out);

		memcpy(cut, out, (size_t)k);
		memcpy(cut + k, e, (size_t)(r - (int)(e - out)));
		CHECK(r - (int)(e - b) == rp && !memcmp(cut, plain, (size_t)rp), "without the button the two pages are the same");
	}
	CHECK(btt_devrows_splice_x(out, r, (char *)malloc(8192), 8192, 1) == BTT_DR_DONE && btt_devrows_splice_x(plain, rp, (char *)malloc(8192), 8192, 0) == BTT_DR_DONE, "a second pass adds nothing");
	CHECK(btt_devrows_splice_x(SONY_PAGE, size, out, size + rows - 1, 1) == BTT_DR_NOROOM && btt_devrows_splice_x(SONY_PAGE, size, out, size + rows, 1) == size + rows, "the buffer for the button page: one byte short, then exact");
	n = ids_of(out, r, ids, 32);
	CHECK(n == 11, "eleven ids on the page with the button: %d", n);
	for (int i = 0; i < n; i++)
		for (int j = i + 1; j < n; j++)
			CHECK(strcmp(ids[i], ids[j]), "id %s is repeated", ids[i]);
	/* the button's ids are no label row: the value function never answers for them, and the label rows' lookups are unchanged */
	CHECK(!btt_devrows_is_value_id("btt_lk_refresh") && btt_devrows_row("btt_lk_refresh") == -1 && btt_devrows_row("btt_lt_refresh") == -1 && btt_devrows_row("btt_lh_refresh") == -1,
	      "the button is not a value row");
	CHECK(btt_devrows_is_refresh_id("btt_lk_refresh") && !btt_devrows_is_refresh_id("btt_lt_refresh") && !btt_devrows_is_refresh_id("btt_lk_codec") && !btt_devrows_is_refresh_id("") && !btt_devrows_is_refresh_id(0),
	      "the button's own id only");

	/* when the button belongs: the kernel module gives a list (version 3) and has none for these headphones */
	link_of(&li, BTT_LINK_CODEC_SBC, 0x11223344u, 0x5566u);
	CHECK(btt_devrows_wants_refresh(&li), "list unknown: the button");
	li.codec_mask = BTT_HC_SBC | BTT_HC_AAC;
	CHECK(!btt_devrows_wants_refresh(&li), "list known: no button");
	li.codec_mask = BTT_HC_OTHER;
	CHECK(!btt_devrows_wants_refresh(&li), "a list of only unnamed codecs is a list: no button");
	link_of(&li, BTT_LINK_CODEC_AAC, 0x11223344u, 0x5566u);
	li.size = 52;
	CHECK(!btt_devrows_wants_refresh(&li), "version 2 (no list field): the kernel module cannot act on the press: no button");
	li.size = 44;
	CHECK(!btt_devrows_wants_refresh(&li), "version 1: no button");

	/* the press */
	g_ask_calls = 0; g_ask_result = BTT_OK;
	CHECK(btt_devrows_press("btt_lk_refresh", fake_ask) == 1 && g_ask_calls == 1, "accepted: handled, asked once");
	g_ask_result = BTT_ERR_BUSY;
	CHECK(btt_devrows_press("btt_lk_refresh", fake_ask) == -1 && g_ask_calls == 2, "refused (busy or start-up): still the button's, the press is dropped");
	CHECK(btt_devrows_press("btt_lk_refresh", 0) == -1, "no call to make: dropped, no crash");
	g_ask_calls = 0; g_ask_result = BTT_OK;
	CHECK(!btt_devrows_press("id_button_disconnect", fake_ask) && !btt_devrows_press("556611223344", fake_ask) && !btt_devrows_press("btt_lk_codec", fake_ask) &&
	      !btt_devrows_press("btt_lk_refresh2", fake_ask) && !btt_devrows_press("btt_lh_refresh", fake_ask) && !btt_devrows_press("", fake_ask) && !btt_devrows_press(0, fake_ask),
	      "every other id goes on to the page's own handler");
	CHECK(g_ask_calls == 0, "the kernel is not asked for any other id");

	/* the texts, in every language: the title is the row's name, the help the short sentence; both present, no placeholder, not longer than a row can show */
	CHECK(btt_devrows_button_text("btt_lt_refresh") == BTT_I18N_ROW_DV_REFRESH && btt_devrows_button_text("btt_lh_refresh") == BTT_I18N_HINT_DV_REFRESH &&
	      btt_devrows_button_text("btt_lk_refresh") == -1 && btt_devrows_button_text("btt_lt_codec") == -1 && btt_devrows_button_text(0) == -1, "the text ids");
	g_lang = BTT_LANG_ENGLISH_US;
	CHECK(!strcmp(str_now(BTT_I18N_ROW_DV_REFRESH), "Refresh Codecs") && !strcmp(str_now(BTT_I18N_HINT_DV_REFRESH), "The headphones reconnect briefly so the Vita can read their codecs."),
	      "English: '%s' / '%s'", str_now(BTT_I18N_ROW_DV_REFRESH), str_now(BTT_I18N_HINT_DV_REFRESH));
	for (g_lang = 0; g_lang < BTT_LANG_COUNT; g_lang++) {
		const char *t = str_now(BTT_I18N_ROW_DV_REFRESH), *h = str_now(BTT_I18N_HINT_DV_REFRESH);

		CHECK(t && h && t[0] && h[0] && !strchr(t, '{') && !strchr(h, '{') && strlen(t) < 64 && strlen(h) < 400, "language %d: both texts there and sane", g_lang);
		if (g_lang != BTT_LANG_ENGLISH_US && g_lang != BTT_LANG_ENGLISH_GB)
			CHECK(strcmp(t, "Refresh Codecs") && strcmp(h, t), "language %d: translated", g_lang);
	}
	g_lang = BTT_LANG_ENGLISH_US;
}

/* The Headset Battery row with no level: Not Connected while no headset is connected, Not Reported once one is (audio link or the headset's own). */
static void t_headset_none(void)
{
	g_lang = BTT_LANG_ENGLISH_US;
	CHECK(!strcmp(str_now(btt_headset_none_id(0, 0)), "Not Connected"), "nothing connected: %s", str_now(btt_headset_none_id(0, 0)));
	CHECK(!strcmp(str_now(btt_headset_none_id(1, 0)), "Not Reported"), "an audio link only: %s", str_now(btt_headset_none_id(1, 0)));
	CHECK(!strcmp(str_now(btt_headset_none_id(0, 1)), "Not Reported"), "the headset's own link only");
	CHECK(!strcmp(str_now(btt_headset_none_id(1, 1)), "Not Reported"), "both links");
	for (g_lang = 0; g_lang < BTT_LANG_COUNT; g_lang++) {
		const char *c = str_now(BTT_I18N_ITEM_NOTCONNECTED), *r = str_now(BTT_I18N_ITEM_NOTREPORTED);

		CHECK(c && c[0] && strlen(c) < 40 && !strchr(c, '{') && strcmp(c, r), "language %d: Not Connected there, short, and not Not Reported", g_lang);
		if (g_lang != BTT_LANG_ENGLISH_US && g_lang != BTT_LANG_ENGLISH_GB)
			CHECK(strcmp(c, "Not Connected"), "language %d: translated", g_lang);
	}
	g_lang = BTT_LANG_ENGLISH_US;
}

typedef struct { const char *name; void (*fn)(void); } Scn;
static const Scn k_scn[] = {
	{ "splice into Sony's page", t_splice_sony },
	{ "splice into a page another plugin grew", t_splice_grown },
	{ "splice refusals", t_splice_refusals },
	{ "ids unique, apart from Sony's and the product's", t_ids },
	{ "the device id: 12 hex digits, hi:lo", t_parse_id },
	{ "which device: the link's, or none", t_gate },
	{ "values in English", t_values_english },
	{ "values and titles in all 20 languages", t_values_languages },
	{ "the Headset Codecs row", t_headset_codecs },
	{ "titles, and the one-line fallback", t_titles },
	{ "the label-value wrapper on a fake structure", t_wrapper },
	{ "the Signal row: the words, every language, an older kernel module", t_signal_row },
	{ "Sony's Profiles row: HFP or HSP added, once, only on the link's device", t_profiles },
	{ "the Refresh Codecs button: page, press, texts", t_refresh_button },
	{ "the Headset Battery row with no level: Not Connected or Not Reported", t_headset_none },
};

int main(void)
{
	for (unsigned i = 0; i < sizeof(k_scn) / sizeof(k_scn[0]); i++) {
		k_scn[i].fn();
		printf("  ok    %s\n", k_scn[i].name);
	}
	printf("test_btpagerows: all %u scenarios passed\n", (unsigned)(sizeof(k_scn) / sizeof(k_scn[0])));
	return 0;
}
