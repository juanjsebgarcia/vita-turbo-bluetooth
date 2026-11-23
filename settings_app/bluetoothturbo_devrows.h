/*
 * bluetoothturbo_devrows.h - the link rows on the system's page for one Bluetooth device (Settings -> Devices -> Bluetooth Devices ->
 * the device), with no system call, so the host test compiles it as it is (test/test_btpagerows.c).
 *
 * The page lists the device's name, its type and its profiles: rows of the kind "label", whose title is on the left and whose value the
 * page's own code writes on the right. Six rows of the same kind are added: Codec, Headset Codecs (what the headphones list: BTT_HC_*, with the
 * add-ons that are installed named plainly, the ones that could be installed marked "(add-on available)" and the ones the console cannot use marked
 * "(unsupported)"), Sample Rate, Bluetooth Latency, Headset Battery and Signal (Excellent, Good, Fair or Weak, from the kernel module's smoothed radio readings, or "Measuring…"
 * for the few seconds after a connection), but only on the page of the device that holds the audio link now. Their values are a snapshot taken when the page is opened.
 * When that device's codec list is not known ("Not Known Yet") a sixth row, the button Refresh Codecs, follows Headset Codecs: pressing it asks the kernel
 * module to disconnect the headphones and connect them again (BTT_KEY_REFRESH_CODECS), which makes the console read the list. It is a plain `button` row
 * (the widget the other products' pages use), its press arrives at the page plugin's funcs[6] wrapper that is already here, and only that wrapper handles it.
 *
 * Three parts, wired in by bluetoothturbo_settings.c:
 *   - the page: recognise it, add the rows before its last </setting_list>, into a buffer the caller owns;
 *   - the texts: a row's title, and its value as one text ("AAC, 256 kb/s", "SBC, bitpool 53, Joint Stereo", "48 kHz", "44,1 kHz" where the language writes the decimal comma, ...), from the
 *     translations (a function the caller gives, so the test can use the real table);
 *   - the value the page asks for: the page plugin's "value of a label" function is wrapped (btt_devrows_value_hook): the original runs
 *     for everything, and for the rows' ids only, the text is stored in the same place the original stores its own. Sony's callback writes
 *     UTF-8 into a string object at +0x18 of the structure it is given: {char *data; int len} (len 0, data the empty string's static
 *     address when empty); the data is allocated with the system's allocator and freed by whoever owns the structure.
 *
 * The fallback, when the value cannot be written (the function not found, or BTT_DEVROWS_ONELINE): the row's title is the whole line,
 * "Codec: AAC, 256 kb/s", and the page shows the value nowhere else.
 */
#ifndef BLUETOOTHTURBO_DEVROWS_H
#define BLUETOOTHTURBO_DEVROWS_H

#include <stddef.h>
#include <stdint.h>

#include "../include/bluetoothturbo.h"
#include "bluetoothturbo_i18n.h"

#define BTT_DEVROWS_PLUGIN  "bluetooth_settings_plugin"
#define BTT_DEVROWS_ROOT    "id_bluetooth_settings_menu"

/* The rows. Ids are unique on the page and keep clear of Sony's (id_bt_*) and of the product's own page (btt_<name>, never btt_lk_*). A row's
 * own id (btt_lk_<name>) is what the page asks the value of; its title id (btt_lt_<name>) goes to the text hook. */
#define BTT_DEVROWS_HEAD \
	"<label id=\"btt_lk_codec\" title=\"btt_lt_codec\"/>" \
	"<label id=\"btt_lk_hcodecs\" title=\"btt_lt_hcodecs\"/>"
/* The button (id btt_lk_refresh, title btt_lt_refresh, help btt_lh_refresh), only while the list is not known. */
#define BTT_DEVROWS_BUTTON \
	"<button id=\"btt_lk_refresh\" title=\"btt_lt_refresh\" hint=\"btt_lh_refresh\"/>"
#define BTT_DEVROWS_TAIL \
	"<label id=\"btt_lk_rate\" title=\"btt_lt_rate\"/>" \
	"<label id=\"btt_lk_latency\" title=\"btt_lt_latency\"/>" \
	"<label id=\"btt_lk_battery\" title=\"btt_lt_battery\"/>" \
	"<label id=\"btt_lk_signal\" title=\"btt_lt_signal\"/>"
