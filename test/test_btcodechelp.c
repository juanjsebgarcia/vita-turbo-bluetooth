/*
 * test_btcodechelp.c - host test of the texts of the Bluetooth Codec Help page and of the list-style hints of Codec Settings, as the Settings module
 * serves them (the real translation table and the real UTF-8 -> UTF-16 decoder):
 *   - the help page: its row, six lines, each with a title short enough for one line and a hint, in all 20 languages, nothing left to substitute,
 *     English as written in tools/gen_i18n_codec.py;
 *   - the five hints (SBC Audio Quality, AAC Bit Rate, LDAC Bit Rate, aptX HD When Available, Hardware Accelerated AAC): one "X = Y" line per choice and
 *     a closing paragraph, in the order the lists show their items (SBC low to high), every language; the line breaks are real line feeds after decoding
 *     when the generator's switch is on 'newline' and there are none when it is on 'semicolon' (both forms are built and length-checked by the generator);
 *   - every text fits the decoder's buffer (the Settings module's 800 units) once decoded.
 *
 *   make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../settings_app/bluetoothturbo_i18n.h"

int sceRegMgrGetKeyInt(const char *c, const char *n, int *v) { (void)c; (void)n; (void)v; return -1; }
int sceAppUtilSystemParamGetInt(int p, int *v) { (void)p; (void)v; return -1; }
#include "../settings_app/bluetoothturbo_i18n.c"

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); exit(1); } } while (0)
#define BUF 800

static const char *txt(int id, int lang) { return btt_i18n_for(id, lang); }

static int units(const char *utf8, unsigned short *out)
{
	return btt_i18n_to_u16(utf8, out, BUF);
}

static int count_char(const unsigned short *u, int n, unsigned short c)
{
	int k = 0;

	for (int i = 0; i < n; i++)
		k += u[i] == c;
	return k;
}

static void t_help_page(void)
{
	static const int title[6] = { BTT_I18N_HELP_TITLE_1, BTT_I18N_HELP_TITLE_2, BTT_I18N_HELP_TITLE_3, BTT_I18N_HELP_TITLE_4, BTT_I18N_HELP_TITLE_5, BTT_I18N_HELP_TITLE_6 };
	static const int hint[6] = { BTT_I18N_HELP_HINT_1, BTT_I18N_HELP_HINT_2, BTT_I18N_HELP_HINT_3, BTT_I18N_HELP_HINT_4, BTT_I18N_HELP_HINT_5, BTT_I18N_HELP_HINT_6 };
	static const char *const en_title[6] = { "Check your headphones' codecs first", "SBC: Vita default", "AAC: very good quality",
	                                         "aptX\xE2\x84\xA2: lowest delay", "LDAC\xE2\x84\xA2: highest quality", "Sound breaking up? Lower the bit rate" };
	unsigned short u[BUF];

	CHECK(!strcmp(txt(BTT_I18N_ROW_CODECHELP, BTT_LANG_ENGLISH_US), "Bluetooth Codec Help"), "the row's English title");
	for (int i = 0; i < 6; i++)
		CHECK(!strcmp(txt(title[i], BTT_LANG_ENGLISH_US), en_title[i]), "line %d's English title: %s", i + 1, txt(title[i], BTT_LANG_ENGLISH_US));
	for (int l = 0; l < BTT_LANG_COUNT; l++) {
		int n = units(txt(BTT_I18N_ROW_CODECHELP, l), u);

		CHECK(n > 0 && n < 40 && (l == BTT_LANG_ENGLISH_GB || l == BTT_LANG_ENGLISH_US || strcmp(txt(BTT_I18N_ROW_CODECHELP, l), "Bluetooth Codec Help")), "language %d: the row title (%d units)", l, n);
		n = units(txt(BTT_I18N_HINT_CODECHELP, l), u);
		CHECK(n > 10 && n < 300, "language %d: the page's hint (%d units)", l, n);
		for (int i = 0; i < 6; i++) {
			const char *t = txt(title[i], l), *h = txt(hint[i], l);
			int nt = units(t, u), nh;

			/* one line on the page: Sony's rows have room for about 50 Latin letters; the longest title is 45 */
			CHECK(nt > 0 && nt <= 50, "language %d, line %d: the title is %d units: %s", l, i + 1, nt, t);
			nh = units(h, u);
			CHECK(nh > 20 && nh < BUF - 1, "language %d, line %d: the hint is %d units", l, i + 1, nh);
			CHECK(!strchr(t, '{') && !strchr(h, '{') && !strchr(h, '%') && !strchr(t, '\n'), "language %d, line %d: nothing left to substitute", l, i + 1);
		}
	}
	CHECK(strstr(txt(hint[2], BTT_LANG_ENGLISH_US), "55 ms") && strstr(txt(hint[2], BTT_LANG_ENGLISH_US), "made for phones"), "AAC: the delay, and no maker named");
	CHECK(strstr(txt(hint[0], BTT_LANG_ENGLISH_US), "Headset Codecs") && strstr(txt(hint[0], BTT_LANG_ENGLISH_US), "Bluetooth Devices"), "line 1 names the page and the row");
	CHECK(strstr(txt(hint[4], BTT_LANG_ENGLISH_US), "660 kb/s") && strstr(txt(hint[4], BTT_LANG_ENGLISH_US), "330 kb/s"), "LDAC: both bit rates");
	CHECK(strstr(txt(hint[3], BTT_LANG_ENGLISH_US), "aptX HD") && strstr(txt(hint[3], BTT_LANG_ENGLISH_US), "experimental"), "aptX: HD, experimental");
}

