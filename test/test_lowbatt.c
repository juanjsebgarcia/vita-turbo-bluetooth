/*
 * test_lowbatt.c - host test of the Home screen module's pure parts: when a low battery is announced (user/lowbatt.c) and the
 * notification block (user/notice.c) and when the codec notice is asked for (user/lowbatt.c), both compiled into this file as they are.
 *
 *   make test      (cc test/test_lowbatt.c, then run)
 *
 * What is proved:
 *   - 20 %, 10 % and 5 % are each announced once per drain, at the level the headset reports (a headset that steps by 10 % gives 20,
 *     10, 0; one that steps by 1 % gives 20, 10, 5); a level that skips thresholds is one notice, and never again for those it passed;
 *   - a threshold is armed again when the level rises above it (charging); a link that is down arms them all (a new connection);
 *   - nothing is announced while the level is unknown or the headset is not connected, or while the switch is off, and the
 *     thresholds passed while it was off are not announced when it is turned on;
 *   - the codec notice is asked for on every 2 s poll while headphones are connected, and only on the 30 s battery tick otherwise;
 *   - the block: zeroed to 0x470 bytes, the text as UTF-16 with the number in place of {n}, a launch word that is empty, the two last
 *     words zero; text over 63 units, empty, bad UTF-8 and control characters are refused (and leave the block zeroed); every
 *     language's text of the table builds for every level 0..100.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../user/lowbatt.c"
#include "../user/notice.c"
#include "../user/bluetoothturbo_notice_table.h"

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

static void test_thresholds(void)
{
	LowBatt s = { 0 };

	CHECK(lowbatt_step(&s, 100, 1, 1) == -1 && lowbatt_step(&s, 21, 1, 1) == -1, "nothing above 20");
	CHECK(lowbatt_step(&s, 20, 1, 1) == 20, "20 announced");
	CHECK(lowbatt_step(&s, 20, 1, 1) == -1 && lowbatt_step(&s, 19, 1, 1) == -1 && lowbatt_step(&s, 11, 1, 1) == -1, "once");
	CHECK(lowbatt_step(&s, 10, 1, 1) == 10, "10 announced");
	CHECK(lowbatt_step(&s, 10, 1, 1) == -1 && lowbatt_step(&s, 6, 1, 1) == -1, "once");
	CHECK(lowbatt_step(&s, 5, 1, 1) == 5, "5 announced");
	CHECK(lowbatt_step(&s, 5, 1, 1) == -1 && lowbatt_step(&s, 0, 1, 1) == -1, "nothing more");
	/* a headset that steps by 10 % */
	memset(&s, 0, sizeof(s));
	CHECK(lowbatt_step(&s, 30, 1, 1) == -1 && lowbatt_step(&s, 20, 1, 1) == 20 && lowbatt_step(&s, 10, 1, 1) == 10 &&
	      lowbatt_step(&s, 0, 1, 1) == 0, "10%% steps: 20, 10, 0");
	/* skipping thresholds is one notice at the level reported; the thresholds passed are spent */
	memset(&s, 0, sizeof(s));
	CHECK(lowbatt_step(&s, 50, 1, 1) == -1 && lowbatt_step(&s, 8, 1, 1) == 8, "50 to 8: one notice");
	CHECK(lowbatt_step(&s, 7, 1, 1) == -1 && lowbatt_step(&s, 5, 1, 1) == 5, "then the 5 %% one");
	/* the first look at a connection that is already low */
	memset(&s, 0, sizeof(s));
	CHECK(lowbatt_step(&s, 4, 1, 1) == 4 && lowbatt_step(&s, 4, 1, 1) == -1, "a new connection at 4%%: once");
}

static void test_rearm(void)
{
	LowBatt s = { 0 };

	lowbatt_step(&s, 20, 1, 1);
	lowbatt_step(&s, 10, 1, 1);
	lowbatt_step(&s, 5, 1, 1);
	/* charging: above 5 re-arms 5 only, above 10 re-arms 10, above 20 re-arms all */
	CHECK(lowbatt_step(&s, 8, 1, 1) == -1, "8 %% after 5: the 5 %% threshold is armed, nothing to say yet");
	CHECK(lowbatt_step(&s, 5, 1, 1) == 5, "5 again after a rise");
	CHECK(lowbatt_step(&s, 12, 1, 1) == -1 && lowbatt_step(&s, 10, 1, 1) == 10, "10 again after a rise above it");
	CHECK(lowbatt_step(&s, 60, 1, 1) == -1 && lowbatt_step(&s, 20, 1, 1) == 20, "20 again after a charge");
	/* a link that is down arms all of them */
	lowbatt_step(&s, 5, 1, 1);
	CHECK(lowbatt_step(&s, -1, 0, 1) == -1 && s.fired == 0, "link down: armed");
	CHECK(lowbatt_step(&s, 5, 1, 1) == 5, "a new connection at 5%%: announced again");
}

static void test_nothing(void)
{
	LowBatt s = { 0 };

	CHECK(lowbatt_step(&s, -1, 1, 1) == -1 && lowbatt_step(&s, 3, 0, 1) == -1 && lowbatt_step(&s, 101, 1, 1) == -1, "unknown, not connected, bad level");
	CHECK(s.fired == 0, "nothing tracked");
	/* switch off: tracked, not announced */
	CHECK(lowbatt_step(&s, 15, 1, 0) == -1 && lowbatt_step(&s, 4, 1, 0) == -1, "off: silent");
	CHECK(lowbatt_step(&s, 4, 1, 1) == -1, "on at 4%%: what was passed while off is not announced");
	CHECK(lowbatt_step(&s, 70, 1, 1) == -1 && lowbatt_step(&s, 20, 1, 1) == 20, "the next drain is");
	/* connected, nothing reported yet, then a level: unchanged state in between */
	memset(&s, 0, sizeof(s));
	lowbatt_step(&s, 10, 1, 1);
	CHECK(lowbatt_step(&s, -1, 1, 1) == -1 && lowbatt_step(&s, 10, 1, 1) == -1, "an unknown level while connected forgets nothing");
}