#define BTT_DEVROWS_XML          BTT_DEVROWS_HEAD BTT_DEVROWS_TAIL                      /* the list is known                 */
#define BTT_DEVROWS_XML_REFRESH  BTT_DEVROWS_HEAD BTT_DEVROWS_BUTTON BTT_DEVROWS_TAIL   /* the list is not known: + the button */
static const char btt_devrows_xml[] = BTT_DEVROWS_XML;
static const char btt_devrows_xml_refresh[] = BTT_DEVROWS_XML_REFRESH;

enum { BTT_DR_CODEC, BTT_DR_RATE, BTT_DR_LATENCY, BTT_DR_BATTERY, BTT_DR_HCODECS, BTT_DR_SIGNAL, BTT_DR_COUNT };

/* ---- tiny helpers (no libc) --------------------------------------------------------------- */

static inline int btt_dr_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline int btt_dr_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static inline int btt_dr_pre(const char *s, const char *p) { while (*p) { if (*s++ != *p++) return 0; } return 1; }
static inline void btt_dr_copy(char *d, const char *s, int n) { for (int i = 0; i < n; i++) d[i] = s[i]; }

static inline const char *btt_dr_find(const char *h, int hn, const char *n, int nn)
{
	for (int i = 0; i + nn <= hn; i++) {
		int j = 0;

		while (j < nn && h[i + j] == n[j])
			j++;
		if (j == nn)
			return h + i;
	}
	return 0;
}

static inline const char *btt_dr_rfind(const char *h, int hn, const char *n, int nn)
{
	for (int i = hn - nn; i >= 0; i--) {
		int j = 0;

		while (j < nn && h[i + j] == n[j])
			j++;
		if (j == nn)
			return h + i;
	}
	return 0;
}

#define BTT_DR_HAS(s, size, lit) btt_dr_find(s, size, lit, (int)sizeof(lit) - 1)

/* ---- the page and the splice ------------------------------------------------------------------ */

/* Is this the per-device page? Its plugin name and its root id are both in the text (Sony's device list page, and any other plugin's page, has
 * not both), found by the attributes' own text. */
static inline int btt_devrows_is_menu(const char *s, int size)
{
	return size > 0 && BTT_DR_HAS(s, size, "plugin=\"" BTT_DEVROWS_PLUGIN "\"") && BTT_DR_HAS(s, size, "id=\"" BTT_DEVROWS_ROOT "\"");
}

enum {
	BTT_DR_NOMATCH = -1,    /* not the per-device page                                                      */
	BTT_DR_NOCLOSE = -2,    /* no </setting_list> to splice before                                          */
	BTT_DR_NOROOM = -3,     /* the destination is too small, or overlaps the page                           */
	BTT_DR_DONE = -4,       /* the rows are already on the page (another pass)                              */
	BTT_DR_NOSEL = -5,      /* no device selected yet (the page was not opened from the list)               */
	BTT_DR_NOTLINK = -6,    /* the page's device is not the one holding the audio link                      */
	BTT_DR_OLDKERNEL = -7,  /* the kernel module does not say which device the link is with (version 1)     */
};

/* Splices the rows before the page's LAST </setting_list> (the root's: the page may already be grown by another plugin, whose nested lists end
 * before it) into dst (cap bytes, not overlapping s), with the Refresh Codecs button when `refresh` is set. Returns the new size, or a BTT_DR_* reason
 * (negative): nothing is written to dst then. */
static inline int btt_devrows_splice_x(const char *s, int size, char *dst, int cap, int refresh)
{
	static const char close_tag[] = "</setting_list>";
	const char *c, *rows = refresh ? btt_devrows_xml_refresh : btt_devrows_xml;
	int head, n = (refresh ? (int)sizeof(btt_devrows_xml_refresh) : (int)sizeof(btt_devrows_xml)) - 1;

	if (!btt_devrows_is_menu(s, size))
		return BTT_DR_NOMATCH;
	if (BTT_DR_HAS(s, size, "id=\"btt_lk_codec\""))
		return BTT_DR_DONE;
	c = btt_dr_rfind(s, size, close_tag, (int)sizeof(close_tag) - 1);
	if (!c)
		return BTT_DR_NOCLOSE;
	if (!dst || cap < size + n || (dst < s + size && s < dst + cap))
		return BTT_DR_NOROOM;
	head = (int)(c - s);
	btt_dr_copy(dst, s, head);
	btt_dr_copy(dst + head, rows, n);
	btt_dr_copy(dst + head + n, s + head, size - head);
	return size + n;
}