/* The text of a hint cut into its lines. */
static int lines_of(const char *t, const char *out[8])
{
	int n = 0;

	out[n++] = t;
	for (const char *p = t; *p; p++)
		if (*p == '\n' && n < 8)
			out[n++] = p + 1;
	return n;
}

static void t_hints(void)
{
	static const struct { int id; int choices; const char *first; } h[] = {
		{ BTT_I18N_HINT_QUALITY, 5, "Stable = about 260 kb/s" },
		{ BTT_I18N_HINT_AACRATE, 4, "128 kb/s = " },
		{ BTT_I18N_HINT_LDACRATE, 3, "330 kb/s = default" },
		{ BTT_I18N_HINT_APTXHD, 2, "On = aptX HD, 576 kb/s" },
		{ BTT_I18N_HINT_AACACCEL, 2, "On = faster encoding" },
	};
	unsigned short u[BUF];
	int newline = strchr(txt(BTT_I18N_HINT_QUALITY, BTT_LANG_ENGLISH_US), '\n') != NULL;

	for (unsigned i = 0; i < sizeof(h) / sizeof(h[0]); i++) {
		const char *en = txt(h[i].id, BTT_LANG_ENGLISH_US);

		CHECK(!strncmp(en, h[i].first, strlen(h[i].first)), "hint %u starts with its first choice: %.40s", i, en);
		for (int l = 0; l < BTT_LANG_COUNT; l++) {
			const char *t = txt(h[i].id, l), *ln[8];
			int n = units(t, u), k, breaks = count_char(u, n, 0x0A);

			CHECK(n > 40 && n < BUF - 1, "hint %u, language %d: %d units", i, l, n);
			CHECK(!strchr(t, '{') && !strchr(t, '%'), "hint %u, language %d: nothing left to substitute", i, l);
			if (newline) {
				k = lines_of(t, ln);
				CHECK(breaks == h[i].choices && k == h[i].choices + 1, "hint %u, language %d: %d line feeds, %d lines, expected %d choices and a closing paragraph", i, l, breaks, k, h[i].choices);
				for (int j = 0; j < h[i].choices; j++) {
					const char *eq = strstr(ln[j], " = "), *end = strchr(ln[j], '\n');

					CHECK(eq && eq < end, "hint %u, language %d, line %d is 'X = Y'", i, l, j + 1);
				}
				CHECK(!strstr(ln[h[i].choices], " = ") || i == 1, "hint %u, language %d: the closing paragraph has no choice in it", i, l);
			} else {
				CHECK(breaks == 0, "hint %u, language %d: the semicolon form has no line feed", i, l);
			}
		}
	}
	/* the SBC levels, low to high, as the list shows them */
	{
		const char *en = txt(BTT_I18N_HINT_QUALITY, BTT_LANG_ENGLISH_US);
		const char *p0 = strstr(en, "Stable"), *p1 = strstr(en, "Balanced"), *p2 = strstr(en, "Standard"), *p3 = strstr(en, "High"), *p4 = strstr(en, "Highest");

		CHECK(p0 && p1 && p2 && p3 && p4 && p0 < p1 && p1 < p2 && p2 < p3 && p3 < p4, "SBC levels low to high");
		CHECK(strstr(en, "670\xE2\x80\x93" "740 kb/s, depending on the headphones"), "Highest: the range, depending on the headphones");
	}
	/* a newline decodes to one line feed in the Settings module's buffer */
	{
		unsigned short v[8];

		CHECK(btt_i18n_to_u16("a = b\nc", v, 8) == 7 && v[5] == 0x0A, "a newline is kept");
	}
}

int main(void)
{
	t_help_page();
	printf("  ok    the codec help page: row, six lines, titles on one line, hints, 20 languages\n");
	t_hints();
	printf("  ok    the list-style hints: one 'X = Y' line per choice, SBC low to high, line feeds decoded\n");
	printf("test_btcodechelp: all scenarios passed\n");
	return 0;
}
