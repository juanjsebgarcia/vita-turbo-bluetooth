/*
 * test_btcodecs.c - host test of the headphones' codec lists (kernel/btcodecs.h, kernel/btcodecs.c, and btlink_core.h's use of them), compiled as they are.
 * The reading from signalling replies by the hook is in test/test_btaac_stream.c ("codec lists"); this is what the lists are made of and where they are kept.
 *
 *   make test      (cc -Itest/host test/test_btcodecs.c, then run)
 *
 * What is proved:
 *   - every codec number the table names (and the LHDC feature bytes that tell its third generation's v3, v4 and LLAC apart), by its vendor and codec numbers, and that a number nobody recognises is "other" (a vendor above 16 bits, a known
 *     vendor's unknown codec, a codec type nobody knows);
 *   - the codec bit of an endpoint's capabilities, from the bytes the reference headset sent, and what is refused: another media type, no codec element, a
 *     list running past its length, a vendor codec without its number; the scan never reads past its length (a guard page after the bytes);
 *   - the store: put, get, the same device again takes its slot, 16 devices fit, the 17th takes the one written longest ago, a mask of 0 is not kept, the
 *     address's high word is 16 bits;
 *   - the file: it reads back exactly (order kept, so the eviction order survives), and every damaged file is an empty store (magic, version, count, length,
 *     checksum, a mask of 0, a device twice, a high word over 16 bits);
 *   - the worker: the file is read once, a finished list is kept and written once, the same list again writes nothing, a changed list writes, a file that cannot be written is logged and the list is still kept in memory;
 *   - what bttGetLinkInfo says: the list of the device the link is with, 0 when nothing is known, nothing when not connected.
 */
#define BTT_HOST_TEST 1
#include <btt_host.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

/* ---- fakes: a file system of one file, and the log ---- */
static uint8_t g_file[1024];
static int g_file_len = -1, g_open_fail, g_writes, g_reads;
static SceUID g_mode;
#include <stdarg.h>
static char g_log[8192];
static void log_add(const char *fmt, ...)
{
	va_list ap;
	size_t n = strlen(g_log);

	va_start(ap, fmt);
	vsnprintf(g_log + n, sizeof(g_log) - n, fmt, ap);
	va_end(ap);
	strncat(g_log, "\n", sizeof(g_log) - strlen(g_log) - 1);
}
void btt_klog_line(const char *line) { log_add("%s", line); }
SceUID ksceIoOpen(const char *path, int flags, int mode)
{
	CHECK(!strcmp(path, "ur0:data/bluetoothturbo/codecs.bin"), "the file's path");
	if (flags & SCE_O_WRONLY) {
		if (g_open_fail)
			return -1;
		g_mode = 2;
		g_file_len = 0;
		return 5;
	}
	if (g_file_len < 0)
		return -1;
	g_mode = 1;
	g_reads++;
	return 6;
}
int ksceIoRead(SceUID fd, void *buf, SceSize n) { CHECK(fd == 6, "read fd"); SceSize k = (SceSize)g_file_len < n ? (SceSize)g_file_len : n; memcpy(buf, g_file, k); return (int)k; }
int ksceIoWrite(SceUID fd, const void *buf, SceSize n) { CHECK(fd == 5 && g_file_len + (int)n <= (int)sizeof(g_file), "write fd"); memcpy(g_file + g_file_len, buf, n); g_file_len += (int)n; return (int)n; }
int ksceIoClose(SceUID fd) { if (fd == 5) g_writes++; return 0; }
int ksceIoMkdir(const char *path, int mode) { return 0; }

#include "../kernel/btaac_hook.h"
volatile BtAacNeg btaac_neg;
volatile BtCdcRec btaac_cdc;
#include "../kernel/btcodecs.c"
#include "../kernel/btlink_core.h"

static int log_has_list(void) { return strstr(g_log, "list: codecs 0x0003") != NULL; }