static inline int btt_devrows_splice(const char *s, int size, char *dst, int cap)
{
	return btt_devrows_splice_x(s, size, dst, cap, 0);
}

/* The BttPageSkip reason to report for a splice result: only the two failures that mean a page of the right device was not given its rows
 * (no closing tag, no room); every other result is no failure (another page, no device selected, not the link's device, rows already there) and is silent. */
static inline int btt_devrows_skip_reason(int r)
{
	return r == BTT_DR_NOCLOSE ? BTT_SKIP_DEVROWS_CLOSE : r == BTT_DR_NOROOM ? BTT_SKIP_DEVROWS_ROOM : 0;
}

static inline const char *btt_devrows_reason(int r)
{
	switch (r) {
	case BTT_DR_NOMATCH:   return "not the device page";
	case BTT_DR_NOCLOSE:   return "no closing setting_list";
	case BTT_DR_NOROOM:    return "no room in the buffer";
	case BTT_DR_DONE:      return "rows already there";
	case BTT_DR_NOSEL:     return "no device selected";
	case BTT_DR_NOTLINK:   return "not the device holding the audio link";
	case BTT_DR_OLDKERNEL: return "the kernel module is older (no device address)";
	}
	return r > 0 ? "spliced" : "unknown";
}

/* ---- which device ---------------------------------------------------------------------------------- */

static inline int btt_dr_hexval(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* A device row's id is its address printed "%012llx": exactly 12 hex digits, the high 16 bits first. Returns 1 and the words (hi in the low
 * 16 bits of *hi), or 0 for any other id (the page's buttons, other rows). For example the id 556611223344 is the address whose high word is 5566 and low word 11223344. */
static inline int btt_devrows_parse_id(const char *id, uint32_t *lo, uint32_t *hi)
{
	uint64_t v = 0;

	if (!id)
		return 0;
	for (int i = 0; i < 12; i++) {
		int d = btt_dr_hexval(id[i]);

		if (d < 0)
			return 0;
		v = (v << 4) | (uint64_t)d;
	}
	if (id[12])
		return 0;
	*lo = (uint32_t)v;
	*hi = (uint32_t)(v >> 32);
	return 1;
}

/* The version 2 fields are there: the caller's copy was filled that far. */
#define BTT_DR_LINK_V2_SIZE ((unsigned)(offsetof(BttLinkInfo, addr_hi) + sizeof(unsigned int)))
#define BTT_DR_LINK_V3_SIZE ((unsigned)(offsetof(BttLinkInfo, codec_mask) + sizeof(unsigned int)))
#define BTT_DR_LINK_V4_SIZE ((unsigned)(offsetof(BttLinkInfo, voice_profile) + sizeof(unsigned int)))   /* version 4 ends with the voice profile */

/* 1 when the link (as bttGetLinkInfo filled `li`, zeroed before the call) is with the device whose id was selected; otherwise 0 and why
 * in *why (BTT_DR_OLDKERNEL, BTT_DR_NOTLINK). */
static inline int btt_devrows_is_link_device(uint32_t lo, uint32_t hi, const BttLinkInfo *li, int *why)
{
	if (li->size < BTT_DR_LINK_V2_SIZE) {
		*why = BTT_DR_OLDKERNEL;
		return 0;
	}
	if (!li->connected || li->codec == BTT_LINK_CODEC_NONE || (!li->addr_lo && !li->addr_hi) ||
	    li->addr_lo != lo || (li->addr_hi & 0xFFFFu) != (hi & 0xFFFFu)) {
		*why = BTT_DR_NOTLINK;
		return 0;
	}
	return 1;
}

/* ---- the values ------------------------------------------------------------------------------------ */

/* What the page shows, taken when it is opened, with each row's value already as text (rendered once: the page's value function then only
 * copies it). */
#define BTT_DR_VALMAX 704                  /* the codec list is the longest: every codec it can name, each with a note, in the longest language (671 bytes) */
typedef struct BttDevSnap {
	unsigned codec, kbps, bitpool, chmode, rate, latency;
	unsigned hc_mask, hc_installed;     /* the codecs the headphones list (BTT_HC_*), and which of them the installed add-ons send (SBC always) */
	int battery;                       /* percent, or -1 when the headset has not reported one (or Read Headset Battery is off) */
	int sig_have;                      /* the kernel module gives the signal fields (version 4); an older one leaves the Signal row's value empty */
	unsigned signal;                   /* BTT_LINK_SIGNAL_*; NONE (with sig_have) is "Measuring…" */
	unsigned voice;                    /* BTT_LINK_VOICE_*: the hands-free or headset profile connected next to the sound link (0 from an older kernel module) */
	char val[BTT_DR_COUNT][BTT_DR_VALMAX];
} BttDevSnap;

typedef const char *(*BttDevStr)(int btt_i18n_id);   /* the text of a translation, in the system language */

typedef struct { char *p; int n, cap; } BttDrW;

static inline void btt_dr_w_init(BttDrW *w, char *p, int cap) { w->p = p; w->n = 0; w->cap = cap; if (cap > 0) p[0] = 0; }
static inline void btt_dr_w_ch(BttDrW *w, char c) { if (w->n < w->cap - 1) w->p[w->n++] = c; if (w->cap > 0) w->p[w->n] = 0; }
static inline void btt_dr_w_str(BttDrW *w, const char *s) { while (s && *s) btt_dr_w_ch(w, *s++); }
static inline void btt_dr_w_dec(BttDrW *w, unsigned v)
{
	char t[11];
	int k = 0;

	do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 10);
	while (k)
		btt_dr_w_ch(w, t[--k]);
}

