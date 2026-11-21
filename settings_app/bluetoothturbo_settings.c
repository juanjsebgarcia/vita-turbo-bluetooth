/*
 * bluetoothturbo_settings.c - the "Bluetooth Turbo" page in Settings -> Devices, directly
 * below Bluetooth Devices.
 *
 * Same pattern as Wi-Fi Turbo's Settings page: hook sceKernelLoadStartModule
 * in SceSettings to wait for system_settings_core.suprx, then
 *   - scePafMiscLoadXmlLayout: splice the product's sub-page (a nested <setting_list>)
 *     into the Devices page, right after its Bluetooth Devices row;
 *   - SceRegMgr get/set/info (int keys): back the product's rows with a private
 *     /CONFIG/BLUETOOTHTURBO namespace, answered by the kernel module
 *     (bttGet/bttSet). The registry is never written;
 *   - scePafToplevelGetText: every title, list item and '?' hint has an id,
 *     resolved here to the system language (UTF-16).
 * All hooks are import hooks. If something doesn't match, the page just
 * doesn't appear.
 *
 * The Devices page acts only on its own row ids, so a sub-page added among its rows
 * does not disturb it; other plugins (YAMT) may have added rows before us. Rows and the sub-page
 * use ids with the prefix "btt_", unique on the page (tools/check_xml_ids.py).
 */
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/sysmem.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "bluetoothturbo_i18n.h"

