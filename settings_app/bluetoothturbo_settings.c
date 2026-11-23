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
 *   - the page of one Bluetooth device (Settings -> Devices -> Bluetooth Devices -> the device) gets four link rows when it is the device that
 *     holds the audio link (bluetoothturbo_devrows.h): scePafToplevelInitPluginFunctions is hooked to wrap two functions of the Bluetooth
 *     plugin, the one for a selected row (which device the page is for) and the one for a label's value (the rows' values).
 *
 * The Devices page acts only on its own row ids, so a sub-page added among its rows
 * does not disturb it; other plugins (YAMT) may have added rows before us. Rows and the sub-page
 * use ids with the prefix "btt_", unique on the page (tools/check_xml_ids.py).
 */
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/sysmem.h>
#include <taihen.h>
#include <stdint.h>

#include "../include/bluetoothturbo.h"
#include "bluetoothturbo_i18n.h"
#include "bluetoothturbo_devrows.h"

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

/* The page, in order: Enable Bluetooth Turbo, Read Headset Battery (the switch that the value below it depends on), Headset Battery (read only), Low Battery Notifications, Bluetooth Codec Help (a sub-page of read-only lines), Bluetooth Codec,
 * Codec Settings (a sub-page: AAC Bit Rate, Hardware Accelerated AAC, LDAC Bit Rate, aptX HD When Available, SBC Audio Quality; see the block below), Bluetooth Latency,
 * Wireless Controller Response, When Headphones Disconnect, Reconnect After Standby,
 * Bluetooth Sound, Custom Equaliser (a sub-page), Bluetooth Channels, Bluetooth Balance, Maximum Bluetooth Volume,
 * Bluetooth Volume Curve, Bluetooth Name, and Bluetooth Network Name (read only). The "Balance" list shows L4 .. L1, the
 * localised "Center", R1 .. R4. The three AAC rows depend on what the AAC add-on gives (bttGet(BTT_KEY_AAC_AVAIL), read each time the page is built):
 * the codec row always, with SBC and then AAC and aptX (bttGet(BTT_KEY_APTX_AVAIL)) each only when it can be used; AAC Bit Rate with the add-on present; Hardware Accelerated AAC only when the
 * start-up self-check passed for it. The last two are greyed unless the codec is AAC (grey_aac_rows). The latency list keeps the saved key of 0.1's switch ("lowlat"), so a saved "on" is Low.
 * The name is the one text field: a text field must sit in a list of style "edit", as the text fields of the other Turbo
 * plugins do. */
#define BAL_ITEM(k) "<list_item id=\"btt_bal" k "\" title=\"btt_bal_" k "\" value=\"" k "\"/>"

static const char BTT_ROWS_A[] =
 ROW("toggle_switch", "enable", "enable") "/>"
 ROW("toggle_switch", "readbatt", "readbatt") "/>"
 /* Read only, like Bluetooth Network Name below: a list with one item whose title is resolved when the row is built, the
  * level the headset reported ("70 %") or "Not Reported". Picking it changes nothing. */
 ROW("list", "headset", "headset") ">"
  "<list_item id=\"btt_headset0\" title=\"btt_i_headset\" value=\"0\"/>"
 "</list>"
 ROW("toggle_switch", "lowbatt", "lowbatt") "/>";

/* The Bluetooth Codec Help sub-page, directly above the codec row: one nested <setting_list> of read-only lines, each a <label> (the kind Sony's device page uses) with a
 * "?" hint that carries the detail. Every title and hint is answered by the text hook (g_text below), so they are translated like the rest and nothing here is
 * a new widget. The ids are unique on the page (tools/check_xml_ids.py). */
#define HELP_LINE(n) "<label id=\"btt_help" n "\" title=\"btt_t_help" n "\" icon=\"tex_spanner\" hint=\"btt_h_help" n "\"/>"
static const char BTT_CODEC_HELP[] =
 "<setting_list id=\"btt_codechelp\" title=\"btt_t_codechelp\" icon=\"tex_spanner\" hint=\"btt_h_codechelp\">"
  HELP_LINE("1") HELP_LINE("2") HELP_LINE("3") HELP_LINE("4") HELP_LINE("5") HELP_LINE("6")
 "</setting_list>";

/* The codec row. Its list holds SBC, then AAC, aptX and LDAC each only when it can be used (the values are the positions in the list, with no gap: the page may select an item by its place;
 * codec_to_pos and pos_to_codec translate between a position and enum BttCodec). It stays on the page's top level. aptX takes position 2 behind AAC and 1 without it, LDAC 3, 2 or 1:
 * one item for each place it can have (build_group). */