/* A template with {n} and {m} where the figures go (the translations keep the language's own order and spacing). */
static inline void btt_dr_w_tmpl(BttDrW *w, const char *t, const char *n, const char *m)
{
	for (; t && *t; t++) {
		if (t[0] == '{' && (t[1] == 'n' || t[1] == 'm') && t[2] == '}') {
			btt_dr_w_str(w, t[1] == 'n' ? n : m);
			t += 2;
		} else {
			btt_dr_w_ch(w, *t);
		}
	}
}

static inline const char *btt_dr_dec(unsigned v, char *buf)   /* buf: 11 bytes */
{
	BttDrW w;

	btt_dr_w_init(&w, buf, 11);
	btt_dr_w_dec(&w, v);
	return buf;
}

/* The row (BTT_DR_*) for an id: the row's own (btt_lk_<name>) or its title's (btt_lt_<name>); -1 for any other id. */
static inline int btt_devrows_row(const char *id)
{
	static const char *const name[BTT_DR_COUNT] = { "codec", "rate", "latency", "battery", "hcodecs", "signal" };

	if (!id || !(btt_dr_pre(id, "btt_lk_") || btt_dr_pre(id, "btt_lt_")))
		return -1;
	id += 7;                                /* both prefixes are 7 characters */
	for (int i = 0; i < BTT_DR_COUNT; i++)
		if (btt_dr_eq(id, name[i]))
			return i;
	return -1;
}

static inline int btt_devrows_is_value_id(const char *id) { return btt_dr_pre(id ? id : "", "btt_lk_") && btt_devrows_row(id) >= 0; }

/* ---- the Refresh Codecs button ------------------------------------------------------------------------------------------------ */

/* The button's own id (what the page plugin's button handler is given), its title's and its help's (what the text hook is asked). */
#define BTT_DR_REFRESH_ID     "btt_lk_refresh"
#define BTT_DR_REFRESH_TITLE  "btt_lt_refresh"
#define BTT_DR_REFRESH_HINT   "btt_lh_refresh"

static inline int btt_devrows_is_refresh_id(const char *id) { return id && btt_dr_eq(id, BTT_DR_REFRESH_ID); }

/* 1 when the button belongs on the page for this link: the kernel module gives a codec list at all (version 3) and has none for these headphones. An older
 * kernel module could not act on the press, so it gets no button. */
static inline int btt_devrows_wants_refresh(const BttLinkInfo *li)
{
	return li->size >= BTT_DR_LINK_V3_SIZE && li->codec_mask == 0;
}

/* The press: 1 when the id is the button's and the request was handed to the kernel module (`ask` is bttSet(BTT_KEY_REFRESH_CODECS, 1)), 0 when the id is
 * not the button's (the caller passes the call on) and -1 when it is and the kernel module declined (busy, or still in its start-up period): the press is
 * then dropped, nothing else to do. A press is consumed whether or not it was accepted. */
static inline int btt_devrows_press(const char *id, int (*ask)(void))
{
	if (!btt_devrows_is_refresh_id(id))
		return 0;
	return ask && ask() == BTT_OK ? 1 : -1;
}