/* ---- tiny string helpers (no libc in this module) ----------------------- */
static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static int s_eq(const char *a, const char *b) {
	while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int s_pre(const char *s, const char *p) {
	while (*p) { if (*s++ != *p++) return 0; }
	return 1; }
static const char *mem_find(const char *h, int hn, const char *n, int nn) {
	for (int i = 0; i + nn <= hn; i++) { int j = 0; while (j < nn && h[i + j] == n[j]) j++; if (j == nn) return h + i; }
	return 0; }
static void s_cpy(char *d, const char *s, int n) { for (int i = 0; i < n; i++) d[i] = s[i]; }

/* ---- the page ------------------------------------------------------------ */

#define ROW(tag, id, key) \
	"<" tag " id=\"btt_" id "\" title=\"btt_t_" id "\" icon=\"tex_spanner\" hint=\"btt_h_" id "\" key=\"/CONFIG/BLUETOOTHTURBO/" key "\""
/* A list item whose id and title id are both given (several lists share "Original"). */
#define ITEMT(id, title, v) "<list_item id=\"btt_" id v "\" title=\"" title "\" value=\"" v "\"/>"
/* Every id on the page must be unique: the five bands' items carry the band
 * number (repeated ids crashed Settings while it built the page). */
#define DB_ITEM(n, k) "<list_item id=\"btt_b" n "_db" k "\" title=\"btt_db_" k "\" value=\"" k "\"/>"
#define DB_ITEMS(n) \
	DB_ITEM(n, "0") DB_ITEM(n, "1") DB_ITEM(n, "2") DB_ITEM(n, "3") DB_ITEM(n, "4") \
	DB_ITEM(n, "5") DB_ITEM(n, "6") DB_ITEM(n, "7") DB_ITEM(n, "8") DB_ITEM(n, "9") \
	DB_ITEM(n, "10") DB_ITEM(n, "11") DB_ITEM(n, "12")
#define BAND(n) \
	"<list id=\"btt_band" n "\" title=\"btt_t_band" n "\" icon=\"tex_spanner\" hint=\"btt_h_band\" key=\"/CONFIG/BLUETOOTHTURBO/band" n "\">" \
	DB_ITEMS(n) "</list>"

static const char BTT_HEAD[] =
"<setting_list id=\"btt_settings\" title=\"btt_t_title\" icon=\"tex_spanner\">";

/* The page, in order: Enable Bluetooth Turbo, Headset Battery (read only), Read Headset Battery, Low Battery Notifications, Bluetooth Latency,
 * Bluetooth Audio Quality, Wireless Controller Response, When Headphones Disconnect, Reconnect After Standby,
 * Bluetooth Sound, Custom Equaliser (a sub-page), Bluetooth Channels, Bluetooth Balance, Maximum Bluetooth Volume,
 * Bluetooth Volume Curve, Bluetooth Name, and Current Bluetooth Name (read only). The "Balance" list shows L4 .. L1, the
 * localised "Center", R1 .. R4. The latency list keeps the saved key of 0.1's switch ("lowlat"), so a saved "on" is Low.
 * The name is the one text field: a text field must sit in a list of style "edit", as the text fields of the other Turbo
 * plugins do. */
#define BAL_ITEM(k) "<list_item id=\"btt_bal" k "\" title=\"btt_bal_" k "\" value=\"" k "\"/>"

static const char BTT_ROWS_A[] =
 ROW("toggle_switch", "enable", "enable") "/>"
 /* Read only, like Current Bluetooth Name below: a list with one item whose title is resolved when the row is built, the
  * level the headset reported ("70 %") or "Not Reported". Picking it changes nothing. */
 ROW("list", "headset", "headset") ">"
  "<list_item id=\"btt_headset0\" title=\"btt_i_headset\" value=\"0\"/>"
 "</list>"
 ROW("toggle_switch", "readbatt", "readbatt") "/>"
 ROW("toggle_switch", "lowbatt", "lowbatt") "/>"
 ROW("list", "latency", "lowlat") ">"
  ITEMT("lat_std", "btt_i_standard", "0") ITEMT("lat_low", "btt_i_low", "1") ITEMT("lat_lowest", "btt_i_lowest", "2")
 "</list>"
 ROW("list", "quality", "quality") ">"
  ITEMT("q_stable", "btt_i_q_stable", "0") ITEMT("q_bal", "btt_i_q_bal", "1") ITEMT("q_std", "btt_i_standard", "2")
  ITEMT("q_high", "btt_i_q_high", "3") ITEMT("q_highest", "btt_i_q_highest", "4")
 "</list>"
 ROW("list", "ctrl", "ctrl") ">"
  ITEMT("ctrl_std", "btt_i_standard", "0") ITEMT("ctrl_fast", "btt_i_fast", "1")
 "</list>"
 ROW("list", "disc", "disc") ">"
  ITEMT("disc_mute", "btt_i_disc_mute", "0") ITEMT("disc_pause", "btt_i_disc_pause", "1")
  ITEMT("disc_spk", "btt_i_disc_spk", "2")
 "</list>"
 ROW("toggle_switch", "reconn", "reconn") "/>"
 ROW("list", "sound", "sound") ">"
  ITEMT("snd_original", "btt_i_original", "0") ITEMT("snd_warm", "btt_i_warm", "1")
  ITEMT("snd_bright", "btt_i_bright", "2") ITEMT("snd_bass", "btt_i_bass", "3")
  ITEMT("snd_lively", "btt_i_lively", "4") ITEMT("snd_custom", "btt_i_custom", "5")
 "</list>";

static const char BTT_ROWS_B[] =
 ROW("list", "channels", "channels") ">"
  ITEMT("ch_stereo", "btt_i_stereo", "0") ITEMT("ch_mono", "btt_i_mono", "1") ITEMT("ch_swap", "btt_i_swap", "2")
 "</list>"
 ROW("list", "balance", "balance") ">"
  BAL_ITEM("0") BAL_ITEM("1") BAL_ITEM("2") BAL_ITEM("3") BAL_ITEM("4") BAL_ITEM("5") BAL_ITEM("6") BAL_ITEM("7") BAL_ITEM("8")
 "</list>"
 ROW("list", "volmax", "volmax") ">"
  ITEMT("vm", "btt_i_max0", "0") ITEMT("vm", "btt_i_max1", "1") ITEMT("vm", "btt_i_max2", "2")
  ITEMT("vm", "btt_i_max3", "3") ITEMT("vm", "btt_i_max4", "4") ITEMT("vm", "btt_i_max5", "5")
 "</list>"
 ROW("list", "volcurve", "volcurve") ">"
  ITEMT("vc_std", "btt_i_standard", "0") ITEMT("vc_finer", "btt_i_vc_finer", "1") ITEMT("vc_louder", "btt_i_vc_louder", "2")
 "</list>"
 "<setting_list id=\"btt_name\" title=\"btt_t_name\" icon=\"tex_spanner\" style=\"edit\">"
  "<text_field id=\"btt_name_text\" title=\"btt_t_name\" hint=\"btt_h_name\" key=\"/CONFIG/BLUETOOTHTURBO/name\""
  " keyboard_type=\"basic_latin\" min_length=\"0\" max_length=\"16\"/>"
 "</setting_list>"
 /* A text field shows no value on its row, so a read-only row shows the name: a list with one item whose
  * title is the name itself (the Settings page's "Current MAC address" pattern). Picking it changes nothing. */
 ROW("list", "namenow", "namenow") ">"
  "<list_item id=\"btt_namenow0\" title=\"btt_i_namenow\" value=\"0\"/>"
 "</list>";

static const char BTT_CUSTOM[] =
 "<setting_list id=\"btt_custom\" title=\"btt_t_custom\" icon=\"tex_spanner\" hint=\"btt_h_custom\">"
  BAND("1") BAND("2") BAND("3") BAND("4") BAND("5")
 "</setting_list>";

static const char BTT_TAIL[] = "</setting_list>";

static char g_group[sizeof(BTT_HEAD) + sizeof(BTT_ROWS_A) + sizeof(BTT_CUSTOM) + sizeof(BTT_ROWS_B) + sizeof(BTT_TAIL)];
/* The spliced page lives in a memory block sized to the page actually received. Other plugins
 * may already have grown it a lot (YAMT adds about 45 KB to the Devices page), so no fixed size
 * is safe. The block grows in 64 KB steps when a bigger page arrives and is never freed while
 * Settings runs: the system may still hold the previous page, and the process ends with
 * Settings anyway. */
#define BTT_PAGE_STEP   (64 * 1024)
#define BTT_PAGE_MAX    (1024 * 1024)  /* 1 MB: anything bigger is left alone */
static char *g_pagebuf;
static int g_pagecap;

static char *page_buffer(int need)
{
	if (need > g_pagecap) {
		int cap = (need + BTT_PAGE_STEP - 1) & ~(BTT_PAGE_STEP - 1);
		void *base = 0;
		SceUID b = cap <= BTT_PAGE_MAX
		           ? sceKernelAllocMemBlock("BluetoothTurboPage", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, cap, NULL)
		           : -1;
		if (b < 0 || sceKernelGetMemBlockBase(b, &base) < 0 || !base)
			return 0;
		g_pagebuf = (char *)base;
		g_pagecap = cap;
	}
	return g_pagebuf;
}

static int build_group(void)
{
	int n = 0;
#define PART(str) { s_cpy(g_group + n, str, sizeof(str) - 1); n += sizeof(str) - 1; }
	PART(BTT_HEAD)
	PART(BTT_ROWS_A)
	PART(BTT_CUSTOM)
	PART(BTT_ROWS_B)
	PART(BTT_TAIL)
#undef PART
	return n;
}

/* ---- text ids -> localised UTF-16 ---------------------------------------- */

static const struct { const char *id; int str; } g_text[] = {
	{ "btt_t_title",   BTT_I18N_TITLE },       { "btt_t_enable",  BTT_I18N_ROW_ENABLE },
	{ "btt_h_enable",  BTT_I18N_HINT_ENABLE },
	{ "btt_t_sound",   BTT_I18N_ROW_SOUND },   { "btt_h_sound",   BTT_I18N_HINT_SOUND },
	{ "btt_t_channels", BTT_I18N_ROW_CHANNELS }, { "btt_h_channels", BTT_I18N_HINT_CHANNELS },
	{ "btt_t_balance", BTT_I18N_ROW_BALANCE }, { "btt_h_balance", BTT_I18N_HINT_BALANCE },
	{ "btt_t_latency", BTT_I18N_ROW_LATENCY }, { "btt_h_latency", BTT_I18N_HINT_LATENCY },
	{ "btt_t_quality", BTT_I18N_ROW_QUALITY }, { "btt_h_quality", BTT_I18N_HINT_QUALITY },
	{ "btt_i_q_bal",  BTT_I18N_ITEM_Q_BALANCED }, { "btt_i_q_stable", BTT_I18N_ITEM_Q_STABLE },
	{ "btt_i_q_high", BTT_I18N_ITEM_Q_HIGH }, { "btt_i_q_highest", BTT_I18N_ITEM_Q_HIGHEST },
	{ "btt_t_volcurve", BTT_I18N_ROW_VOLCURVE }, { "btt_h_volcurve", BTT_I18N_HINT_VOLCURVE },
	{ "btt_t_volmax",  BTT_I18N_ROW_VOLMAX },  { "btt_h_volmax",  BTT_I18N_HINT_VOLMAX },
	{ "btt_t_ctrl",    BTT_I18N_ROW_CTRL },    { "btt_h_ctrl",    BTT_I18N_HINT_CTRL },
	{ "btt_t_disc",    BTT_I18N_ROW_DISC },    { "btt_h_disc",    BTT_I18N_HINT_DISC },
	{ "btt_i_disc_mute", BTT_I18N_ITEM_DISC_MUTE }, { "btt_i_disc_pause", BTT_I18N_ITEM_DISC_PAUSE },
	{ "btt_i_disc_spk", BTT_I18N_ITEM_DISC_SPK },
	{ "btt_t_reconn",  BTT_I18N_ROW_RECONN },  { "btt_h_reconn",  BTT_I18N_HINT_RECONN },
	{ "btt_t_name",    BTT_I18N_ROW_NAME },    { "btt_h_name",    BTT_I18N_HINT_NAME },
	{ "btt_t_namenow", BTT_I18N_ROW_NAMENOW }, { "btt_h_namenow", BTT_I18N_HINT_NAMENOW },
	{ "btt_t_headset", BTT_I18N_ROW_HEADSET }, { "btt_h_headset", BTT_I18N_HINT_HEADSET },
	{ "btt_t_readbatt", BTT_I18N_ROW_READBATT }, { "btt_h_readbatt", BTT_I18N_HINT_READBATT },
	{ "btt_t_lowbatt", BTT_I18N_ROW_LOWBATT }, { "btt_h_lowbatt", BTT_I18N_HINT_LOWBATT },
	{ "btt_t_custom",  BTT_I18N_ROW_CUSTOM },  { "btt_h_custom",  BTT_I18N_HINT_CUSTOM },
	{ "btt_t_band1",   BTT_I18N_ROW_B1 },      { "btt_t_band2",   BTT_I18N_ROW_B2 },
	{ "btt_t_band3",   BTT_I18N_ROW_B3 },      { "btt_t_band4",   BTT_I18N_ROW_B4 },
	{ "btt_t_band5",   BTT_I18N_ROW_B5 },      { "btt_h_band",    BTT_I18N_HINT_BAND },
	{ "btt_i_original", BTT_I18N_ITEM_ORIGINAL }, { "btt_i_warm",  BTT_I18N_ITEM_WARM },
	{ "btt_i_bright",  BTT_I18N_ITEM_BRIGHT }, { "btt_i_bass",    BTT_I18N_ITEM_BASS },
	{ "btt_i_lively",  BTT_I18N_ITEM_LIVELY }, { "btt_i_custom",  BTT_I18N_ITEM_CUSTOM },
	{ "btt_i_stereo",  BTT_I18N_ITEM_STEREO }, { "btt_i_mono",    BTT_I18N_ITEM_MONO },
	{ "btt_i_swap",    BTT_I18N_ITEM_SWAP },
	{ "btt_i_standard", BTT_I18N_ITEM_STANDARD }, { "btt_i_low",  BTT_I18N_ITEM_LOW },
	{ "btt_i_lowest",  BTT_I18N_ITEM_LOWEST }, { "btt_i_vc_finer", BTT_I18N_ITEM_VC_FINER },
	{ "btt_i_vc_louder", BTT_I18N_ITEM_VC_LOUDER }, { "btt_i_fast", BTT_I18N_ITEM_FAST },
	{ "btt_i_max0",    BTT_I18N_ITEM_MAX_100 }, { "btt_i_max1",   BTT_I18N_ITEM_MAX_90 },
	{ "btt_i_max2",    BTT_I18N_ITEM_MAX_80 }, { "btt_i_max3",    BTT_I18N_ITEM_MAX_70 },
	{ "btt_i_max4",    BTT_I18N_ITEM_MAX_60 }, { "btt_i_max5",    BTT_I18N_ITEM_MAX_50 },
};

typedef unsigned short btt_u16;
static btt_u16 g_textbuf[800];

/* "btt_db_N" -> "-12 dB" .. "0 dB" .. "+12 dB" (same in every language). */
static const btt_u16 *db_text(const char *id)
{
	int v = 0, n = 0;
	for (const char *p = id + 7; *p >= '0' && *p <= '9'; p++)
		v = v * 10 + (*p - '0');
	int db = BTT_BAND_DB(v);
	if (db > 0) g_textbuf[n++] = '+';
	if (db < 0) { g_textbuf[n++] = '-'; db = -db; }
	if (db >= 10) g_textbuf[n++] = '0' + db / 10;
	g_textbuf[n++] = '0' + db % 10;
	g_textbuf[n++] = ' '; g_textbuf[n++] = 'd'; g_textbuf[n++] = 'B';
	g_textbuf[n] = 0;
	return g_textbuf;
}

/* "btt_bal_N" -> "L4" .. "L1", the localised centre, "R1" .. "R4" (the letters are the same in every language). */
static const btt_u16 *bal_text(const char *id)
{
	int v = id[8] >= '0' && id[8] <= '8' ? id[8] - '0' : BTT_BAL_CENTRE;

	if (v == BTT_BAL_CENTRE) {
		btt_i18n_to_u16(btt_i18n(BTT_I18N_ITEM_CENTRE), g_textbuf, (int)(sizeof(g_textbuf) / sizeof(g_textbuf[0])));
		return g_textbuf;
	}
	g_textbuf[0] = v < BTT_BAL_CENTRE ? 'L' : 'R';
	g_textbuf[1] = '0' + (v < BTT_BAL_CENTRE ? BTT_BAL_CENTRE - v : v - BTT_BAL_CENTRE);
	g_textbuf[2] = 0;
	return g_textbuf;
}

/* The name the console uses now (the console's own when none is set), for the read-only row. Plain ASCII. */
static const btt_u16 *namenow_text(void)
{
	char name[BTT_NAME_MAX + 1];
	int n = 0;

	if (bttGetName(name, (int)sizeof(name)) < 0 || !name[0])
		s_cpy(name, BTT_NAME_DEFAULT, (int)sizeof(BTT_NAME_DEFAULT));
	for (; name[n] && n < BTT_NAME_MAX; n++)
		g_textbuf[n] = (btt_u16)(unsigned char)name[n];
	g_textbuf[n] = 0;
	return g_textbuf;
}

/* The headset's battery for the read-only row: the level (e.g. "70%") while the headset's report is held, otherwise "Not Reported" in the system
 * language. The kernel module answers "unknown" until a headset has reported a level, and while Read Headset Battery is off. */
static const btt_u16 *headset_text(void)
{
	BttHeadsetBattery b;

	b.size = sizeof(b);
	b.level = BTT_BATT_UNKNOWN;
	if (bttGet(BTT_KEY_READBATT) == 0 || bttGetHeadsetBattery(&b) != BTT_OK || b.level < 0 || b.level > 100) {
		btt_i18n_to_u16(btt_i18n(BTT_I18N_ITEM_NOTREPORTED), g_textbuf, (int)(sizeof(g_textbuf) / sizeof(g_textbuf[0])));
		return g_textbuf;
	}
	/* The number in the language's own style ("70%", "70 %", "%70"): the template has {n} where the number goes. */
	{
		const char *t = btt_i18n(BTT_I18N_ITEM_PCT);
		char utf8[48];
		int k = 0;

		for (; *t && k < (int)sizeof(utf8) - 5; t++) {
			if (t[0] == '{' && t[1] == 'n' && t[2] == '}') {
				if (b.level >= 100)
					utf8[k++] = '1';
				if (b.level >= 10)
					utf8[k++] = (char)('0' + (b.level / 10) % 10);
				utf8[k++] = (char)('0' + b.level % 10);
				t += 2;
			} else {
				utf8[k++] = *t;
			}
		}
		utf8[k] = 0;
		btt_i18n_to_u16(utf8, g_textbuf, (int)(sizeof(g_textbuf) / sizeof(g_textbuf[0])));
	}
	return g_textbuf;
}

static const btt_u16 *btt_text(const char *id)
{
	if (!s_pre(id, "btt_"))
		return 0;
	if (s_pre(id, "btt_db_"))
		return db_text(id);
	if (s_pre(id, "btt_bal_"))
		return bal_text(id);
	if (s_eq(id, "btt_i_namenow"))
		return namenow_text();
	if (s_eq(id, "btt_i_headset"))
		return headset_text();
	for (unsigned i = 0; i < sizeof(g_text) / sizeof(g_text[0]); i++)
		if (s_eq(id, g_text[i].id)) {
			btt_i18n_to_u16(btt_i18n(g_text[i].str), g_textbuf,
			                (int)(sizeof(g_textbuf) / sizeof(g_textbuf[0])));
			return g_textbuf;
		}
	return 0;
}

/* ---- registry keys -> settings ----------------------------------------------- */

static int is_btt(const char *cat) { return cat && s_pre(cat, "/CONFIG/BLUETOOTHTURBO"); }

static int key_of(const char *name)
{
	static const char *const k[BTT_KEY_COUNT] = {
		[BTT_KEY_ENABLE] = "enable", [BTT_KEY_LOWLAT] = "lowlat", [BTT_KEY_SOUND] = "sound",
		[BTT_KEY_BAND1] = "band1", [BTT_KEY_BAND2] = "band2", [BTT_KEY_BAND3] = "band3",
		[BTT_KEY_BAND4] = "band4", [BTT_KEY_BAND5] = "band5",
		[BTT_KEY_CHANNELS] = "channels", [BTT_KEY_BALANCE] = "balance", [BTT_KEY_VOLCURVE] = "volcurve",
		[BTT_KEY_VOLMAX] = "volmax", [BTT_KEY_CTRL] = "ctrl",
		[BTT_KEY_QUALITY] = "quality",
		[BTT_KEY_DISCONNECT] = "disc", [BTT_KEY_RECONNECT] = "reconn", [BTT_KEY_LOWBATT] = "lowbatt",
		[BTT_KEY_READBATT] = "readbatt",
	};
	for (int i = 0; name && i < BTT_KEY_COUNT; i++)
		if (k[i] && s_eq(name, k[i]))
			return i;
	return -1;
}

/* ---- hooks ----------------------------------------------------------------- */

static SceUID g_h_load = -1, g_h_xml = -1, g_h_get = -1, g_h_set = -1, g_h_gets = -1, g_h_sets = -1, g_h_info = -1,
              g_h_text = -1;


static tai_hook_ref_t r_load, r_xml, r_get, r_set, r_gets, r_sets, r_info, r_text;
static int g_installed;

#define HAS(lit) mem_find(s, size, lit, (int)sizeof(lit) - 1)

/* scePafMiscLoadXmlLayout(out, buf, size, a4): put the product's sub-page in Settings -> Devices,
 * directly below Bluetooth Devices. The page is matched by its own plugin= attribute (one
 * document, the same on every firmware 3.60-3.74), and the group goes right after the page's
 * one Bluetooth Devices row, <link id="bluetooth_settings" ... />. The anchor includes the
 * closing quote, so it can't match another id; if it isn't found, nothing is spliced (never a
 * guess at another position: the page's last </setting_list> closes a different sub-page). */
static const char k_anchor[] = "<link id=\"bluetooth_settings\"";

/* Tells the kernel module (once per reason; the page is built once per Settings visit, not per frame) that the Devices
 * page was found and left as it was: the kernel module's worker puts the reason in the log. */
static void report_skip(int reason)
{
	static int sent;

	if (!(sent & (1 << reason))) {
		sent |= 1 << reason;
		bttSet(BTT_KEY_PAGE_SKIP, reason);
	}
}

static int xml_hook(void *out, void *buf, int size, void *a4)
{
	const char *s = (const char *)buf;
	if (size > 0 && HAS("plugin=\"peripherals_settings_plugin\"")) {
		const char *a = 0, *e = 0;
		char *pb = 0;
		int skip = 0;

		if (size > BTT_PAGE_MAX - (int)sizeof(g_group)) {
			skip = BTT_SKIP_SIZE;
		} else {
			a = mem_find(s, size, k_anchor, (int)sizeof(k_anchor) - 1);
			e = a ? mem_find(a, size - (int)(a - s), "/>", 2) : 0;
			pb = e ? page_buffer(size + (int)sizeof(g_group)) : 0;
			skip = !e ? BTT_SKIP_ANCHOR : !pb ? BTT_SKIP_ALLOC : 0;
		}
		if (skip)
			report_skip(skip);
		if (pb) {
			int head = (int)(e + 2 - s), n = build_group();
			s_cpy(g_pagebuf, s, head);
			s_cpy(g_pagebuf + head, g_group, n);
			s_cpy(g_pagebuf + head + n, s + head, size - head);
			buf = g_pagebuf;
			size += n;
		}
	}
	return TAI_CONTINUE(int, r_xml, out, buf, size, a4);
}

static int get_hook(const char *cat, const char *name, int *val)
{
	if (is_btt(cat) && val) {
		int k = key_of(name), v = k >= 0 ? bttGet(k) : 0;
		*val = v < 0 ? 0 : v;
		return 0;
	}
	return TAI_CONTINUE(int, r_get, cat, name, val);
}

static int set_hook(const char *cat, const char *name, int v)
{
	if (is_btt(cat)) {
		int k = key_of(name);
		if (k >= 0)
			bttSet(k, v);
		return 0;
	}
	return TAI_CONTINUE(int, r_set, cat, name, v);
}

/* String keys: the Bluetooth name. GetKeyStr must succeed, or the row is not built; SetKeyStr is where the typed
 * text arrives. Text the name cannot be (more than 16 characters, anything but printable ASCII) is refused
 * here, by not storing it; the kernel module checks it again. An empty field means the console's own name. */
static int gets_hook(const char *cat, const char *name, char *buf, int size)
{
	if (is_btt(cat)) {
		if (buf && size > 0) {
			buf[0] = 0;
			if (name && s_eq(name, "name") && bttGetName(buf, size) < 0)
				buf[0] = 0;
		}
		return 0;
	}
	return TAI_CONTINUE(int, r_gets, cat, name, buf, size);
}

static int sets_hook(const char *cat, const char *name, const char *str, int len)
{
	if (is_btt(cat)) {
		if (name && s_eq(name, "name")) {
			/* At most len bytes, in a terminated copy (the caller's buffer may not be), with room for one
			 * character more than the longest name so that a longer one is seen and refused. */
			char t[BTT_NAME_MAX + 2];
			int n = 0;

			while (str && n < len && n < (int)sizeof(t) - 1 && str[n]) {
				t[n] = str[n];
				n++;
			}
			t[n] = 0;
			if (btt_name_valid(t))
				bttSetName(t);
		}
		return 0;
	}
	return TAI_CONTINUE(int, r_sets, cat, name, str, len);
}

static int info_hook(const char *cat, void *info, int unk)
{
	if (is_btt(cat)) {
		if (info) {
			/* (size << 16) | 1 = a string key of `size` bytes; 0x00040000 = an int key */
			const char *name = *(const char **)((char *)info + 4);
			*(int *)((char *)info + 8) = name && s_eq(name, "name") ? 0x00200001 : 0x00040000;
		}
		return 0;
	}
	return TAI_CONTINUE(int, r_info, cat, info, unk);
}

static void *text_hook(void *arg, char **msg)
{
	if (msg && *msg) {
		const btt_u16 *t = btt_text(*msg);
		if (t) return (void *)t;
	}
	return TAI_CONTINUE(void *, r_text, arg, msg);
}

/* sceKernelLoadStartModule as SceSettings imports it takes SIX arguments; a
 * five-argument hook corrupts the shared hook chain (it broke HENkaku's own
 * Settings entry in Wi-Fi Turbo's early builds). */
static SceUID load_hook(const char *path, SceSize args, void *argp,
                        int flags, void *option, int *status)
{
	SceUID ret = TAI_CONTINUE(SceUID, r_load, path, args, argp, flags, option, status);
	if (!g_installed && path && mem_find(path, s_len(path), "system_settings_core", 20)) {
		g_installed = 1;
		g_h_xml  = taiHookFunctionImport(&r_xml,  "SceSettings", 0x3D643CE8, 0x19FE55A8, xml_hook);
		g_h_get  = taiHookFunctionImport(&r_get,  "SceSystemSettingsCore", 0xC436F916, 0x16DDF3DC, get_hook);
		g_h_set  = taiHookFunctionImport(&r_set,  "SceSystemSettingsCore", 0xC436F916, 0xD72EA399, set_hook);
		g_h_gets = taiHookFunctionImport(&r_gets, "SceSystemSettingsCore", 0xC436F916, 0xE188382F, gets_hook);   /* sceRegMgrGetKeyStr */
		g_h_sets = taiHookFunctionImport(&r_sets, "SceSystemSettingsCore", 0xC436F916, 0x41D320C5, sets_hook);   /* sceRegMgrSetKeyStr */
		g_h_info = taiHookFunctionImport(&r_info, "SceSystemSettingsCore", 0xC436F916, 0x58421DD1, info_hook);
		g_h_text = taiHookFunctionImport(&r_text, "SceSystemSettingsCore", 0x4D9A9DD0, 0x19CEFDA7, text_hook);
	}
	return ret;
}

void btt_settings_install(void)
{
	g_h_load = taiHookFunctionImport(&r_load, "SceSettings", 0xCAE9ACE6, 0x2DCC4AFA, load_hook);
	/* The kernel module logs it (from its worker, not here). An older kernel
	 * module doesn't know the key and just refuses. */
	bttSet(BTT_KEY_SETTINGS_VER, BTT_VER_NUM);
}

void btt_settings_release(void)
{
	if (g_h_text >= 0) taiHookRelease(g_h_text, r_text);
	if (g_h_info >= 0) taiHookRelease(g_h_info, r_info);
	if (g_h_sets >= 0) taiHookRelease(g_h_sets, r_sets);
	if (g_h_gets >= 0) taiHookRelease(g_h_gets, r_gets);
	if (g_h_set >= 0)  taiHookRelease(g_h_set, r_set);
	if (g_h_get >= 0)  taiHookRelease(g_h_get, r_get);
	if (g_h_xml >= 0)  taiHookRelease(g_h_xml, r_xml);
	if (g_h_load >= 0) taiHookRelease(g_h_load, r_load);
}
