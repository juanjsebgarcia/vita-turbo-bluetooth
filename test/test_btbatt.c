/*
 * test_btbatt.c - host test of the headset battery call (kernel/btbatt.c, compiled into this file as it is), with
 * the console's user-memory copies replaced by fakes.
 *
 *   make test      (cc -Itest/host test/test_btbatt.c, then run)
 *
 * What is proved:
 *   - the call says "unknown" while nothing has been stored, whatever the time (level -1, source none, age 0, not connected), and
 *     writes the version and the size it wrote;
 *   - a level is given with its source and its age in milliseconds, a level outside 0..100 is refused, a newer level replaces
 *     it, the link state is given as noted, a level is kept ten minutes after the link goes down and is unknown after that, a
 *     link that comes back keeps it, reset forgets everything;
 *   - the size rule: no buffer, an unreadable buffer or a size under 8 is refused and nothing is written; a caller
 *     with a smaller structure than the kernel's (8, 12 and 20 bytes: the call must never write past what it asked
 *     for) and one with a bigger structure (the bytes past ours stay as the caller left them) both work.
 */
#define BTT_HOST_TEST 1
#include <btt_host.h>
#include <stdlib.h>

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

static uint64_t g_now_us;
uint64_t ksceKernelGetSystemTimeWide(void) { return g_now_us; }
static int g_user_ok = 1;
int ksceKernelMemcpyUserToKernel(void *dst, const void *src, size_t len) { if (!g_user_ok) return -1; memcpy(dst, src, len); return 0; }
int ksceKernelMemcpyKernelToUser(void *dst, const void *src, size_t len) { if (!g_user_ok) return -1; memcpy(dst, src, len); return 0; }

#include "../kernel/btbatt.c"

static BttHeadsetBattery call(uint32_t size, int *ret)
{
	BttHeadsetBattery b;

	memset(&b, 0xA5, sizeof(b));
	b.size = size;
	*ret = bttGetHeadsetBattery(&b);
	return b;
}

/* the 32-bit division by 1000 against the real one, on the edges and on a spread of values */
static void test_us_to_ms(void)
{
	uint64_t x = 88172645463325252ull;     /* a fixed seed: the same run every time */
	static const uint64_t edge[] = { 0, 1, 999, 1000, 1001, 65535, 65536, 999999, 1000000, 4294967295ull, 4294967296ull, 4294967999ull,
	                                 4294968000ull, 1000ull * 4294967294ull - 1, 1000ull * 4294967294ull, 1000ull * 4294967294ull + 999,
	                                 1000ull * 4294967295ull, 1000ull << 32, (1000ull << 32) - 1, ~0ull };

	for (unsigned i = 0; i < sizeof(edge) / sizeof(edge[0]); i++) {
		uint64_t want = edge[i] / 1000;

		CHECK(us_to_ms(edge[i]) == (want > 4294967294ull ? 4294967294u : (uint32_t)want), "edge %llu: %u", (unsigned long long)edge[i], us_to_ms(edge[i]));
	}
	for (int i = 0; i < 200000; i++) {
		uint64_t want, v;

		x ^= x << 13; x ^= x >> 7; x ^= x << 17;
		v = x >> (i % 40);                 /* every magnitude from 24 to 64 bits */
		want = v / 1000;
		CHECK(us_to_ms(v) == (want > 4294967294ull ? 4294967294u : (uint32_t)want), "value %llu: %u", (unsigned long long)v, us_to_ms(v));
	}
}

static void test_unknown(void)
{
	int r;
	BttHeadsetBattery b = call(sizeof(b), &r);

	CHECK(r == BTT_OK, "the call works: %d", r);
	CHECK(b.size == sizeof(b) && b.version == BTT_BATT_VERSION, "size %u version %u", b.size, b.version);
	CHECK(b.level == BTT_BATT_UNKNOWN && b.source == BTT_BATT_SRC_NONE && b.age_ms == 0 && b.connected == 0,
	      "unknown: level %d source %u age %u connected %u", b.level, b.source, b.age_ms, b.connected);
	g_now_us = 7ull * 3600 * 1000000;
	b = call(sizeof(b), &r);
	CHECK(b.level == BTT_BATT_UNKNOWN && b.source == BTT_BATT_SRC_NONE, "still unknown later");
}

static void test_sizes(void)
{
	int r;
	uint8_t big[64];
	BttHeadsetBattery b = call(sizeof(b), &r);

	CHECK(bttGetHeadsetBattery(NULL) == BTT_ERR_ARG, "no buffer");
	b = call(4, &r);
	CHECK(r == BTT_ERR_ARG && b.version == 0xA5A5A5A5u, "a size under 8 is refused and nothing is written");
	b = call(0, &r);
	CHECK(r == BTT_ERR_ARG, "size 0 refused");
	g_user_ok = 0;
	b = call(sizeof(b), &r);
	CHECK(r == BTT_ERR_ARG, "an unreadable buffer is refused");
	g_user_ok = 1;
	/* smaller structures: only what was asked for is written */
	for (uint32_t n = 8; n <= 20; n += 4) {
		memset(big, 0xA5, sizeof(big));
		memcpy(big, &n, 4);
		CHECK(bttGetHeadsetBattery((BttHeadsetBattery *)(void *)big) == BTT_OK, "size %u", n);
		CHECK(!memcmp(big, &n, 4), "size field now %u written", n);
		for (uint32_t i = n; i < sizeof(big); i++)
			CHECK(big[i] == 0xA5, "byte %u written past the %u asked for", i, n);
		CHECK(*(uint32_t *)(void *)(big + 4) == BTT_BATT_VERSION, "version");
	}
	/* a bigger structure: ours is filled, the rest is as the caller left it */
	memset(big, 0xA5, sizeof(big));
	*(uint32_t *)(void *)big = 64;
	CHECK(bttGetHeadsetBattery((BttHeadsetBattery *)(void *)big) == BTT_OK, "a bigger structure");
	CHECK(*(uint32_t *)(void *)big == sizeof(BttHeadsetBattery), "size says what was written (%u)", *(uint32_t *)(void *)big);
	for (uint32_t i = sizeof(BttHeadsetBattery); i < sizeof(big); i++)
		CHECK(big[i] == 0xA5, "byte %u past our structure changed", i);
}