/* The text hook's answers for the button's ids (the translation to use), or -1 for any other id. */
static inline int btt_devrows_button_text(const char *id)
{
	if (!id)
		return -1;
	if (btt_dr_eq(id, BTT_DR_REFRESH_TITLE))
		return BTT_I18N_ROW_DV_REFRESH;
	if (btt_dr_eq(id, BTT_DR_REFRESH_HINT))
		return BTT_I18N_HINT_DV_REFRESH;
	return -1;
}

/* ---- the Headset Codecs row ---------------------------------------------------------------------------------------------------- */

/* The codecs Bluetooth Turbo can send when its add-on is installed (SBC is the console's own). Any other codec the headphones list is named "(unsupported)". */
#define BTT_DR_HC_ADDON_BITS (BTT_HC_AAC | BTT_HC_APTX | BTT_HC_APTX_HD | BTT_HC_LDAC)
#define BTT_DR_HC_NAMES 24

/* By bit, the codecs' own names (not translated); NULL for the one bit that is not a codec (BTT_HC_OTHER, said in the language's own word). */
static const char *const btt_dr_hc_name[BTT_DR_HC_NAMES] = {
	"SBC", "AAC", "aptX", "aptX HD", "LDAC", "aptX Adaptive", "aptX Low Latency", "LHDC", "LHDC LL", "Samsung Scalable", "Samsung HD", "LC3plus", "Opus", "MP3", "ATRAC",
	0, "LHDC v1", "LHDC v2", "LHDC v3", "LHDC v4", "LLAC", "LHDC v5", "FastStream", "aptX TWS",
};

/* The order the row lists them in, by bit index, so the codecs read in families whatever their bit: the ones Bluetooth Turbo can send first (SBC, AAC, aptX, aptX HD,
 * LDAC), then MP3, ATRAC, the other aptX family members, FastStream, LHDC (v1 to v5, LL, LLAC, the one with no version), LC3plus, Opus, Samsung's two. "Other" is last. */
#define BTT_DR_HC_SHOWN 23
static const unsigned char btt_dr_hc_order[BTT_DR_HC_SHOWN] = {
	0, 1, 2, 3, 4,                  /* SBC, AAC, aptX, aptX HD, LDAC */
	13, 14,                         /* MP3, ATRAC */
	5, 6, 23, 22,                   /* aptX Adaptive, aptX Low Latency, aptX TWS, FastStream */
	16, 17, 18, 19, 21, 8, 20, 7,   /* LHDC v1, v2, v3, v4, v5, LL, LLAC, LHDC */
	11, 12,                         /* LC3plus, Opus */
	9, 10,                          /* Samsung Scalable, Samsung HD */
};

/* The bits of the codecs whose add-ons are installed and usable now, from what the kernel module says of each (BTT_KEY_*_AVAIL, the bits the Codec row's own
 * choices follow): SBC always; AAC, aptX and LDAC when their module is loaded, its start-up self-test passed and it is not held off; aptX HD also needs the HD
 * encoder's self-test. A module that is loaded but not usable (yet, or held) is "(add-on available)" like one that is not there: the console cannot send it. */
static inline unsigned btt_devrows_hc_installed(int aac_avail, int aptx_avail, int ldac_avail)
{
	unsigned m = BTT_HC_SBC;

	if (aac_avail > 0 && (aac_avail & BTT_AAC_AVAIL_MODULE) && (aac_avail & BTT_AAC_AVAIL_TESTED) && !(aac_avail & BTT_AAC_AVAIL_HELD))
		m |= BTT_HC_AAC;
	if (aptx_avail > 0 && (aptx_avail & BTT_APTX_AVAIL_MODULE) && (aptx_avail & BTT_APTX_AVAIL_TESTED) && !(aptx_avail & BTT_APTX_AVAIL_HELD)) {
		m |= BTT_HC_APTX;
		if (aptx_avail & BTT_APTX_AVAIL_HD)
			m |= BTT_HC_APTX_HD;
	}
	if (ldac_avail > 0 && (ldac_avail & BTT_LDAC_AVAIL_MODULE) && (ldac_avail & BTT_LDAC_AVAIL_TESTED) && !(ldac_avail & BTT_LDAC_AVAIL_HELD))
		m |= BTT_HC_LDAC;
	return m;
}