static const char BTT_CODEC_OPEN[] = ROW("list", "codec", "codec") ">" ITEMT("codec_sbc", "btt_i_codec_sbc", "0");
static const char BTT_CODEC_ITEM_AAC[] = ITEMT("codec_aac", "btt_i_codec_aac", "1");
static const char BTT_CODEC_ITEM_APTX[] = ITEMT("codec_aptx", "btt_i_codec_aptx", "2");
static const char BTT_CODEC_ITEM_APTX_2ND[] = ITEMT("codec_aptx", "btt_i_codec_aptx", "1");
static const char BTT_CODEC_ITEM_LDAC_3[] = ITEMT("codec_ldac", "btt_i_codec_ldac", "3");
static const char BTT_CODEC_ITEM_LDAC_2[] = ITEMT("codec_ldac", "btt_i_codec_ldac", "2");
static const char BTT_CODEC_ITEM_LDAC_1[] = ITEMT("codec_ldac", "btt_i_codec_ldac", "1");
static const char BTT_CODEC_CLOSE[] = "</list>";

/* ---- The Codec Settings sub-page: everything that is set per codec ------------------------------------------ *
 * One nested <setting_list>, directly after the codec row. The rows of this block are the ones each codec brings: the LDAC bit rate and aptX HD When Available (with the aptX add-on whose HD self-test passed) are in here too, between BTT_CODEC_PAGE_OPEN and _CLOSE (and in build_group's matching block, and tools/check_xml_ids.py).
 * What is shared by every codec (Bluetooth Latency) stays on the top level. The AAC rows are added only with the add-on (build_group).
 * The bit rate list's values are the indexes of enum BttAacBitrate. The greying of the AAC rows is triggered from the get of the block's LAST
 * keyed row (BTT_CODEC_PAGE_LAST_KEY): it runs after the rows above it have been built, on this sub-page. If rows are added at the end, move
 * the trigger to the new last one. */
static const char BTT_CODEC_PAGE_OPEN[] =
 "<setting_list id=\"btt_codecset\" title=\"btt_t_codecset\" icon=\"tex_spanner\" hint=\"btt_h_codecset\">";
static const char BTT_AAC_RATE[] =
 ROW("list", "aacrate", "aacrate") ">"
  ITEMT("rate", "btt_i_rate128", "0") ITEMT("rate", "btt_i_rate192", "1") ITEMT("rate", "btt_i_rate256", "2") ITEMT("rate", "btt_i_rate320", "3")
 "</list>";
static const char BTT_AAC_ACCEL[] = ROW("toggle_switch", "aacaccel", "aacaccel") "/>";
static const char BTT_LDAC_RATE[] =
 ROW("list", "ldacrate", "ldacrate") ">"
  ITEMT("ldacrate", "btt_i_ldac330", "0") ITEMT("ldacrate", "btt_i_ldac660", "1") ITEMT("ldacrate", "btt_i_ldacabr", "2")
 "</list>";
static const char BTT_APTX_HD[] = ROW("toggle_switch", "aptxhd", "aptxhd") "/>";
static const char BTT_SBC_QUALITY[] =
 ROW("list", "quality", "quality") ">"
  ITEMT("q_stable", "btt_i_q_stable", "0") ITEMT("q_bal", "btt_i_q_bal", "1") ITEMT("q_std", "btt_i_standard", "2")
  ITEMT("q_high", "btt_i_q_high", "3") ITEMT("q_highest", "btt_i_q_highest", "4")
 "</list>";
static const char BTT_CODEC_PAGE_CLOSE[] = "</setting_list>";
#define BTT_CODEC_PAGE_LAST_KEY BTT_KEY_QUALITY
/* ---- end of the Codec Settings block ---------------------------------------------------------------------------- */

static const char BTT_ROWS_A2[] =
 ROW("list", "latency", "lowlat") ">"
  ITEMT("lat_std", "btt_i_standard", "0") ITEMT("lat_low", "btt_i_low", "1") ITEMT("lat_lowest", "btt_i_lowest", "2")
 "</list>"
 ROW("list", "ctrl", "ctrl") ">"
  ITEMT("ctrl_std", "btt_i_standard", "0") ITEMT("ctrl_fast", "btt_i_fast", "1")
 "</list>"
 ROW("list", "disc", "disc") ">"
  ITEMT("disc_mute", "btt_i_disc_mute", "0") ITEMT("disc_pause", "btt_i_disc_pause", "1")
  ITEMT("disc_spk", "btt_i_disc_spk", "2")
 "</list>"
 ROW("toggle_switch", "reconn", "reconn") "/>"
 ROW("toggle_switch", "srconly", "srconly") "/>"
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

/* The most the group can hold: every part, the longest codec row (LDAC's item the longest of the three variants: they are the same length). */
static char g_group[sizeof(BTT_HEAD) + sizeof(BTT_ROWS_A) + sizeof(BTT_CODEC_HELP) + sizeof(BTT_CODEC_OPEN) + sizeof(BTT_CODEC_ITEM_AAC) + sizeof(BTT_CODEC_ITEM_APTX) + sizeof(BTT_CODEC_ITEM_LDAC_3) + sizeof(BTT_CODEC_CLOSE) +
                    sizeof(BTT_CODEC_PAGE_OPEN) + sizeof(BTT_AAC_RATE) + sizeof(BTT_AAC_ACCEL) + sizeof(BTT_LDAC_RATE) + sizeof(BTT_APTX_HD) + sizeof(BTT_SBC_QUALITY) + sizeof(BTT_CODEC_PAGE_CLOSE) +
                    sizeof(BTT_ROWS_A2) + sizeof(BTT_CUSTOM) + sizeof(BTT_ROWS_B) + sizeof(BTT_TAIL)];