/* A level is given with its source and its age in milliseconds, a level outside 0..100 is refused, a
 * newer level replaces it, the link state is given as noted, a level is kept for ten minutes after the link goes down (exactly
 * BTT_BATT_KEEP_S, with the flag cleared) and is unknown after that, a link that comes back keeps it, reset forgets everything,
 * an age is clamped, never wrapped, and a reader that finds the writer in the middle of a change (an odd sequence word)
 * tries again and says unknown if it never gets a whole copy. */
static void test_stored_level(void)
{
	int r;
	BttHeadsetBattery b;

	btt_batt_reset();
	g_now_us = 100ull * 1000000;
	btt_batt_report(70, BTT_BATT_SRC_HFIND, g_now_us - 5000000);
	btt_batt_link(1, g_now_us);
	b = call(sizeof(b), &r);
	CHECK(b.level == 70 && b.source == BTT_BATT_SRC_HFIND && b.age_ms == 5000 && b.connected == 1,
	      "level %d source %u age %u connected %u", b.level, b.source, b.age_ms, b.connected);
	btt_batt_report(101, BTT_BATT_SRC_HFIND, g_now_us);
	btt_batt_report(-1, BTT_BATT_SRC_HFIND, g_now_us);
	b = call(sizeof(b), &r);
	CHECK(b.level == 70, "a level outside 0..100 changes nothing (%d)", b.level);
	g_now_us += 2000000;
	btt_batt_report(40, BTT_BATT_SRC_VENDOR, g_now_us - 1000000);
	b = call(sizeof(b), &r);
	CHECK(b.level == 40 && b.source == BTT_BATT_SRC_VENDOR && b.age_ms == 1000, "a newer level replaces it");
	btt_batt_report(0, BTT_BATT_SRC_VENDOR, g_now_us);
	b = call(sizeof(b), &r);
	CHECK(b.level == 0 && b.age_ms == 0, "0 percent is a level");

	/* the link goes down: the level stays, with the flag cleared, for exactly ten minutes */
	btt_batt_link(0, g_now_us);
	b = call(sizeof(b), &r);
	CHECK(b.level == 0 && b.connected == 0, "kept with the link down");
	g_now_us += (uint64_t)BTT_BATT_KEEP_S * 1000000;
	b = call(sizeof(b), &r);
	CHECK(b.level == 0, "kept at exactly %d s", BTT_BATT_KEEP_S);
	g_now_us += 1;
	b = call(sizeof(b), &r);
	CHECK(b.level == BTT_BATT_UNKNOWN && b.source == BTT_BATT_SRC_NONE && b.age_ms == 0 && b.connected == 0, "unknown after it");
	/* a link that comes back inside the ten minutes keeps the level */
	btt_batt_report(55, BTT_BATT_SRC_HFIND, g_now_us);
	btt_batt_link(1, g_now_us);
	g_now_us += 100;
	btt_batt_link(0, g_now_us);
	g_now_us += 300ull * 1000000;
	btt_batt_link(1, g_now_us);
	g_now_us += (uint64_t)BTT_BATT_KEEP_S * 1000000 * 2;
	b = call(sizeof(b), &r);
	CHECK(b.level == 55 && b.connected == 1, "a connected headset's level is not aged out");

	/* an age that does not fit 32 bits is clamped */
	btt_batt_reset();
	g_now_us = 90ull * 24 * 3600 * 1000000;
	btt_batt_report(10, BTT_BATT_SRC_HFIND, 1);
	b = call(sizeof(b), &r);
	CHECK(b.level == 10 && b.age_ms == 4294967294u, "age clamped (%u)", b.age_ms);

	/* reset forgets everything */
	btt_batt_reset();
	b = call(sizeof(b), &r);
	CHECK(b.level == BTT_BATT_UNKNOWN && b.connected == 0, "reset");

	/* a writer in the middle of a change: the reader gives up after its tries and says unknown, never a mixed value */
	btt_batt_report(33, BTT_BATT_SRC_HFIND, g_now_us);
	g_seq++;                          /* odd: writer inside */
	g_level = 99;
	b = call(sizeof(b), &r);
	CHECK(r == BTT_OK && b.level == BTT_BATT_UNKNOWN && b.source == BTT_BATT_SRC_NONE, "a reader never blocks and never mixes (%d)", b.level);
	g_seq++;
	g_level = 33;
	b = call(sizeof(b), &r);
	CHECK(b.level == 33, "whole again");
}

int main(void)
{
	test_us_to_ms();
	test_unknown();
	test_sizes();
	test_stored_level();
	printf("test_btbatt: ok\n");
	return 0;
}