/* The row's text: the headphones' codecs in a fixed order (SBC, AAC, aptX, ...), separated as the language does; an installed codec by its name, one an
 * add-on could add as "<name> (add-on available)", any other named one as "<name> (unsupported)", and "Other" for numbers nobody recognises. "Not known yet"
 * when the console has not read the headphones' list (it does when it opens the stream; the headphones opening it themselves are not asked). */
static inline void btt_dr_hc_text(BttDrW *w, unsigned mask, unsigned installed, BttDevStr str)
{
	int first = 1;

	if (!mask) {
		btt_dr_w_str(w, str(BTT_I18N_DV_HC_UNKNOWN));
		return;
	}
	for (int k = 0; k < BTT_DR_HC_SHOWN; k++) {
		int i = btt_dr_hc_order[k];
		unsigned bit = 1u << i;

		if (!(mask & bit))
			continue;
		if (!first)
			btt_dr_w_str(w, str(BTT_I18N_DV_HC_SEP));
		first = 0;
		if (installed & bit)
			btt_dr_w_str(w, btt_dr_hc_name[i]);
		else
			btt_dr_w_tmpl(w, str((BTT_DR_HC_ADDON_BITS & bit) ? BTT_I18N_DV_HC_ADDON : BTT_I18N_DV_HC_UNSUPPORTED), btt_dr_hc_name[i], "");
	}
	if (mask & BTT_HC_OTHER) {
		if (!first)
			btt_dr_w_str(w, str(BTT_I18N_DV_HC_SEP));
		btt_dr_w_str(w, str(BTT_I18N_DV_HC_OTHER));
	}
}

/* The value of a row as one text, UTF-8, into out; its length. Empty when there is nothing to say. */
static inline int btt_devrows_value(const BttDevSnap *s, int row, BttDevStr str, char *out, int cap)
{
	BttDrW w;
	char a[16], b[16];

	btt_dr_w_init(&w, out, cap);
	switch (row) {
	case BTT_DR_CODEC:
		if (s->codec == BTT_LINK_CODEC_AAC) {
			if (s->kbps)
				btt_dr_w_tmpl(&w, str(BTT_I18N_DV_AAC), btt_dr_dec(s->kbps, a), "");
			else
				btt_dr_w_str(&w, str(BTT_I18N_ITEM_CODEC_AAC));
		} else if (s->codec == BTT_LINK_CODEC_APTX) {
			/* the codec stays aptX for both; the fixed rate tells them apart (384 kb/s, aptX HD 576) */
			if (s->kbps)
				btt_dr_w_tmpl(&w, str(s->kbps >= 500 ? BTT_I18N_DV_APTX_HD : BTT_I18N_DV_APTX), btt_dr_dec(s->kbps, a), "");
			else
				btt_dr_w_str(&w, str(BTT_I18N_ITEM_CODEC_APTX));
		} else if (s->codec == BTT_LINK_CODEC_LDAC) {
			if (s->kbps)
				btt_dr_w_tmpl(&w, str(BTT_I18N_DV_LDAC), btt_dr_dec(s->kbps, a), "");
			else
				btt_dr_w_str(&w, str(BTT_I18N_ITEM_CODEC_LDAC));
		} else if (s->codec == BTT_LINK_CODEC_SBC) {
			int ch = s->chmode == BTT_LINK_CH_MONO ? BTT_I18N_ITEM_MONO : s->chmode == BTT_LINK_CH_DUAL ? BTT_I18N_ITEM_DV_DUAL :
			         s->chmode == BTT_LINK_CH_STEREO ? BTT_I18N_ITEM_STEREO : s->chmode == BTT_LINK_CH_JOINT ? BTT_I18N_ITEM_DV_JOINT : -1;

			/* the details are known together or not at all (the stream is not started, or its encoder not set up yet) */
			if (s->bitpool && ch >= 0)
				btt_dr_w_tmpl(&w, str(BTT_I18N_DV_SBC), btt_dr_dec(s->bitpool, a), str(ch));
			else
				btt_dr_w_str(&w, str(BTT_I18N_ITEM_CODEC_SBC));
		}
		break;
	case BTT_DR_HCODECS:
		btt_dr_hc_text(&w, s->hc_mask, s->hc_installed, str);
		break;
	case BTT_DR_RATE:
		if (s->rate) {
			BttDrW f;
			unsigned tenth = (s->rate / 100) % 10;

			btt_dr_w_init(&f, b, sizeof(b));
			btt_dr_w_dec(&f, s->rate / 1000);
			if (tenth) {
				btt_dr_w_str(&f, str(BTT_I18N_DV_DECIMAL));
				btt_dr_w_dec(&f, tenth);
			}
			btt_dr_w_tmpl(&w, str(BTT_I18N_DV_KHZ), b, "");
		}
		break;
	case BTT_DR_LATENCY:
		btt_dr_w_str(&w, str(s->latency == BTT_LAT_LOWEST ? BTT_I18N_ITEM_LOWEST : s->latency == BTT_LAT_LOW ? BTT_I18N_ITEM_LOW : BTT_I18N_ITEM_STANDARD));
		break;
	case BTT_DR_SIGNAL:
		if (s->sig_have)
			btt_dr_w_str(&w, str(s->signal == BTT_LINK_SIGNAL_EXCELLENT ? BTT_I18N_DV_SIG_EXCELLENT : s->signal == BTT_LINK_SIGNAL_GOOD ? BTT_I18N_DV_SIG_GOOD :
			                     s->signal == BTT_LINK_SIGNAL_FAIR ? BTT_I18N_DV_SIG_FAIR : s->signal == BTT_LINK_SIGNAL_WEAK ? BTT_I18N_DV_SIG_WEAK : BTT_I18N_DV_SIG_MEASURING));
		break;
	case BTT_DR_BATTERY:
		if (s->battery >= 0)
			btt_dr_w_tmpl(&w, str(BTT_I18N_ITEM_PCT), btt_dr_dec((unsigned)s->battery, a), "");
		else
			btt_dr_w_str(&w, str(BTT_I18N_ITEM_NOTREPORTED));
		break;
	}
	return w.n;
}