/* The spliced page lives in a memory block sized to the page actually received. Other plugins
 * may already have grown it a lot (YAMT adds about 45 KB to the Devices page), so no fixed size
 * is safe. The block grows in 64 KB steps when a bigger page arrives and is never freed while
 * Settings runs: the system may still hold the previous page, and the process ends with
 * Settings anyway. */
#define BTT_PAGE_STEP   (64 * 1024)
#define BTT_PAGE_MAX    (1024 * 1024)  /* 1 MB: anything bigger is left alone */
static char *g_pagebuf;
static int g_pagecap;
/* The page of a Bluetooth device is grown in a block of its own, so it never overwrites the Devices page, which Settings may still hold. */
static char *g_devbuf;
static int g_devcap;

static char *grow_buffer(char **base_p, int *cap_p, int need, const char *name)
{
	if (need > *cap_p) {
		int cap = (need + BTT_PAGE_STEP - 1) & ~(BTT_PAGE_STEP - 1);
		void *base = 0;
		SceUID b = cap <= BTT_PAGE_MAX
		           ? sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, cap, NULL)
		           : -1;
		if (b < 0 || sceKernelGetMemBlockBase(b, &base) < 0 || !base)
			return 0;
		*base_p = (char *)base;
		*cap_p = cap;
	}
	return *base_p;
}

static char *page_buffer(int need) { return grow_buffer(&g_pagebuf, &g_pagecap, need, "BluetoothTurboPage"); }

/* ---- the link rows on the page of one Bluetooth device ------------------------------------------------------ *
 * See bluetoothturbo_devrows.h. Three hooks, all on the UI thread and all short: the select function of the Bluetooth plugin remembers the address
 * of the device chosen; the XML hook (the page is built once per visit) takes the snapshot and adds the rows when the page's device holds the audio
 * link; the function that gives a label's value writes the rows' values. Nothing here reads a file, and the one kernel call (bttGetLinkInfo, with
 * bttGetHeadsetBattery) is the kind the text hook already makes. */

/* Where the rows' values go. 0 = the value is written beside the title by the plugin's label-value function (as Sony's own rows are);
 * 1 = the title is the whole line ("Codec: AAC, 256 kb/s"). The line is used regardless when the function could not be wrapped. */
#ifndef BTT_DEVROWS_ONELINE
#define BTT_DEVROWS_ONELINE 0
#endif

static uint32_t g_sel_lo, g_sel_hi;           /* the address of the device selected last (12 hex digits in the row's id) */
static int g_sel_valid;
static BttDevSnap g_snap;                      /* what the page shows, taken when it is opened; valid only while the rows are on it */
static int g_snap_valid;
static int g_value_wrapped;                    /* funcs[4] of the Bluetooth plugin is wrapped and the allocator was found */
static int (*g_orig_select)(const char *id, int a2, void *a3);
static int (*g_orig_value)(const char *id, void *out);
static void *(*g_paf_new)(unsigned bytes);

static SceUID g_h_funcs = -1;
static tai_hook_ref_t r_funcs;

static const char *devrows_str(int id) { return btt_i18n(id); }
static void *devrows_alloc(unsigned bytes) { return g_paf_new ? g_paf_new(bytes) : 0; }
static int devrows_oneline(void) { return BTT_DEVROWS_ONELINE || !g_value_wrapped; }

/* funcs[6] of the Bluetooth plugin: a row was selected; for a device that is its address. Remember it and pass the call on unchanged. The one id that
 * is not passed on is the Refresh Codecs button's (a button row reports through this function, as Wi-Fi Turbo's Randomize does through the Network page's). */
/* The Refresh Codecs button: ask the kernel module to read the headphones' codecs again (it reconnects them). */
static int devrows_ask(void) { return bttSet(BTT_KEY_REFRESH_CODECS, 1); }

static int devrows_select(const char *id, int a2, void *a3)
{
	uint32_t lo, hi;

	/* The button's press ends here: it is ours and the page's own handler knows nothing of it. Every other id goes on, as before. */
	if (btt_devrows_press(id, devrows_ask))
		return 0;

	if (btt_devrows_parse_id(id, &lo, &hi)) {
		g_sel_lo = lo;
		g_sel_hi = hi;
		g_sel_valid = 1;
	}
	return g_orig_select ? g_orig_select(id, a2, a3) : 0;
}