static void t_classify(void)
{
	/* the mapping of vendor and codec numbers (BlueZ, PipeWire and BlueALSA tables); LHDC 0x4C33 from its numbers alone is "LHDC", its version not known */
	static const struct { uint32_t type, vendor, codec, bit; } t[] = {
		{ 0, 0, 0, BTT_HC_SBC }, { 1, 0, 0, BTT_HC_MP3 }, { 2, 0, 0, BTT_HC_AAC }, { 4, 0, 0, BTT_HC_ATRAC },
		{ 0xFF, 0x4F, 1, BTT_HC_APTX }, { 0xFF, 0xD7, 0x24, BTT_HC_APTX_HD }, { 0xFF, 0xD7, 0xAD, BTT_HC_APTX_ADAPTIVE },
		{ 0xFF, 0x0A, 2, BTT_HC_APTX_LL }, { 0xFF, 0xD7, 2, BTT_HC_APTX_LL }, { 0xFF, 0x0A, 1, BTT_HC_FASTSTREAM }, { 0xFF, 0xD7, 0x25, BTT_HC_APTX_TWS },
		{ 0xFF, 0x12D, 0xAA, BTT_HC_LDAC },
		{ 0xFF, 0x53A, 0x484C, BTT_HC_LHDC_V1 }, { 0xFF, 0x53A, 0x4C32, BTT_HC_LHDC_V2 }, { 0xFF, 0x53A, 0x4C33, BTT_HC_LHDC }, { 0xFF, 0x53A, 0x4C35, BTT_HC_LHDC_V5 },
		{ 0xFF, 0x53A, 0x4C4C, BTT_HC_LHDC_LL },
		{ 0xFF, 0x75, 0x0103, BTT_HC_SAMSUNG_SCALABLE }, { 0xFF, 0x75, 0x0102, BTT_HC_SAMSUNG_HD },
		{ 0xFF, 0x8A9, 1, BTT_HC_LC3PLUS }, { 0xFF, 0xE0, 1, BTT_HC_OPUS }, { 0xFF, 0x5F1, 0x1005, BTT_HC_OPUS },
		/* nobody recognises these: another number of a known vendor (a Samsung number nobody has named, LHDC's absent 0x4C34, aptX Adaptive under the older aptX number,
		 * the old guess of LLAC under the 0x4C4C...), vendors whose codecs only earbud chip software is known to carry (low confidence: never named), a vendor above 16 bits, another codec type */
		{ 0xFF, 0x4F, 2, BTT_HC_OTHER }, { 0xFF, 0x53A, 1, BTT_HC_OTHER }, { 0xFF, 0x53A, 0x4C34, BTT_HC_OTHER }, { 0xFF, 0x12D, 1, BTT_HC_OTHER },
		{ 0xFF, 0x0A, 0x00AD, BTT_HC_OTHER }, { 0xFF, 0x0A, 3, BTT_HC_OTHER }, { 0xFF, 0xD7, 1, BTT_HC_OTHER },
		{ 0xFF, 0x75, 0x0001, BTT_HC_OTHER }, { 0xFF, 0x75, 0x0101, BTT_HC_OTHER }, { 0xFF, 0x75, 0x0104, BTT_HC_OTHER }, { 0xFF, 0x75, 0, BTT_HC_OTHER },
		{ 0xFF, 0xCCF, 0xCA01, BTT_HC_OTHER }, { 0xFF, 0x38F, 0x4D31, BTT_HC_OTHER }, { 0xFF, 0x38F, 0x8FAA, BTT_HC_OTHER },
		{ 0xFF, 0x8A9, 2, BTT_HC_OTHER }, { 0xFF, 0xE0, 2, BTT_HC_OTHER }, { 0xFF, 0x5F1, 1, BTT_HC_OTHER }, { 0xFF, 0x1234, 1, BTT_HC_OTHER },
		{ 0xFF, 0x1004F, 1, BTT_HC_OTHER }, { 3, 0, 0, BTT_HC_OTHER }, { 5, 0, 0, BTT_HC_OTHER }, { 0xFE, 0x4F, 1, BTT_HC_OTHER },
	};
	uint32_t seen = 0;

	for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		CHECK(btcdc_classify(t[i].type, t[i].vendor, t[i].codec) == t[i].bit, "case %u: 0x%X/0x%X/0x%X gives 0x%X, expected 0x%X", i, t[i].type, t[i].vendor, t[i].codec,
		      btcdc_classify(t[i].type, t[i].vendor, t[i].codec), t[i].bit);
		seen |= t[i].bit;
	}
	/* LHDC's third generation by its feature bytes (the rate byte, then P7 and P8): none set is v3, LLAC is the high bit of the first, v4 of the second, both set names both */
	{
		static const struct { uint8_t ci[3]; uint32_t bit; } l[] = {
			{ { 0x01, 0x01, 0x01 }, BTT_HC_LHDC_V3 }, { { 0x01, 0x7F, 0x7F }, BTT_HC_LHDC_V3 }, { { 0x01, 0x81, 0x01 }, BTT_HC_LHDC_LLAC }, { { 0x01, 0x01, 0x81 }, BTT_HC_LHDC_V4 },
			{ { 0xFF, 0xFF, 0xFF }, BTT_HC_LHDC_LLAC | BTT_HC_LHDC_V4 }, { { 0, 0x80, 0 }, BTT_HC_LHDC_LLAC }, { { 0, 0, 0x80 }, BTT_HC_LHDC_V4 },
		};

		for (unsigned i = 0; i < sizeof(l) / sizeof(l[0]); i++) {
			uint32_t got = btcdc_classify_caps(0xFF, 0x53A, 0x4C33, l[i].ci, 3);

			CHECK(got == l[i].bit, "LHDC feature bytes %02X %02X %02X: 0x%X, expected 0x%X", l[i].ci[0], l[i].ci[1], l[i].ci[2], got, l[i].bit);
			seen |= got;
		}
		/* the feature bytes of any other number are not read; a short element (fewer than the three bytes) is "LHDC" */
		CHECK(btcdc_classify_caps(0xFF, 0x53A, 0x4C35, l[2].ci, 3) == BTT_HC_LHDC_V5 && btcdc_classify_caps(0xFF, 0x53A, 0x4C4C, l[3].ci, 3) == BTT_HC_LHDC_LL,
		      "other LHDC numbers ignore the feature bytes");
		CHECK(btcdc_classify_caps(0xFF, 0x53A, 0x4C33, l[2].ci, 2) == BTT_HC_LHDC && btcdc_classify_caps(0xFF, 0x53A, 0x4C33, l[2].ci, 0) == BTT_HC_LHDC &&
		      btcdc_classify_caps(0xFF, 0x53A, 0x4C33, 0, 0) == BTT_HC_LHDC, "a short element: LHDC");
	}
	CHECK(seen == 0xFFFFFFu, "every bit of the list is reachable (0x%X)", seen);
	for (unsigned b = 0; b < 24; b++)
		CHECK(__builtin_popcount(1u << b) == 1, "one bit each");
}

