/*
 * test_btfall.c - host test of the codec fallback notification's rules and texts (kernel/btfall_core.h, compiled as it is, with the real text table):
 *   - "the headphones do not support it": the chosen codec is offered, the headphones' codec list is known and does not hold it, the link is SBC. Not when
 *     the list is not known, holds the codec, the link is not SBC or is not there, the master switch is off, the key is SBC, or the add-on cannot be used
 *     (then SBC is what the console offers); aptX counts aptX HD only while HD is preferred;
 *   - "the headphones could not use it": the stream code reports a refusal (the configuration rejected, the START rejected until the retries gave up, the stream
 *     never started) with the headphones' address: one notice, naming aptX HD as such;
 *   - once: for each headset and codec until the console restarts, whichever way it was told; again only after the Bluetooth Codec setting changes; another headset,
 *     or another codec, is told as well; a full table writes over the oldest pair;
 *   - the queue: first in, first out, four deep, a fifth is dropped and counted, an empty queue gives nothing;
 *   - the texts: in all 20 languages, both kinds, every codec, the codec's name put in, at most 63 UTF-16 units (the system's limit for a notification), no
 *     placeholder left, English as written in tools/gen_i18n_codec.py, an unknown language reads English, a buffer too small is refused.
 *
 *   make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/btfall_core.h"

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); exit(1); } } while (0)

#define ADDR_A_LO 0x11223344u
#define ADDR_A_HI 0x0000AABBu
#define ADDR_B_LO 0x55667788u
#define ADDR_B_HI 0x0000CCDDu

/* a connected headset A whose list lacks everything but SBC, the Bluetooth Codec key at `codec` and the add-on usable */
static BtFallIn lacking(int codec)
{
	BtFallIn in;

	memset(&in, 0, sizeof(in));
	in.on = 1;
	in.setting = codec;
	in.offered = codec;
	in.hd_pref = 1;
	in.connected = 1;
	in.addr_lo = ADDR_A_LO;
	in.addr_hi = ADDR_A_HI;
	in.codec_mask = BTT_HC_SBC;
	in.link_codec = BTT_LINK_CODEC_SBC;
	return in;
}

static int popn(BtFall *s, BtFallNotice *n)
{
	return btfall_pop(s, n);
}

static void t_unsupported(void)
{
	BtFall s;
	BtFallIn in;
	BtFallNotice n;

	btfall_init(&s);
	in = lacking(BTT_CODEC_AAC);
	CHECK(btfall_step(&s, &in) == 1, "AAC chosen, a list known without it: told");
	CHECK(popn(&s, &n) && n.kind == BTFALL_K_UNSUPPORTED && n.codec == BTFALL_C_AAC, "the headphones do not support AAC");
	CHECK(!popn(&s, &n), "one notice");
	for (int i = 0; i < 5; i++)
		CHECK(btfall_step(&s, &in) == 0, "every pass after it is quiet");
	/* the same on the other codecs, each its own notice */
	in = lacking(BTT_CODEC_LDAC);
	CHECK(btfall_step(&s, &in) == 1 && popn(&s, &n) && n.kind == BTFALL_K_UNSUPPORTED && n.codec == BTFALL_C_LDAC, "LDAC (the setting changed, so the count started again)");
	in = lacking(BTT_CODEC_APTX);
	CHECK(btfall_step(&s, &in) == 1 && popn(&s, &n) && n.codec == BTFALL_C_APTX, "aptX");
	/* the setting changed back: told again, because the count starts again with every change of the setting */
	in = lacking(BTT_CODEC_AAC);
	CHECK(btfall_step(&s, &in) == 1 && popn(&s, &n) && n.codec == BTFALL_C_AAC, "after the setting changed, AAC is told again");
	CHECK(btfall_step(&s, &in) == 0, "and once");
	/* another headset */
	in.addr_lo = ADDR_B_LO;
	in.addr_hi = ADDR_B_HI;
	CHECK(btfall_step(&s, &in) == 1 && popn(&s, &n), "another headset is told too");
	/* back to the first: still told */
	in.addr_lo = ADDR_A_LO;
	in.addr_hi = ADDR_A_HI;
	CHECK(btfall_step(&s, &in) == 0, "the first is not told again");
}