/* The Headset Battery row's text when it has no level to show: "Not Connected" while no headset is connected (neither an audio link nor the
 * headset's own link, as the kernel module reports them), "Not Reported" for a connected headset that sends no level (and while Read Headset
 * Battery is off). The string id, for the row's value in the system language. */
static inline int btt_headset_none_id(int audio_connected, int headset_connected)
{
	return audio_connected || headset_connected ? BTT_I18N_ITEM_NOTREPORTED : BTT_I18N_ITEM_NOTCONNECTED;
}

/* Fills the snapshot from what bttGetLinkInfo and the headset battery say, and renders the four values in the system language. */
static inline void btt_devrows_snap(const BttLinkInfo *li, int battery, unsigned hc_installed, BttDevStr str, BttDevSnap *s)
{
	s->codec = li->codec;
	s->kbps = li->bitrate_kbps;
	s->bitpool = li->sbc_bitpool;
	s->chmode = li->sbc_channel_mode;
	s->rate = li->sample_rate;
	s->latency = li->latency;
	s->hc_mask = li->size >= BTT_DR_LINK_V3_SIZE ? li->codec_mask : 0;   /* an older kernel module has no list: not known */
	s->hc_installed = hc_installed;
	s->battery = battery >= 0 && battery <= 100 ? battery : -1;
	s->sig_have = li->size >= BTT_DR_LINK_V4_SIZE;   /* an older kernel module has no radio readings: the row has no value */
	s->signal = s->sig_have ? li->signal_level : BTT_LINK_SIGNAL_NONE;
	s->voice = s->sig_have ? li->voice_profile : BTT_LINK_VOICE_NONE;
	for (int row = 0; row < BTT_DR_COUNT; row++)
		btt_devrows_value(s, row, str, s->val[row], BTT_DR_VALMAX);
}

/* A row's title, UTF-8. With `oneline` the title is the whole line, "Codec: AAC, 256 kb/s" (the value goes nowhere else). */
static inline int btt_devrows_title(const BttDevSnap *s, int row, int oneline, BttDevStr str, char *out, int cap)
{
	static const int name[BTT_DR_COUNT] = { BTT_I18N_ROW_DV_CODEC, BTT_I18N_ROW_DV_RATE, BTT_I18N_ROW_LATENCY, BTT_I18N_ROW_HEADSET, BTT_I18N_ROW_DV_HCODECS, BTT_I18N_ROW_DV_SIGNAL };
	BttDrW w;

	btt_dr_w_init(&w, out, cap);
	if (row < 0 || row >= BTT_DR_COUNT)
		return 0;
	if (oneline && s && s->val[row][0])
		btt_dr_w_tmpl(&w, str(BTT_I18N_DV_LINE), str(name[row]), s->val[row]);   /* the language's own separator */
	else
		btt_dr_w_str(&w, str(name[row]));
	return w.n;
}