static const uint8_t k_sbc[] = { 0x01, 0x00, 0x07, 0x06, 0x00, 0x00, 0x3F, 0xFF, 0x02, 0x35, 0x04, 0x02, 0x02, 0x00 };
static const uint8_t k_aac[] = { 0x01, 0x00, 0x07, 0x08, 0x00, 0x02, 0xC0, 0x01, 0x8C, 0x84, 0x09, 0xB6, 0x04, 0x02, 0x02, 0x00 };
static const uint8_t k_aptx[] = { 0x01, 0x00, 0x07, 0x09, 0x00, 0xFF, 0x4F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x32, 0x04, 0x02, 0x02, 0x00 };
static const uint8_t k_adaptive[] = { 0x01, 0x00, 0x07, 0x0F, 0x00, 0xFF, 0xD7, 0x00, 0x00, 0x00, 0xAD, 0x00, 0x91, 0x0A, 0x53, 0x53, 0xFA, 0x7D, 0x64 };
static const uint8_t k_ldac[] = { 0x01, 0x00, 0x07, 0x0A, 0x00, 0xFF, 0x2D, 0x01, 0x00, 0x00, 0xAA, 0x00, 0x3F, 0x07 };
/* LHDC v3 elements as capabilities (11 bytes: media, codec type, vendor 0x053A, codec 0x4C33, rates, P7, P8) and the same with the feature bits */
static const uint8_t k_lhdc3[] = { 0x01, 0x00, 0x07, 0x0B, 0x00, 0xFF, 0x3A, 0x05, 0x00, 0x00, 0x33, 0x4C, 0x3C, 0x01, 0x01 };
static const uint8_t k_lhdc3_llac[] = { 0x01, 0x00, 0x07, 0x0B, 0x00, 0xFF, 0x3A, 0x05, 0x00, 0x00, 0x33, 0x4C, 0x3C, 0x81, 0x01 };
static const uint8_t k_lhdc3_v4[] = { 0x01, 0x00, 0x07, 0x0B, 0x00, 0xFF, 0x3A, 0x05, 0x00, 0x00, 0x33, 0x4C, 0x3C, 0x02, 0x81 };
static const uint8_t k_lhdc3_short[] = { 0x01, 0x00, 0x07, 0x09, 0x00, 0xFF, 0x3A, 0x05, 0x00, 0x00, 0x33, 0x4C, 0x3C };   /* no feature bytes */
static const uint8_t k_lhdc5[] = { 0x01, 0x00, 0x07, 0x0B, 0x00, 0xFF, 0x3A, 0x05, 0x00, 0x00, 0x35, 0x4C, 0x3C, 0x81, 0x81 };
static const uint8_t k_samsung_sc[] = { 0x01, 0x00, 0x07, 0x0A, 0x00, 0xFF, 0x75, 0x00, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00 };
static const uint8_t k_samsung_other[] = { 0x01, 0x00, 0x07, 0x0A, 0x00, 0xFF, 0x75, 0x00, 0x00, 0x00, 0x07, 0x01, 0x00, 0x00 };
static const uint8_t k_video[] = { 0x01, 0x00, 0x07, 0x06, 0x10, 0x00, 0x3F, 0xFF, 0x02, 0x35 };