static void t_not_told(void)
{
	static const struct { const char *what; int field; unsigned v; } c[] = {
		{ "the list is not known", 0, 0 },
		{ "the list holds AAC", 1, BTT_HC_SBC | BTT_HC_AAC },
		{ "the link is AAC", 2, BTT_LINK_CODEC_AAC },
		{ "the link is not there", 3, 0 },
		{ "Bluetooth Turbo is off", 4, 0 },
		{ "the key is SBC", 5, BTT_CODEC_SBC },
		{ "the add-on cannot be used (SBC is what is offered)", 6, BTT_CODEC_SBC },
	};

	for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
		BtFall s;
		BtFallIn in = lacking(BTT_CODEC_AAC);

		btfall_init(&s);
		switch (c[i].field) {
		case 0: in.codec_mask = 0; break;
		case 1: in.codec_mask = c[i].v; break;
		case 2: in.link_codec = (int)c[i].v; break;
		case 3: in.connected = 0; break;
		case 4: in.on = 0; break;
		case 5: in.setting = (int)c[i].v; in.offered = (int)c[i].v; break;
		case 6: in.offered = (int)c[i].v; break;
		}
		for (int k = 0; k < 4; k++)
			CHECK(btfall_step(&s, &in) == 0, "%s: nothing is told", c[i].what);
	}
	/* the codecs' own bits: only the chosen one counts */
	{
		BtFall s;
		BtFallIn in = lacking(BTT_CODEC_AAC);

		btfall_init(&s);
		in.codec_mask = BTT_HC_SBC | BTT_HC_LDAC | BTT_HC_APTX | BTT_HC_APTX_HD;
		CHECK(btfall_step(&s, &in) == 1, "AAC chosen, the list holds the others but not AAC: told");
	}
}

static void t_aptx(void)
{
	BtFall s;
	BtFallIn in;
	BtFallNotice n;

	btfall_init(&s);
	in = lacking(BTT_CODEC_APTX);
	in.codec_mask = BTT_HC_SBC | BTT_HC_APTX_HD;
	CHECK(btfall_step(&s, &in) == 0, "aptX HD preferred, the headphones list only HD: they can be given it");
	in.hd_pref = 0;
	CHECK(btfall_step(&s, &in) == 1 && popn(&s, &n) && n.kind == BTFALL_K_UNSUPPORTED && n.codec == BTFALL_C_APTX, "aptX HD not preferred (off or held): only plain aptX counts");
	btfall_init(&s);
	in.codec_mask = BTT_HC_SBC | BTT_HC_APTX;
	CHECK(btfall_step(&s, &in) == 0, "plain aptX listed");
}

static void t_refused(void)
{
	BtFall s;
	BtFallIn in;
	BtFallNotice n;

	btfall_init(&s);
	in = lacking(BTT_CODEC_AAC);
	in.codec_mask = BTT_HC_SBC | BTT_HC_AAC;       /* they list it, and still could not use it */
	in.link_codec = BTT_LINK_CODEC_AAC;
	in.fail_codec = BTFALL_C_AAC;
	in.fail_lo = ADDR_A_LO;
	in.fail_hi = ADDR_A_HI;
	CHECK(btfall_step(&s, &in) == 1 && popn(&s, &n) && n.kind == BTFALL_K_REFUSED && n.codec == BTFALL_C_AAC, "refused: told as refused");
	CHECK(btfall_step(&s, &in) == 0, "the same report again: not told again");
	in.fail_codec = 0;
	CHECK(btfall_step(&s, &in) == 0, "no report: quiet");
	/* aptX HD refused is its own pair: plain aptX for the same headset can be told too */
	in.fail_codec = BTFALL_C_APTX_HD;
	CHECK(btfall_step(&s, &in) == 1 && popn(&s, &n) && n.codec == BTFALL_C_APTX_HD && n.kind == BTFALL_K_REFUSED, "aptX HD");
	in.fail_codec = BTFALL_C_APTX;
	CHECK(btfall_step(&s, &in) == 1 && popn(&s, &n) && n.codec == BTFALL_C_APTX, "aptX, the same headset");
	/* told as refused, then the list is read and lacks the codec: once for the pair */
	btfall_init(&s);
	in = lacking(BTT_CODEC_LDAC);
	in.fail_codec = BTFALL_C_LDAC;
	in.fail_lo = ADDR_A_LO;
	in.fail_hi = ADDR_A_HI;
	CHECK(btfall_step(&s, &in) == 1, "refused and unsupported in one pass: one notice for the pair");
	/* the report's own address, not the link's: another headset */
	btfall_init(&s);
	in = lacking(BTT_CODEC_AAC);
	in.codec_mask = BTT_HC_SBC | BTT_HC_AAC;
	in.fail_codec = BTFALL_C_AAC;
	in.fail_lo = ADDR_B_LO;
	in.fail_hi = ADDR_B_HI;
	CHECK(btfall_step(&s, &in) == 1, "told for the headset of the report");
	in.fail_lo = ADDR_A_LO;
	in.fail_hi = ADDR_A_HI;
	CHECK(btfall_step(&s, &in) == 1, "and for another headset's");
	/* off: nothing, and a setting change still resets the count */
	btfall_init(&s);
	in = lacking(BTT_CODEC_AAC);
	in.on = 0;
	in.fail_codec = BTFALL_C_AAC;
	CHECK(btfall_step(&s, &in) == 0, "Bluetooth Turbo off: nothing");
}