static void units(const uint8_t *b, uint16_t *u, int n)
{
	for (int i = 0; i < n; i++)
		u[i] = (uint16_t)(b[2 * i] | (b[2 * i + 1] << 8));
}

static void test_notice_due(void)
{
	unsigned n = 0;

	for (unsigned t = 0; t < 45; t++)
		n += lowbatt_notice_due(t, 15, 0) ? 1u : 0u;
	CHECK(n == 3 && lowbatt_notice_due(0, 15, 0) && lowbatt_notice_due(15, 15, 0) && lowbatt_notice_due(30, 15, 0) && !lowbatt_notice_due(1, 15, 0) && !lowbatt_notice_due(14, 15, 0),
	      "with no headphones connected the codec notice is asked for on the battery's tick only (every 30 s), got %u in 45 polls", n);
	for (unsigned t = 0; t < 45; t++)
		CHECK(lowbatt_notice_due(t, 15, 1), "while connected it is asked for on every poll (tick %u)", t);
}

static void test_block(void)
{
	static uint8_t blk[NOTICE_BLOCK_SIZE];
	uint16_t u[80];
	int n;

	memset(blk, 0xA5, sizeof(blk));
	n = notice_build(blk, "Headset battery at {n}%", 20);
	CHECK(n == 22, "units %d", n);
	units(blk, u, n + 1);
	CHECK(u[0] == 'H' && u[n - 1] == '%' && u[n - 4] == ' ' && u[n - 3] == '2' && u[n - 2] == '0' && u[n] == 0, "text");
	for (unsigned i = 2 * (unsigned)(n + 1); i < NOTICE_BLOCK_SIZE; i++)
		CHECK(blk[i] == 0, "byte %u past the text is not zero", i);   /* includes the launch word at +0x80 and the words at +0x468 */
	CHECK(blk[0x80] == 0 && *(uint32_t *)(void *)(blk + 0x468) == 0 && *(uint32_t *)(void *)(blk + 0x46C) == 0, "launch word and last words");
	CHECK(notice_build(blk, "{n}", 5) == 1 && blk[0] == '5', "one digit");
	CHECK(notice_build(blk, "{n}", 100) == 3 && blk[0] == '1' && blk[4] == '0', "three digits");
	CHECK(notice_build(blk, "\xe3\x81\x82{n}", 7) == 2 && blk[0] == 0x42 && blk[1] == 0x30 && blk[2] == '7', "a three-byte character");
	CHECK(notice_build(blk, "caf\xc3\xa9", 1) == 4 && blk[6] == 0xE9 && blk[7] == 0, "a two-byte character");
	/* refused, block left zeroed */
	memset(blk, 0xA5, sizeof(blk));
	CHECK(notice_build(blk, "", 5) == -1 && blk[0] == 0 && blk[NOTICE_BLOCK_SIZE - 1] == 0, "empty");
	CHECK(notice_build(blk, "a\nb", 5) == -1 && blk[0] == 0, "control character");
	CHECK(notice_build(blk, "bad \xff byte", 5) == -1, "not UTF-8");
	CHECK(notice_build(blk, "\xf0\x9f\x98\x80", 5) == -1, "beyond the 16 bits");
	CHECK(notice_build(blk, "x{n}", -1) == -1 && notice_build(blk, "x{n}", 101) == -1, "level out of range");
	CHECK(notice_build(blk, NULL, 5) == -1, "no text");
	{
		char t[80];

		memset(t, 'x', 63);
		t[63] = 0;
		CHECK(notice_build(blk, t, 5) == 63, "63 units fit");
		t[63] = 'x';
		t[64] = 0;
		CHECK(notice_build(blk, t, 5) == -1 && blk[0] == 0, "64 units do not");
		CHECK(notice_build(blk, "{n}{n}{n}{n}{n}{n}{n}{n}{n}{n}{n}{n}{n}{n}{n}{n}{n}{n}{n}{n}{n}{n}x", 100) == -1, "the number counts too");
	}
}

static void test_table(void)
{
	static uint8_t blk[NOTICE_BLOCK_SIZE];

	CHECK(sizeof(g_notice) / sizeof(g_notice[0]) == 20, "twenty languages");
	for (int l = 0; l < 20; l++) {
		const char *t = g_notice[l];

		CHECK(t && *t && strstr(t, "{n}") && !strstr(strstr(t, "{n}") + 3, "{n}"), "language %d: {n} once in '%s'", l, t);
		for (int lv = 0; lv <= 100; lv++)
			CHECK(notice_build(blk, t, lv) > 0, "language %d level %d does not build", l, lv);
	}
	CHECK(!strcmp(g_notice[1], "Headset battery at {n}%") && !strcmp(g_notice[18], g_notice[1]), "English (US and UK)");
	CHECK(!strcmp(g_notice[2], "Batterie du micro-casque \xc3\xa0 {n} %") && !strcmp(g_notice[19], "Kulakl\xc4\xb1k pili %{n}"),
	      "number styles: a space before the percent sign in French, the sign first in Turkish");
}

int main(void)
{
	test_thresholds();
	test_rearm();
	test_nothing();
	test_notice_due();
	test_block();
	test_table();
	printf("test_lowbatt: ok\n");
	return 0;
}