/* funcs[4] of the Bluetooth plugin: the value of a label row, written into the structure it is given. */
static int devrows_value(const char *id, void *out)
{
	int r = btt_devrows_value_hook(g_orig_value, id, out, g_snap_valid && !devrows_oneline() ? &g_snap : 0, devrows_alloc);
	return r;
}

/* scePafToplevelInitPluginFunctions(a1, a2, funcs): the plugin's functions are set; wrap two of the Bluetooth plugin's. Never slots 3 or 5: they
 * run for every row, constantly. */
static int funcs_hook(void *a1, int a2, uint32_t *funcs)
{
	int res = TAI_CONTINUE(int, r_funcs, a1, a2, funcs);
	const char *plugin = a1 ? (const char *)((uint32_t *)a1)[1] : 0;

	if (plugin && funcs && s_eq(plugin, BTT_DEVROWS_PLUGIN)) {
		if (funcs[6] && funcs[6] != (uint32_t)devrows_select) {
			g_orig_select = (int (*)(const char *, int, void *))funcs[6];
			funcs[6] = (uint32_t)devrows_select;
		}
		if (funcs[4] && funcs[4] != (uint32_t)devrows_value && g_paf_new) {
			g_orig_value = (int (*)(const char *, void *))funcs[4];
			funcs[4] = (uint32_t)devrows_value;
			g_value_wrapped = 1;
		}
	}
	return res;
}

/* The headset's battery for a snapshot: the level, or -1 while Read Headset Battery is off or nothing is held (as the Headset Battery row does). */
static int devrows_battery(void)
{
	BttHeadsetBattery b;

	b.size = sizeof(b);
	b.level = BTT_BATT_UNKNOWN;
	if (bttGet(BTT_KEY_READBATT) == 0 || bttGetHeadsetBattery(&b) != BTT_OK || b.level < 0 || b.level > 100)
		return -1;
	return b.level;
}

static void report_skip(int reason);

/* From the XML hook, for every page: on the page of the device that holds the audio link, add the rows (into a block of our own) and take the
 * snapshot. Returns the new size, with *out set to the grown page, or 0 to leave the page as it is. */
static int devrows_xml(const char *s, int size, const char **out)
{
	BttLinkInfo li;
	unsigned char *z = (unsigned char *)&li;
	int why = 0, r, need;
	char *d;

	if (size <= 0 || !BTT_DR_HAS(s, size, BTT_DEVROWS_PLUGIN) || !btt_devrows_is_menu(s, size))
		return 0;                              /* any other page: the snapshot stays (the rows may still be built from it) */
	g_snap_valid = 0;                          /* a new device page: the rows of the last one are gone with it */
	for (unsigned i = 0; i < sizeof(li); i++)
		z[i] = 0;
	li.size = sizeof(li);
	if (!g_sel_valid) {
		r = BTT_DR_NOSEL;
	} else if (bttGetLinkInfo(&li) != BTT_OK) {
		r = BTT_DR_NOTLINK;
	} else if (!btt_devrows_is_link_device(g_sel_lo, g_sel_hi, &li, &why)) {
		r = why;
	} else {
		int refresh = btt_devrows_wants_refresh(&li);   /* the button, while the headphones' codecs are not known */

		need = size + (int)sizeof(btt_devrows_xml_refresh);
		d = need <= BTT_PAGE_MAX ? grow_buffer(&g_devbuf, &g_devcap, need, "BluetoothTurboDevPage") : 0;
		r = d ? btt_devrows_splice_x(s, size, d, need, refresh) : BTT_DR_NOROOM;
		if (r > 0) {
			btt_devrows_snap(&li, devrows_battery(), btt_devrows_hc_installed(bttGet(BTT_KEY_AAC_AVAIL), bttGet(BTT_KEY_APTX_AVAIL), bttGet(BTT_KEY_LDAC_AVAIL)), devrows_str, &g_snap);
			g_snap_valid = 1;
			*out = d;
		}
	}
	if (btt_devrows_skip_reason(r))               /* the page of the right device was left without its rows: say so once, as the Devices page does */
		report_skip(btt_devrows_skip_reason(r));
	return r > 0 ? r : 0;
}

/* With the other hooks, once system_settings_core is loaded (the plugin's functions are set up after it). The system's allocator is the one
 * the page's string objects free with (library ScePafStdc, the same entry the other Turbo products use for "new"). */
static void devrows_hook(void)
{
	uintptr_t fp = 0;

	if (taiGetModuleExportFunc("ScePaf", 0xA7D28DAE, 0xFC5CD359, &fp) >= 0 && fp >= 0x80000000u && fp < 0xF0000000u)
		g_paf_new = (void *(*)(unsigned))fp;
	g_h_funcs = taiHookFunctionImport(&r_funcs, "SceSettings", 0x4D9A9DD0, 0xF5354FEF, funcs_hook);
}