#define B_AAC  (1u << BTFALL_C_AAC)
#define B_APTX (1u << BTFALL_C_APTX)
#define B_LDAC (1u << BTFALL_C_LDAC)

static void t_banned(void)
{
	BtFall s;
	BtFallIn in;
	BtFallNotice n;

	btfall_init(&s);
	/* AAC was refused (told), the headset is marked for AAC, the setting goes to LDAC, which the headset lists: the mark is for AAC only, so nothing is told for LDAC */
	in = lacking(BTT_CODEC_AAC);
	in.codec_mask = BTT_HC_SBC | BTT_HC_AAC | BTT_HC_LDAC;
	in.banned = B_AAC;
	in.fail_codec = BTFALL_C_AAC;
	in.fail_lo = ADDR_A_LO;
	in.fail_hi = ADDR_A_HI;
	CHECK(btfall_step(&s, &in) == 1 && popn(&s, &n) && n.kind == BTFALL_K_REFUSED && n.codec == BTFALL_C_AAC, "the refusal");
	in.fail_codec = 0;
	CHECK(btfall_step(&s, &in) == 0, "marked, link SBC, AAC already told for this headset: nothing more");
	in = lacking(BTT_CODEC_LDAC);
	in.codec_mask = BTT_HC_SBC | BTT_HC_AAC | BTT_HC_LDAC;
	in.banned = B_AAC;
	CHECK(btfall_step(&s, &in) == 0, "LDAC chosen later: a mark for AAC says nothing about LDAC (no 'could not use')");
	in.banned = B_AAC | B_APTX;
	CHECK(btfall_step(&s, &in) == 0, "marked for AAC and aptX, LDAC chosen: still nothing");
	in.banned = B_LDAC;
	CHECK(btfall_step(&s, &in) == 1 && popn(&s, &n) && n.kind == BTFALL_K_REFUSED && n.codec == BTFALL_C_LDAC, "marked for LDAC itself: told as could-not-use");
	CHECK(btfall_step(&s, &in) == 0, "once");
	btfall_init(&s);
	in.banned = 0;
	CHECK(btfall_step(&s, &in) == 0, "not marked, the list holds LDAC: nothing");
	in.banned = B_LDAC;
	in.link_codec = BTT_LINK_CODEC_LDAC;
	CHECK(btfall_step(&s, &in) == 0, "marked but the link is on LDAC: nothing");
	in.link_codec = BTT_LINK_CODEC_SBC;
	in.codec_mask = BTT_HC_SBC;
	CHECK(btfall_step(&s, &in) == 1 && popn(&s, &n) && n.kind == BTFALL_K_UNSUPPORTED, "the list lacks it: the support rule wins");
	/* aptX chosen, the headset marked for AAC and LDAC: aptX is not told as refused */
	btfall_init(&s);
	in = lacking(BTT_CODEC_APTX);
	in.codec_mask = BTT_HC_SBC | BTT_HC_AAC | BTT_HC_APTX;
	in.banned = B_AAC | B_LDAC;
	CHECK(btfall_step(&s, &in) == 0, "aptX chosen, marks for other codecs only: nothing");
	in.banned |= B_APTX;
	CHECK(btfall_step(&s, &in) == 1 && popn(&s, &n) && n.kind == BTFALL_K_REFUSED && n.codec == BTFALL_C_APTX, "marked for aptX: told");
}

static void t_table_and_queue(void)
{
	BtFall s;
	BtFallIn in = lacking(BTT_CODEC_AAC);
	BtFallNotice n;

	btfall_init(&s);
	/* 17 headsets: the 17th writes over the first, which is then told again if it returns */
	for (uint32_t i = 0; i < BTFALL_TABLE + 1u; i++) {
		in.addr_lo = 0x1000u + i;
		in.addr_hi = 0;
		btfall_step(&s, &in);
		while (popn(&s, &n))
			;
	}
	in.addr_lo = 0x1001u;
	CHECK(btfall_step(&s, &in) == 0, "the second headset is still known");
	in.addr_lo = 0x1000u;
	CHECK(btfall_step(&s, &in) == 1, "the first, written over by the 17th, is told again");
	/* the queue: four deep, FIFO */
	btfall_init(&s);
	CHECK(!popn(&s, &n), "empty");
	for (int i = 0; i < 4; i++)
		CHECK(btfall_push(&s, BTFALL_K_UNSUPPORTED, BTFALL_C_AAC + i), "push %d", i);
	CHECK(!btfall_push(&s, BTFALL_K_REFUSED, BTFALL_C_LDAC) && s.dropped == 1, "the fifth is dropped and counted");
	for (int i = 0; i < 4; i++)
		CHECK(popn(&s, &n) && n.codec == BTFALL_C_AAC + i, "first in, first out: %d", i);
	CHECK(!popn(&s, &n), "empty again");
	CHECK(btfall_push(&s, BTFALL_K_REFUSED, BTFALL_C_LDAC) && popn(&s, &n) && n.kind == BTFALL_K_REFUSED && n.codec == BTFALL_C_LDAC, "the queue goes on after a wrap-round");
	/* the counters wrap-round past the end of the array many times */
	for (int i = 0; i < 40; i++)
		CHECK(btfall_push(&s, BTFALL_K_UNSUPPORTED, BTFALL_C_AAC) && popn(&s, &n), "push and pop %d", i);
}