/* the bytes are put at the end of a page and the page after is protected: a scan that reads one byte past its length dies */
static void scan_guarded(const uint8_t *bytes, uint32_t n, uint32_t want, const char *what)
{
	long ps = sysconf(_SC_PAGESIZE);
	uint8_t *pg = mmap(NULL, (size_t)ps * 2, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

	CHECK(pg != MAP_FAILED, "mmap");
	CHECK(mprotect(pg + ps, (size_t)ps, PROT_NONE) == 0, "guard page");
	memcpy(pg + ps - n, bytes, n);
	CHECK(btcdc_scan_caps(pg + ps - n, n) == want, "%s: 0x%X, expected 0x%X", what, btcdc_scan_caps(pg + ps - n, n), want);
	munmap(pg, (size_t)ps * 2);
}

static void t_scan(void)
{
	scan_guarded(k_sbc, sizeof(k_sbc), BTT_HC_SBC, "SBC");
	scan_guarded(k_aac, sizeof(k_aac), BTT_HC_AAC, "AAC");
	scan_guarded(k_aptx, sizeof(k_aptx), BTT_HC_APTX, "aptX");
	scan_guarded(k_adaptive, sizeof(k_adaptive), BTT_HC_APTX_ADAPTIVE, "aptX Adaptive");
	scan_guarded(k_ldac, sizeof(k_ldac), BTT_HC_LDAC, "LDAC");
	scan_guarded(k_lhdc3, sizeof(k_lhdc3), BTT_HC_LHDC_V3, "LHDC v3");
	scan_guarded(k_lhdc3_llac, sizeof(k_lhdc3_llac), BTT_HC_LHDC_LLAC, "LHDC LLAC by its feature bit");
	scan_guarded(k_lhdc3_v4, sizeof(k_lhdc3_v4), BTT_HC_LHDC_V4, "LHDC v4 by its feature bit");
	scan_guarded(k_lhdc3_short, sizeof(k_lhdc3_short), BTT_HC_LHDC, "LHDC with no feature bytes");
	scan_guarded(k_lhdc5, sizeof(k_lhdc5), BTT_HC_LHDC_V5, "LHDC v5 ignores the feature bits");
	scan_guarded(k_samsung_sc, sizeof(k_samsung_sc), BTT_HC_SAMSUNG_SCALABLE, "Samsung Scalable");
	scan_guarded(k_samsung_other, sizeof(k_samsung_other), BTT_HC_OTHER, "a Samsung number nobody has named");
	scan_guarded(k_video, sizeof(k_video), 0, "another media type");
	scan_guarded(k_sbc, 0, 0, "nothing");
	scan_guarded(k_sbc, 1, 0, "one byte");
	scan_guarded(k_sbc, 2, 0, "transport only, empty");
	/* every truncation of every real element: never read past it, never a codec from half an element */
	const uint8_t *all[] = { k_sbc, k_aac, k_aptx, k_adaptive, k_ldac, k_lhdc3_llac, k_lhdc3_v4 };

	for (int e = 0; e < 7; e++)
		for (uint32_t n = 0; n < 4u + all[e][3]; n++)   /* the transport element (2) and the codec element (2 + its length) */
			scan_guarded(all[e], n, 0, "a list cut inside the codec element holds no codec");
	/* a vendor codec without its number: other; one with a short number field: other */
	{
		static const uint8_t v[] = { 0x01, 0x00, 0x07, 0x05, 0x00, 0xFF, 0x4F, 0x00, 0x00 };

		scan_guarded(v, sizeof(v), BTT_HC_OTHER, "a vendor codec with no number");
	}
	/* an element of another category first; the codec element after it */
	{
		static const uint8_t c[] = { 0x04, 0x02, 0x02, 0x00, 0x07, 0x06, 0x00, 0x00, 0x3F, 0xFF, 0x02, 0x35 };

		scan_guarded(c, sizeof(c), BTT_HC_SBC, "content protection first");
	}
	/* a codec element too short to name a codec */
	{
		static const uint8_t c[] = { 0x07, 0x01, 0x00 };

		scan_guarded(c, sizeof(c), 0, "an element too short to name a codec");
	}
	CHECK(btcdc_sep_is_audio_sink((const uint8_t[]){ 0x04, 0x08 }) && btcdc_sep_is_audio_sink((const uint8_t[]){ 0x06, 0x08 }) && !btcdc_sep_is_audio_sink((const uint8_t[]){ 0x04, 0x00 }) &&
	      !btcdc_sep_is_audio_sink((const uint8_t[]){ 0x04, 0x18 }), "audio sinks, in use or not; not sources, not video");
}

static void t_store(void)
{
	BtCdcStore s;

	btcdc_store_clear(&s);
	CHECK(btcdc_store_get(&s, 1, 2) == 0 && btcdc_store_get(&s, 0, 0) == 0, "an empty store knows nobody (not even address 0)");
	CHECK(btcdc_store_put(&s, 1, 2, 3) == 1 && btcdc_store_get(&s, 1, 2) == 3, "put, get");
	CHECK(btcdc_store_put(&s, 1, 2, 3) == 0, "the same list again changes nothing");
	CHECK(btcdc_store_put(&s, 1, 2, 7) == 1 && btcdc_store_get(&s, 1, 2) == 7, "a changed list is kept");
	CHECK(btcdc_store_put(&s, 9, 9, 0) == 0 && btcdc_store_get(&s, 9, 9) == 0, "a mask of 0 is not a list");
	CHECK(btcdc_store_put(&s, 1, 0x10002, 5) == 1 && btcdc_store_get(&s, 1, 2) == 5 && btcdc_store_get(&s, 1, 0x10002) == 5, "the high word is 16 bits");
	/* sixteen fit; the seventeenth takes the one written longest ago */
	btcdc_store_clear(&s);
	for (uint32_t i = 0; i < 16; i++)
		CHECK(btcdc_store_put(&s, 100 + i, 7, 1u << (i % 15)) == 1, "device %u", i);
	for (uint32_t i = 0; i < 16; i++)
		CHECK(btcdc_store_get(&s, 100 + i, 7) == 1u << (i % 15), "device %u kept", i);
	CHECK(btcdc_store_put(&s, 100, 7, 1u << 0) == 0, "written again (the same list): nothing changes, and it is not made newer");
	CHECK(btcdc_store_put(&s, 101, 7, 0x20) == 1, "device 1 changes: now the newest");
	CHECK(btcdc_store_put(&s, 200, 7, 0x40) == 1, "a 17th device");
	CHECK(btcdc_store_get(&s, 200, 7) == 0x40 && btcdc_store_get(&s, 100, 7) == 0 && btcdc_store_get(&s, 101, 7) == 0x20, "device 0 (the oldest written) went, device 1 (rewritten) stayed");
	for (uint32_t i = 2; i < 16; i++)
		CHECK(btcdc_store_get(&s, 100 + i, 7), "device %u still there", i);
}

static void t_file(void)
{
	BtCdcStore s, r;
	uint8_t f[BTCDC_FILE_MAX], g[BTCDC_FILE_MAX];
	uint32_t n;

	btcdc_store_clear(&s);
	n = btcdc_store_save(&s, f);
	CHECK(n == 16 && btcdc_store_load(&r, f, n) && btcdc_store_get(&r, 1, 1) == 0, "an empty store is a file of 16 bytes, and reads back empty");
	for (uint32_t i = 0; i < 16; i++)
		btcdc_store_put(&s, 0xA0000000u + i, i, 1u + i);
	btcdc_store_put(&s, 0xA0000003u, 3, 0x8001);      /* device 3 is now the newest */
	n = btcdc_store_save(&s, f);
	CHECK(n == BTCDC_FILE_MAX && n == 12 + 16 * 12 + 4, "a full store: %u bytes", n);
	CHECK(btcdc_store_load(&r, f, n), "reads back");
	for (uint32_t i = 0; i < 16; i++)
		CHECK(btcdc_store_get(&r, 0xA0000000u + i, i) == (i == 3 ? 0x8001u : 1u + i), "device %u", i);
	/* the eviction order survived: the next new device takes device 0 (the oldest), not device 3 */
	btcdc_store_put(&r, 0xB0000000u, 1, 1);
	CHECK(btcdc_store_get(&r, 0xA0000000u, 0) == 0 && btcdc_store_get(&r, 0xA0000003u, 3) == 0x8001, "the oldest goes after a reload as before");
	/* every damage is an empty store */
	struct { const char *what; uint32_t off; uint8_t xor_; } d[] = {
		{ "the magic", 0, 1 }, { "the version", 4, 1 }, { "the count (one more)", 8, 1 }, { "a device's address", 12, 0x10 }, { "a mask", 20, 1 }, { "the checksum", 12 + 16 * 12, 1 },
	};
	for (unsigned i = 0; i < sizeof(d) / sizeof(d[0]); i++) {
		memcpy(g, f, n);
		g[d[i].off] ^= d[i].xor_;
		btcdc_store_put(&r, 5, 5, 5);
		CHECK(!btcdc_store_load(&r, g, n) && btcdc_store_get(&r, 5, 5) == 0 && btcdc_store_get(&r, 0xA0000001u, 1) == 0, "damaged %s: empty", d[i].what);
	}
	/* a file of the first version (its LLAC and Samsung bits meant other codecs) is dropped even with a checksum that is right */
	memcpy(g, f, n);
	btcdc_put32(g + 4, 1);
	btcdc_put32(g + n - 4, btcdc_fnv(g, n - 4));
	CHECK(BTCDC_FILE_VER == 2 && !btcdc_store_load(&r, g, n) && btcdc_store_get(&r, 0xA0000001u, 1) == 0, "a version 1 file is discarded");
	CHECK(!btcdc_store_load(&r, f, n - 1) && !btcdc_store_load(&r, f, n + 1) && !btcdc_store_load(&r, f, 0) && !btcdc_store_load(&r, f, 15), "wrong lengths");
	{   /* a count over the limit, with a checksum that is right; a device twice; a mask of 0; a high word over 16 bits */
		uint8_t b[12 + 17 * 12 + 4];

		memset(b, 0, sizeof(b));
		btcdc_put32(b, BTCDC_MAGIC); btcdc_put32(b + 4, BTCDC_FILE_VER); btcdc_put32(b + 8, 17);
		btcdc_put32(b + 12 + 17 * 12, btcdc_fnv(b, 12 + 17 * 12));
		CHECK(!btcdc_store_load(&r, b, sizeof(b)), "17 devices");
		btcdc_store_clear(&s);
		btcdc_store_put(&s, 1, 1, 1);
		btcdc_store_put(&s, 2, 2, 1);
		n = btcdc_store_save(&s, f);
		memcpy(f + 12 + 12, f + 12, 8);                          /* the second device is the first again */
		btcdc_put32(f + n - 4, btcdc_fnv(f, n - 4));
		CHECK(!btcdc_store_load(&r, f, n), "a device twice");
		n = btcdc_store_save(&s, f);
		btcdc_put32(f + 12 + 8, 0);
		btcdc_put32(f + n - 4, btcdc_fnv(f, n - 4));
		CHECK(!btcdc_store_load(&r, f, n), "a mask of 0");
		n = btcdc_store_save(&s, f);
		btcdc_put32(f + 12 + 4, 0x10001);
		btcdc_put32(f + n - 4, btcdc_fnv(f, n - 4));
		CHECK(!btcdc_store_load(&r, f, n), "a high word over 16 bits");
	}
}

static void world(void)
{
	memset((void *)&btaac_cdc, 0, sizeof(btaac_cdc));
	g_log[0] = 0;
	g_file_len = -1;
	g_open_fail = g_writes = g_reads = 0;
	btcdc_store_clear(&g_store);
	g_loaded = 0;
	g_seen_seq = 0;
}

static void finish(uint32_t lo, uint32_t hi, uint32_t mask)
{
	btaac_cdc.done_lo = lo;
	btaac_cdc.done_hi = hi;
	btaac_cdc.done_mask = mask;
	btaac_cdc.done_seq += 2;   /* odd while written, even when done */
}

static void t_worker(void)
{
	uint8_t f[BTCDC_FILE_MAX];
	uint32_t n;

	/* no file: nothing known, nothing written until a list is finished */
	world();
	btt_codecs_pass();
	CHECK(g_reads == 0 && g_writes == 0 && btt_codecs_lookup(1, 2) == 0, "no file, nothing kept");
	finish(0x11223344u, 0x5566u, BTT_HC_SBC | BTT_HC_AAC);
	btt_codecs_pass();
	CHECK(g_writes == 1 && btt_codecs_lookup(0x11223344u, 0x5566u) == (BTT_HC_SBC | BTT_HC_AAC) && log_has_list(), "a finished list is kept and written");
	btt_codecs_pass();
	btt_codecs_pass();
	CHECK(g_writes == 1, "nothing finished since: nothing written");
	finish(0x11223344u, 0x5566u, BTT_HC_SBC | BTT_HC_AAC);
	btt_codecs_pass();
	CHECK(g_writes == 1, "the same list again: nothing written");
	finish(0x11223344u, 0x5566u, BTT_HC_SBC | BTT_HC_AAC | BTT_HC_LDAC);
	btt_codecs_pass();
	CHECK(g_writes == 2 && btt_codecs_lookup(0x11223344u, 0x5566u) == (BTT_HC_SBC | BTT_HC_AAC | BTT_HC_LDAC), "a changed list is written");
	/* what was written is read at the next start */
	n = (uint32_t)g_file_len;
	memcpy(f, g_file, n);
	world();
	memcpy(g_file, f, n);
	g_file_len = (int)n;
	btt_codecs_pass();
	CHECK(g_reads == 1 && btt_codecs_lookup(0x11223344u, 0x5566u) == (BTT_HC_SBC | BTT_HC_AAC | BTT_HC_LDAC) && btt_codecs_lookup(0x11223344u, 0x5567u) == 0, "read at the first pass");
	btt_codecs_pass();
	CHECK(g_reads == 1, "once");
	/* a file that does not hold together: empty, logged, and the next list is kept */
	world();
	memcpy(g_file, f, n);
	g_file[5] ^= 1;
	g_file_len = (int)n;
	btt_codecs_pass();
	CHECK(btt_codecs_lookup(0x11223344u, 0x5566u) == 0 && strstr(g_log, "did not hold together"), "a damaged file: empty, logged");
	/* a write that cannot be made: logged, the list is still known until the next start */
	g_open_fail = 1;
	finish(7, 8, BTT_HC_SBC);
	btt_codecs_pass();
	CHECK(g_writes == 0 && btt_codecs_lookup(7, 8) == BTT_HC_SBC && strstr(g_log, "could not be written"), "no file: logged, kept in memory");
}

static void t_link(void)
{
	BtLinkIn in;
	BttLinkInfo o;

	memset(&in, 0, sizeof(in));
	in.on = in.recognised = in.connected = 1;
	in.addr_lo = 0x11223344u;
	in.addr_hi = 0x5566u;
	in.codec_mask = BTT_HC_SBC | BTT_HC_AAC | BTT_HC_APTX | BTT_HC_APTX_ADAPTIVE;
	btlink_compose(&in, &o);
	CHECK(o.codec_mask == in.codec_mask && o.connected == 1, "the list of the device the link is with");
	in.codec_mask = 0;
	btlink_compose(&in, &o);
	CHECK(o.codec_mask == 0 && o.connected == 1, "never read: 0, which means not known");
	in.codec_mask = BTT_HC_SBC;
	in.connected = 0;
	btlink_compose(&in, &o);
	CHECK(o.codec_mask == 0, "not connected: nothing");
	in.connected = 1;
	in.on = 0;
	btlink_compose(&in, &o);
	CHECK(o.codec_mask == 0, "switched off: nothing");
	CHECK(BTT_LINK_VERSION == 4 && sizeof(BttLinkInfo) == 72 && offsetof(BttLinkInfo, codec_mask) == 52 && offsetof(BttLinkInfo, link_quality) == 56, "version 3 added the word at 52 (a 56-byte caller ends there); version 4 follows it");
}

int main(void)
{
	t_classify();
	t_scan();
	t_store();
	t_file();
	t_worker();
	t_link();
	printf("test_btcodecs: ok\n");
	return 0;
}