/* What the AAC add-on gives, as bttGet(BTT_KEY_AAC_AVAIL) says (0 without the kernel module or without the add-on; the bits change after
 * boot, when the self-check finishes, so this is read each time). AAC can be used by the same rule as the kernel module's
 * btt_aac_codec_effective. */
static int aac_avail(void)
{
	int v = bttGet(BTT_KEY_AAC_AVAIL);

	return v < 0 ? 0 : v;
}
/* The same for the aptX add-on, as bttGet(BTT_KEY_APTX_AVAIL) says. */
static int aptx_avail(void)
{
	int v = bttGet(BTT_KEY_APTX_AVAIL);

	return v < 0 ? 0 : v;
}
/* The same for the LDAC add-on, as bttGet(BTT_KEY_LDAC_AVAIL) says. */
static int ldac_avail(void)
{
	int v = bttGet(BTT_KEY_LDAC_AVAIL);

	return v < 0 ? 0 : v;
}
/* The bits the page was built with (build_group). The rest of the page's callbacks (the codec value, the hint, the greying) use these, not
 * a fresh read: the bits can change while Settings is open (the self-check finishes), and a codec value for an item the built list does not
 * have must never be given. -1 until a page has been built. */
static int g_built_avail = -1, g_built_aptx = -1, g_built_ldac = -1;
static int avail_page(void)
{
	return g_built_avail >= 0 ? g_built_avail : aac_avail();
}
static int aptx_page(void)
{
	return g_built_aptx >= 0 ? g_built_aptx : aptx_avail();
}
static int ldac_page(void)
{
	return g_built_ldac >= 0 ? g_built_ldac : ldac_avail();
}
static int ldac_usable(int avail)
{
	return (avail & BTT_LDAC_AVAIL_MODULE) && (avail & BTT_LDAC_AVAIL_TESTED) && !(avail & BTT_LDAC_AVAIL_HELD);
}
static int aac_usable(int avail)
{
	return (avail & BTT_AAC_AVAIL_MODULE) && (avail & BTT_AAC_AVAIL_TESTED) && !(avail & BTT_AAC_AVAIL_HELD);
}
static int aptx_usable(int avail)
{
	return (avail & BTT_APTX_AVAIL_MODULE) && (avail & BTT_APTX_AVAIL_TESTED) && !(avail & BTT_APTX_AVAIL_HELD);
}
/* Whether a codec can be used, by the bits the page was built with (SBC always). */
static int codec_usable(int codec)
{
	return codec == BTT_CODEC_SBC || (codec == BTT_CODEC_AAC && aac_usable(avail_page())) || (codec == BTT_CODEC_APTX && aptx_usable(aptx_page())) ||
	       (codec == BTT_CODEC_LDAC && ldac_usable(ldac_page()));
}
/* The codec in force on this page: an add-on codec only while it can be used (a saved AAC, aptX or LDAC with the add-on gone reads as SBC; the saved key is not rewritten). */
static int codec_shown(void)
{
	int saved = bttGet(BTT_KEY_CODEC);

	return saved > BTT_CODEC_SBC && saved < BTT_CODEC_COUNT && codec_usable(saved) ? saved : BTT_CODEC_SBC;
}

/* The codec row's value for a codec, and back: the list holds SBC, then AAC, aptX and LDAC each if it can be used (the bits the page was built with), and a value is a place in it. */
static int codec_to_pos(int codec)
{
	int pos = 0;

	for (int c = BTT_CODEC_SBC + 1; c <= codec && c < BTT_CODEC_COUNT; c++)
		if (codec_usable(c))
			pos++;
	return pos;
}
static int pos_to_codec(int pos)
{
	int p = 0;

	for (int c = BTT_CODEC_SBC + 1; c < BTT_CODEC_COUNT; c++)
		if (codec_usable(c) && ++p == pos)
			return c;
	return pos > 0 ? pos : BTT_CODEC_SBC;
}