static int u16_len(const char *u8)
{
	int n = 0;

	for (const unsigned char *p = (const unsigned char *)u8; *p; p++)
		if ((*p & 0xC0) != 0x80)
			n += (*p >= 0xF0) ? 2 : 1;
	return n;
}

static void t_texts(void)
{
	char t[BTFALL_TEXT];

	CHECK(btfall_text(1, BTFALL_K_UNSUPPORTED, BTFALL_C_AAC, t, sizeof(t)) > 0 && !strcmp(t, "Your headphones don't support AAC. Using SBC."), "English, unsupported: %s", t);
	CHECK(btfall_text(1, BTFALL_K_REFUSED, BTFALL_C_APTX_HD, t, sizeof(t)) > 0 && !strcmp(t, "Your headphones couldn't use aptX HD. Using SBC."), "English, refused: %s", t);
	CHECK(btfall_text(18, BTFALL_K_UNSUPPORTED, BTFALL_C_LDAC, t, sizeof(t)) > 0 && !strcmp(t, "Your headphones don't support LDAC. Using SBC."), "British English");
	CHECK(btfall_text(99, BTFALL_K_UNSUPPORTED, BTFALL_C_APTX, t, sizeof(t)) > 0 && !strcmp(t, "Your headphones don't support aptX. Using SBC."), "an unknown language reads English");
	CHECK(btfall_text(-3, BTFALL_K_REFUSED, BTFALL_C_AAC, t, sizeof(t)) > 0 && !strncmp(t, "Your headphones", 15), "a negative language reads English");
	for (int l = 0; l < BTFALL_LANGS; l++)
		for (int k = 0; k < BTFALL_K_COUNT; k++)
			for (int c = BTFALL_C_AAC; c < BTFALL_C_COUNT; c++) {
				int n = btfall_text(l, k, c, t, sizeof(t));

				CHECK(n > 0 && n == (int)strlen(t), "language %d kind %d codec %d: a text", l, k, c);
				CHECK(!strchr(t, '{') && !strchr(t, '}') && !strchr(t, '%') && !strchr(t, '\n') && strstr(t, btfall_codec_name(c)), "language %d kind %d codec %d: the name in, no placeholder: %s", l, k, c, t);
				CHECK(u16_len(t) <= 63, "language %d kind %d codec %d: %d units (the system shows 63): %s", l, k, c, u16_len(t), t);
				CHECK(l == 1 || l == 18 || strcmp(t, "Your headphones couldn't use aptX HD. Using SBC.") != 0, "translated");
			}
	CHECK(btfall_text(1, BTFALL_K_UNSUPPORTED, BTFALL_C_AAC, t, 10) == -1, "a buffer too small is refused");
	CHECK(btfall_text(1, 5, BTFALL_C_AAC, t, sizeof(t)) == -1 && btfall_text(1, 0, BTFALL_C_NONE, t, sizeof(t)) == -1 && btfall_text(1, 0, 99, t, sizeof(t)) == -1, "unknown kind or codec");
	CHECK(btfall_text(1, 0, BTFALL_C_AAC, 0, 20) == -1, "no buffer");
}

typedef struct { const char *name; void (*fn)(void); } Scn;
static const Scn k_scn[] = {
	{ "the headphones do not support the codec: once for each headset and codec, again after the setting changes", t_unsupported },
	{ "when nothing is told", t_not_told },
	{ "aptX and aptX HD", t_aptx },
	{ "the headphones could not use the codec (a refusal reported by the stream code)", t_refused },
	{ "a headset marked after a refusal, a later choice of another codec", t_banned },
	{ "the table of pairs, the queue", t_table_and_queue },
	{ "the texts in 20 languages", t_texts },
};

int main(void)
{
	for (unsigned i = 0; i < sizeof(k_scn) / sizeof(k_scn[0]); i++) {
		k_scn[i].fn();
		printf("  ok    %s\n", k_scn[i].name);
	}
	printf("test_btfall: all %u scenarios passed\n", (unsigned)(sizeof(k_scn) / sizeof(k_scn[0])));
	return 0;
}