/* ---- the page plugin's "value of a label" function, wrapped --------------------------------------------- */

/* The system's string object inside the structure the page plugin fills: at +0x18, {char *data; int len}. */
#define BTT_DR_OUT_STRING 0x18

typedef struct BttPafString { char *data; int len; } BttPafString;
typedef void *(*BttDevAlloc)(unsigned bytes);          /* the system's allocator, the one the string objects free with */

/* Stores text as the page plugin's own callback does: a NUL-terminated copy in a fresh allocation, its length beside it. The string the
 * object held before is not freed here: a fresh structure holds the empty string's static data (which must never be freed), and one that
 * already holds text is not told apart from it by anything but the length, so the worst case is a few bytes not given back. 1 when stored. */
static inline int btt_devrows_set_string(void *out, const char *utf8, BttDevAlloc alloc)
{
	BttPafString *p;
	int n = btt_dr_len(utf8);
	char *d;

	if (!out || !alloc || n <= 0)
		return 0;
	p = (BttPafString *)((char *)out + BTT_DR_OUT_STRING);
	d = (char *)alloc((unsigned)n + 1);
	if (!d)
		return 0;
	btt_dr_copy(d, utf8, n);
	d[n] = 0;
	p->data = d;
	p->len = n;
	return 1;
}

/* ---- Sony's Profiles row: the voice profile Bluetooth Turbo opens ---------------------------------------------------------------- */

#define BTT_DR_PROFILE_ID "id_bt_profile"   /* Sony's row of the profiles ("A2DP,AVRCP": names separated by a comma and no space) */

static inline int btt_dr_contains(const char *h, int hn, const char *n)
{
	return btt_dr_find(h, hn, n, btt_dr_len(n)) != 0;
}

/* After the original wrote the Profiles value, adds ",HFP" (the hands-free link is up) or ",HSP" (only the console's own headset link is) to the text, in the same
 * string object, with a copy made by the system's allocator; the old text is not freed (as for the rows' own values: telling Sony's own allocation from a
 * static one is not safe). Nothing is added when there is no such link, when the text is empty, when it already names HFP or HSP (a second call on a structure the
 * original did not rewrite, or a profile list that says it), or when the allocation fails. 1 when added. */
static inline int btt_devrows_add_profile(void *out, unsigned voice, BttDevAlloc alloc)
{
	BttPafString *p;
	const char *add = voice == BTT_LINK_VOICE_HFP ? ",HFP" : voice == BTT_LINK_VOICE_HSP ? ",HSP" : 0;
	char *d;
	int n;

	if (!out || !alloc || !add)
		return 0;
	p = (BttPafString *)((char *)out + BTT_DR_OUT_STRING);
	n = p->len;
	if (!p->data || n <= 0 || n > 4096 || p->data[n] != 0 || btt_dr_contains(p->data, n, "HFP") || btt_dr_contains(p->data, n, "HSP"))
		return 0;
	d = (char *)alloc((unsigned)n + 5);
	if (!d)
		return 0;
	btt_dr_copy(d, p->data, n);
	btt_dr_copy(d + n, add, 4);
	d[n + 4] = 0;
	p->data = d;
	p->len = n + 4;
	return 1;
}

/* What the wrapper does for one call (id, out): the original runs first, for every id and with its own result; then, for a row of ours and
 * only when there is a snapshot (`snap`, null otherwise), its text for the row is stored (nothing for an empty one); and for Sony's Profiles row, on the page of the
 * device the snapshot is for, the voice profile is added to the text the original wrote. */
static inline int btt_devrows_value_hook(int (*orig)(const char *, void *), const char *id, void *out, const BttDevSnap *snap, BttDevAlloc alloc)
{
	int r = orig ? orig(id, out) : 0;

	if (snap && id && out && btt_devrows_is_value_id(id))
		btt_devrows_set_string(out, snap->val[btt_devrows_row(id)], alloc);
	else if (snap && id && out && btt_dr_eq(id, BTT_DR_PROFILE_ID))
		btt_devrows_add_profile(out, snap->voice, alloc);
	return r;
}

#endif /* BLUETOOTHTURBO_DEVROWS_H */