static int build_group(void)
{
	int n = 0, avail = aac_avail(), apt = aptx_avail(), ld = ldac_avail();

	g_built_avail = avail;
	g_built_aptx = apt;
	g_built_ldac = ld;
#define PART(str) { s_cpy(g_group + n, str, sizeof(str) - 1); n += sizeof(str) - 1; }
	PART(BTT_HEAD)
	PART(BTT_ROWS_A)
	PART(BTT_CODEC_HELP)
	PART(BTT_CODEC_OPEN)
	if (aac_usable(avail)) {
		PART(BTT_CODEC_ITEM_AAC)
	}
	if (aptx_usable(apt)) {
		if (aac_usable(avail)) {
			PART(BTT_CODEC_ITEM_APTX)
		} else {
			PART(BTT_CODEC_ITEM_APTX_2ND)
		}
	}
	if (ldac_usable(ld)) {   /* the third place when AAC and aptX are both there, the second with one of them, else the first */
		int before = (aac_usable(avail) ? 1 : 0) + (aptx_usable(apt) ? 1 : 0);

		if (before == 2) {
			PART(BTT_CODEC_ITEM_LDAC_3)
		} else if (before == 1) {
			PART(BTT_CODEC_ITEM_LDAC_2)
		} else {
			PART(BTT_CODEC_ITEM_LDAC_1)
		}
	}
	PART(BTT_CODEC_CLOSE)
	/* the Codec Settings sub-page (keep in step with BTT_CODEC_PAGE_* above) */
	PART(BTT_CODEC_PAGE_OPEN)
	if (avail & BTT_AAC_AVAIL_MODULE) {
		PART(BTT_AAC_RATE)
		if (avail & BTT_AAC_AVAIL_NEON) {
			PART(BTT_AAC_ACCEL)
		}
	}
	if (ld & BTT_LDAC_AVAIL_MODULE) {
		PART(BTT_LDAC_RATE)
	}
	if (aptx_usable(apt) && (apt & BTT_APTX_AVAIL_HD)) {   /* the HD encoder's self-test passed too */
		PART(BTT_APTX_HD)
	}
	PART(BTT_SBC_QUALITY)
	PART(BTT_CODEC_PAGE_CLOSE)
	PART(BTT_ROWS_A2)
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
	{ "btt_t_codecset", BTT_I18N_ROW_CODECSET }, { "btt_h_codecset", BTT_I18N_HINT_CODECSET },
	{ "btt_t_codechelp", BTT_I18N_ROW_CODECHELP }, { "btt_h_codechelp", BTT_I18N_HINT_CODECHELP },
	{ "btt_t_help1", BTT_I18N_HELP_TITLE_1 }, { "btt_h_help1", BTT_I18N_HELP_HINT_1 }, { "btt_t_help2", BTT_I18N_HELP_TITLE_2 }, { "btt_h_help2", BTT_I18N_HELP_HINT_2 },
	{ "btt_t_help3", BTT_I18N_HELP_TITLE_3 }, { "btt_h_help3", BTT_I18N_HELP_HINT_3 }, { "btt_t_help4", BTT_I18N_HELP_TITLE_4 }, { "btt_h_help4", BTT_I18N_HELP_HINT_4 },
	{ "btt_t_help5", BTT_I18N_HELP_TITLE_5 }, { "btt_h_help5", BTT_I18N_HELP_HINT_5 }, { "btt_t_help6", BTT_I18N_HELP_TITLE_6 }, { "btt_h_help6", BTT_I18N_HELP_HINT_6 },
	{ "btt_t_codec",   BTT_I18N_ROW_CODEC },   { "btt_i_codec_sbc", BTT_I18N_ITEM_CODEC_SBC }, { "btt_i_codec_aac", BTT_I18N_ITEM_CODEC_AAC },
	{ "btt_i_codec_aptx", BTT_I18N_ITEM_CODEC_APTX }, { "btt_i_codec_ldac", BTT_I18N_ITEM_CODEC_LDAC },
	{ "btt_t_ldacrate", BTT_I18N_ROW_LDACRATE }, { "btt_h_ldacrate", BTT_I18N_HINT_LDACRATE },
	{ "btt_t_aptxhd", BTT_I18N_ROW_APTXHD }, { "btt_h_aptxhd", BTT_I18N_HINT_APTXHD },
	{ "btt_i_ldac330", BTT_I18N_ITEM_LDAC_330 }, { "btt_i_ldac660", BTT_I18N_ITEM_LDAC_660 }, { "btt_i_ldacabr", BTT_I18N_ITEM_LDAC_ABR },
	{ "btt_t_aacrate", BTT_I18N_ROW_AACRATE }, { "btt_h_aacrate", BTT_I18N_HINT_AACRATE },
	{ "btt_i_rate128", BTT_I18N_ITEM_RATE_128 }, { "btt_i_rate192", BTT_I18N_ITEM_RATE_192 },
	{ "btt_i_rate256", BTT_I18N_ITEM_RATE_256 }, { "btt_i_rate320", BTT_I18N_ITEM_RATE_320 },
	{ "btt_t_aacaccel", BTT_I18N_ROW_AACACCEL }, { "btt_h_aacaccel", BTT_I18N_HINT_AACACCEL },
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
	{ "btt_t_srconly", BTT_I18N_ROW_SRCONLY }, { "btt_h_srconly", BTT_I18N_HINT_SRCONLY },
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

/* The headset's battery for the read-only row: the level (e.g. "70%") while the headset's report is held, otherwise "Not Connected" (no headset connected) or
 * "Not Reported" in the system language. The kernel module answers "unknown" until a headset has reported a level, and while Read Headset Battery is off. */
static const btt_u16 *headset_text(void)
{
	BttHeadsetBattery b;
	int ok;

	b.size = sizeof(b);
	b.level = BTT_BATT_UNKNOWN;
	b.connected = 0;
	ok = bttGetHeadsetBattery(&b) == BTT_OK;
	if (bttGet(BTT_KEY_READBATT) == 0 || !ok || b.level < 0 || b.level > 100) {
		BttLinkInfo li;
		unsigned char *z = (unsigned char *)&li;

		for (unsigned i = 0; i < sizeof(li); i++)
			z[i] = 0;
		li.size = sizeof(li);
		if (bttGetLinkInfo(&li) != BTT_OK)
			li.connected = 0;
		btt_i18n_to_u16(btt_i18n(btt_headset_none_id(li.connected != 0, ok && b.connected != 0)), g_textbuf, (int)(sizeof(g_textbuf) / sizeof(g_textbuf[0])));
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

/* The text hook's answer for a row's title (UTF-16), or 0. Without a snapshot only the row's name is given. */
static const btt_u16 *devrows_title(const char *id)
{
	char t[BTT_DR_VALMAX + 64];   /* the row's name and, in the one-line fallback, its value */
	int row = btt_devrows_row(id);

	if (row < 0)
		return 0;
	btt_devrows_title(g_snap_valid ? &g_snap : 0, row, devrows_oneline(), devrows_str, t, (int)sizeof(t));
	btt_i18n_to_u16(t, g_textbuf, (int)(sizeof(g_textbuf) / sizeof(g_textbuf[0])));
	return g_textbuf;
}

static const btt_u16 *btt_text(const char *id)
{
	if (!s_pre(id, "btt_"))
		return 0;
	{
		int bt = btt_devrows_button_text(id);   /* the Refresh Codecs button's title and help */

		if (bt >= 0) {
			btt_i18n_to_u16(btt_i18n(bt), g_textbuf, (int)(sizeof(g_textbuf) / sizeof(g_textbuf[0])));
			return g_textbuf;
		}
	}
	if (s_pre(id, "btt_lt_"))
		return devrows_title(id);
	if (s_pre(id, "btt_db_"))
		return db_text(id);
	if (s_pre(id, "btt_bal_"))
		return bal_text(id);
	if (s_eq(id, "btt_i_namenow"))
		return namenow_text();
	if (s_eq(id, "btt_i_headset"))
		return headset_text();
	if (s_eq(id, "btt_h_codec")) {
		/* One row, three hints: what the add-ons need is told by what the console can offer now (any one usable; an add-on there but
		 * neither usable yet; neither there). */
		int avail = avail_page(), apt = aptx_page(), ld = ldac_page();
		int str = aac_usable(avail) || aptx_usable(apt) || ldac_usable(ld) ? BTT_I18N_HINT_CODEC :
		          (avail & BTT_AAC_AVAIL_MODULE) || (apt & BTT_APTX_AVAIL_MODULE) || (ld & BTT_LDAC_AVAIL_MODULE) ? BTT_I18N_HINT_CODEC_WAIT : BTT_I18N_HINT_CODEC_NOADDON;

		btt_i18n_to_u16(btt_i18n(str), g_textbuf, (int)(sizeof(g_textbuf) / sizeof(g_textbuf[0])));
		return g_textbuf;
	}
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
		[BTT_KEY_CODEC] = "codec", [BTT_KEY_AAC_ACCEL] = "aacaccel", [BTT_KEY_AAC_BITRATE] = "aacrate", [BTT_KEY_LDAC_RATE] = "ldacrate",
		[BTT_KEY_APTX_HD] = "aptxhd", [BTT_KEY_SOURCE_ONLY] = "srconly",
	};
	for (int i = 0; name && i < BTT_KEY_COUNT; i++)
		if (k[i] && s_eq(name, k[i]))
			return i;
	return -1;
}

/* ---- greying out rows, and rebuilding a row that shows a value --------------------------- *
 * AAC Bit Rate and Hardware Accelerated AAC mean nothing unless the codec is AAC, and Low Battery Notifications nothing while Read Headset Battery is Off.
 * SystemSettingsCore's own row exports enable or grey a row by id and do nothing when the row is not on the page (the add-on absent, or no NEON verdict),
 * so they are called either way. They are only called from callbacks that run once the rows exist: the get of the Codec Settings sub-page's last keyed row
 * (BTT_CODEC_PAGE_LAST_KEY) for the AAC rows, the get of the codec row (a keyed row after Low Battery Notifications on the same page) for that one, and the
 * set of the codec row and of the Read Headset Battery switch (which only reach the rows when they are live).
 * A row reads its value once, when it is built: the read-only Headset Battery row is shown again by removing and adding it with the third export, from the set of the switch, as the VPN page's status row is after its switch. */
#define BTT_NID_ROW_ENABLE   0xF832D8FB
#define BTT_NID_ROW_DISABLE  0x9D058B17
#define BTT_NID_ROW_SHOW     0x84029629   /* (id, 1) add / (id, 0) remove a row; a negative result: the row is not on the page being shown */
static void (*g_row_enable)(const char *id), (*g_row_disable)(const char *id);
static int (*g_row_show)(const char *id, int show);

static int rows_resolve(void)
{
	if (!g_row_enable || !g_row_disable) {
		uintptr_t e = 0, d = 0;

		if (taiGetModuleExportFunc("SceSystemSettingsCore", TAI_ANY_LIBRARY, BTT_NID_ROW_ENABLE, &e) < 0 ||
		    taiGetModuleExportFunc("SceSystemSettingsCore", TAI_ANY_LIBRARY, BTT_NID_ROW_DISABLE, &d) < 0 || !e || !d)
			return 0;
		g_row_enable = (void (*)(const char *))e;
		g_row_disable = (void (*)(const char *))d;
	}
	return 1;
}

static void grey_aac_rows(void)
{
	void (*f)(const char *);

	if (!rows_resolve())
		return;
	f = codec_shown() == BTT_CODEC_AAC ? g_row_enable : g_row_disable;
	f("btt_aacrate");
	f("btt_aacaccel");
	(codec_shown() == BTT_CODEC_LDAC ? g_row_enable : g_row_disable)("btt_ldacrate");   /* LDAC Bit Rate: meaningful for LDAC only */
	(codec_shown() == BTT_CODEC_APTX ? g_row_enable : g_row_disable)("btt_aptxhd");    /* aptX HD When Available: meaningful for aptX only */
}

/* Low Battery Notifications: greyed while Read Headset Battery is Off (the notifications need the level the headset reports, which is read only while the
 * switch is on). The saved choice is not touched. Done from the get of the codec row, which is built after it, and from the set of the switch. */
static void grey_lowbatt_row(void)
{
	if (rows_resolve())
		(bttGet(BTT_KEY_READBATT) != 0 ? g_row_enable : g_row_disable)("btt_lowbatt");
}

/* Shows the Headset Battery row again, so it reads the headset's level (or "Not Reported") as it is now. Removing a row that is not on the page being shown
 * fails: then it is not added either. */
static void refresh_headset_row(void)
{
	if (!g_row_show) {
		uintptr_t f = 0;

		if (taiGetModuleExportFunc("SceSystemSettingsCore", TAI_ANY_LIBRARY, BTT_NID_ROW_SHOW, &f) < 0 || !f)
			return;
		g_row_show = (int (*)(const char *, int))f;
	}
	if (g_row_show("btt_headset", 0) >= 0)
		g_row_show("btt_headset", 1);
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
	{
		const char *grown = 0;
		int n = devrows_xml((const char *)buf, size, &grown);

		if (n > 0) {
			buf = (void *)grown;
			size = n;
		}
	}
	return TAI_CONTINUE(int, r_xml, out, buf, size, a4);
}

static int get_hook(const char *cat, const char *name, int *val)
{
	if (is_btt(cat) && val) {
		int k = key_of(name), v = k >= 0 ? bttGet(k) : 0;
		if (k == BTT_KEY_CODEC)
			v = codec_to_pos(codec_shown());
		*val = v < 0 ? 0 : v;
		/* The sub-page's last keyed row: by the time it is asked for, the AAC rows above it exist and can be greyed. */
		if (k == BTT_CODEC_PAGE_LAST_KEY)
			grey_aac_rows();
		/* The codec row is built after Low Battery Notifications on the same page: that row exists by now. */
		if (k == BTT_KEY_CODEC)
			grey_lowbatt_row();
		return 0;
	}
	return TAI_CONTINUE(int, r_get, cat, name, val);
}

static int set_hook(const char *cat, const char *name, int v)
{
	if (is_btt(cat)) {
		int k = key_of(name);
		if (k >= 0)
			bttSet(k, k == BTT_KEY_CODEC ? pos_to_codec(v) : v);
		if (k == BTT_KEY_CODEC)
			grey_aac_rows();       /* a no-op unless the sub-page's rows are live too; they follow the codec at once when they are */
		if (k == BTT_KEY_READBATT) {
			refresh_headset_row(); /* the Headset Battery row shows the level (or Not Reported) it has now, not the one it was built with */
			grey_lowbatt_row();    /* then Low Battery Notifications follow the switch at once (last: a row that was rebuilt is enabled again) */
		}
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
		devrows_hook();
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
	if (g_h_funcs >= 0) taiHookRelease(g_h_funcs, r_funcs);   /* the plugin's table still points at our wrappers; the process ends with Settings */
	if (g_h_text >= 0) taiHookRelease(g_h_text, r_text);
	if (g_h_info >= 0) taiHookRelease(g_h_info, r_info);
	if (g_h_sets >= 0) taiHookRelease(g_h_sets, r_sets);
	if (g_h_gets >= 0) taiHookRelease(g_h_gets, r_gets);
	if (g_h_set >= 0)  taiHookRelease(g_h_set, r_set);
	if (g_h_get >= 0)  taiHookRelease(g_h_get, r_get);
	if (g_h_xml >= 0)  taiHookRelease(g_h_xml, r_xml);
	if (g_h_load >= 0) taiHookRelease(g_h_load, r_load);
}
