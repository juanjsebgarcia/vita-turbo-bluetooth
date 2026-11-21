/*
 * test_btdisc.c - host test of what the console does when Bluetooth headphones disconnect (kernel/btdisc.c),
 * compiled into this file as it is, with taiHEN's calls replaced by fakes that really patch and unpatch a
 * stand-in for the audio policy's code.
 *
 *   make test      (cc -Itest/host test/test_btdisc.c, then run)
 *
 * What is proved, on the code's bytes themselves:
 *   - Mute (the default) writes nothing and does not even look at the system, however often it is asked;
 *   - Pause, Keep Volume writes exactly the four bytes `0A E0 00 BF` (a Thumb branch, then a no-op) at the
 *     mute's skip site, and Play on Speaker the four bytes `AF F3 00 80` (a 32-bit no-op) at the branch to the
 *     "audio was lost" code; each once, however many passes follow; the branch's target is the notification test
 *     the stock code has 0x18 bytes on, which is the point of the Pause change;
 *   - a change from one to another takes the old bytes out first, and Mute takes everything out: the code is
 *     then byte for byte the stock code (compared whole);
 *   - nothing is written unless the site and the stock bytes around it are exactly the stock code: a single
 *     byte different anywhere in either window (every byte is tried) writes nothing for that mode and is not
 *     retried on every pass, but is after the mode changes; the other mode is not affected by the other
 *     window;
 *   - Play on Speaker already made by another plugin (the same four bytes) is left alone: never written,
 *     never removed;
 *   - a missing module, a failed lookup and a refused write change nothing and are not retried on every pass;
 *   - an out of range mode is Mute; module stop takes everything out, once, and releases nothing when nothing
 *     is in.
 */
#define BTT_HOST_INJECT 1
#include <btt_host.h>
#include <stdlib.h>

#include "../kernel/btdisc.c"

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

/* ---- the stand-in for the audio policy's code ------------------------------------------- */

#define TEXT_SIZE 0x6400
static uint8_t g_text_mem[TEXT_SIZE];
static uint8_t g_pristine[TEXT_SIZE];

static void make_text(void)
{
	memset(g_text_mem, 0x5A, sizeof(g_text_mem));
	memcpy(g_text_mem + OFF_SPK_WIN, k_spk_win, sizeof(k_spk_win));
	memcpy(g_text_mem + OFF_PAUSE_WIN, k_pause_win, sizeof(k_pause_win));
	memcpy(g_pristine, g_text_mem, sizeof(g_text_mem));
}

/* ---- fake taiHEN ------------------------------------------------------------------------- */

static int g_have_module = 1, g_offset_ok = 1, g_lookups, g_fail_inject, g_injects, g_releases;
static struct { int used; uint32_t off; size_t n; uint8_t orig[8]; } g_t_inj[8];

int taiGetModuleInfoForKernel(SceUID pid, const char *name, tai_module_info_t *info)
{
	g_lookups++;
	CHECK(pid == KERNEL_PID && !strcmp(name, "SceAVConfig"), "wrong module looked up: %s", name);
	if (!g_have_module)
		return -1;
	info->modid = 0x888;
	return 0;
}
int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr)
{
	CHECK(modid == 0x888 && segidx == 0 && offset == 0, "wrong segment asked for");
	if (!g_offset_ok)
		return -1;
	*addr = (uintptr_t)g_text_mem;
	return 0;
}
SceUID taiInjectDataForKernel(SceUID pid, SceUID modid, int segidx, uint32_t offset, const void *data, size_t size)
{
	CHECK(pid == KERNEL_PID && modid == 0x888 && segidx == 0, "wrong inject target");
	CHECK(size == 4 && offset + size <= TEXT_SIZE, "inject out of range or not four bytes");
	CHECK(offset == OFF_SPK || offset == OFF_PAUSE, "an inject at 0x%x, which is neither site", (unsigned)offset);
	if (g_fail_inject)
		return -1;
	int i;
	for (i = 0; i < 8 && g_t_inj[i].used; i++) ;
	CHECK(i < 8, "too many injections");
	g_t_inj[i].used = 1;
	g_t_inj[i].off = offset;
	g_t_inj[i].n = size;
	memcpy(g_t_inj[i].orig, g_text_mem + offset, size);
	memcpy(g_text_mem + offset, data, size);
	g_injects++;
	return 0x100 + i;
}
int taiInjectReleaseForKernel(SceUID uid)
{
	int i = uid - 0x100;
	CHECK(i >= 0 && i < 8 && g_t_inj[i].used, "released something not injected (uid 0x%x)", uid);
	memcpy(g_text_mem + g_t_inj[i].off, g_t_inj[i].orig, g_t_inj[i].n);
	g_t_inj[i].used = 0;
	g_releases++;
	return 0;
}
void btt_klog_line(const char *line)
{
	if (getenv("BTT_TEST_VERBOSE"))
		printf("    %s\n", line);
}

/* ---- helpers ------------------------------------------------------------------------------- */

static void reset(void)
{
	make_text();
	g_have_module = g_offset_ok = 1;
	g_lookups = g_fail_inject = g_injects = g_releases = 0;
	memset(g_t_inj, 0, sizeof(g_t_inj));
	/* the module's own state, as at module load */
	gd_mod = -1; gd_text = 0; gd_state = -1; gd_other_spk = gd_spk_ok = gd_pause_ok = 0;
	gd_in = 0; gd_failed = 0;
	for (int i = 0; i < BTT_DISC_COUNT; i++)
		gd_inj[i] = -1;
}

static int stock(void) { return !memcmp(g_text_mem, g_pristine, sizeof(g_text_mem)); }
static int has(uint32_t off, const uint8_t *b) { return !memcmp(g_text_mem + off, b, 4); }

/* ---- tests ----------------------------------------------------------------------------------- */

static void test_mute_is_stock(void)
{
	reset();
	for (int i = 0; i < 50; i++)
		btt_btdisc_sync(BTT_DISC_MUTE);
	CHECK(g_lookups == 0 && g_injects == 0 && stock(), "Mute looked at or changed the system (%d lookups, %d injects)", g_lookups, g_injects);
	btt_btdisc_restore();
	CHECK(g_releases == 0, "a restore with nothing in released something");
	for (int bad = -3; bad < 8; bad++) {   /* out of range is Mute */
		if (bad >= 0 && bad < BTT_DISC_COUNT)
			continue;
		btt_btdisc_sync(bad);
		CHECK(g_lookups == 0 && g_injects == 0 && stock(), "mode %d was not Mute", bad);
	}
}

static void test_bytes_and_target(void)
{
	/* the two replacements, as bytes */
	CHECK(!memcmp(k_spk_new, "\xAF\xF3\x00\x80", 4), "Play on Speaker is not a 32-bit no-op");
	CHECK(!memcmp(k_pause_new, "\x0A\xE0\x00\xBF", 4), "Pause, Keep Volume is not `b` then a no-op");
	/* the Thumb `b` (encoding T2: 0xE000 | imm11): its target is the site + 4 + 2 * imm11; that must be the notification
	 * test, the instruction the stock window holds 0x18 bytes after the site (ldr.w r3,[fp,#0xec]: DB F8 EC 30) */
	uint32_t hw = (uint32_t)k_pause_new[0] | ((uint32_t)k_pause_new[1] << 8), imm11 = hw & 0x7FF;
	uint32_t target = OFF_PAUSE + 4 + 2 * imm11;
	CHECK((hw & 0xF800) == 0xE000, "not an unconditional branch: %04x", hw);
	CHECK(target == OFF_PAUSE + 0x18, "branch lands at +0x%x, not at the notification test", (unsigned)(target - OFF_PAUSE));
	CHECK(target >= OFF_PAUSE_WIN && target + 4 <= OFF_PAUSE_WIN + sizeof(k_pause_win) &&
	      !memcmp(k_pause_win + (target - OFF_PAUSE_WIN), "\xDB\xF8\xEC\x30", 4), "the branch's target is not the notification test");
	/* the second halfword is a 16-bit no-op */
	CHECK(k_pause_new[2] == 0x00 && k_pause_new[3] == 0xBF, "padding is not a no-op");
	/* the replaced bytes are the stock bytes at the sites, a conditional 32-bit branch each */
	CHECK(!memcmp(k_spk_win + (OFF_SPK - OFF_SPK_WIN), "\x40\xF0\xB2\x80", 4), "speaker site is not bne.w");
	CHECK(!memcmp(k_pause_win + (OFF_PAUSE - OFF_PAUSE_WIN), "\x3F\xF4\x47\xAE", 4), "pause site is not beq.w");
}

static void test_each_mode(void)
{
	reset();
	for (int i = 0; i < 20; i++)
		btt_btdisc_sync(BTT_DISC_PAUSE);
	CHECK(g_injects == 1 && g_lookups == 1, "Pause: %d injects, %d lookups", g_injects, g_lookups);
	CHECK(has(OFF_PAUSE, k_pause_new) && !has(OFF_SPK, k_spk_new), "Pause wrote the wrong site");
	/* only the four bytes changed */
	for (int i = 0; i < TEXT_SIZE; i++)
		if (g_text_mem[i] != g_pristine[i])
			CHECK(i >= (int)OFF_PAUSE && i < (int)OFF_PAUSE + 4, "a byte outside the site changed at 0x%x", i);

	/* straight to the other mode: the old bytes out first */
	for (int i = 0; i < 20; i++)
		btt_btdisc_sync(BTT_DISC_SPEAKER);
	CHECK(g_injects == 2 && g_releases == 1, "Speaker: %d injects, %d releases", g_injects, g_releases);
	CHECK(has(OFF_SPK, k_spk_new) && !has(OFF_PAUSE, k_pause_new), "Speaker left the Pause bytes in");
	CHECK(!memcmp(g_text_mem + OFF_PAUSE, g_pristine + OFF_PAUSE, 4), "the Pause site is not stock after Speaker");
	CHECK(g_lookups == 1, "the code was looked at again (%d)", g_lookups);

	/* and to Mute: byte for byte stock */
	btt_btdisc_sync(BTT_DISC_MUTE);
	CHECK(stock() && g_releases == 2, "Mute did not restore the stock code (%d releases)", g_releases);
	btt_btdisc_sync(BTT_DISC_MUTE);
	CHECK(g_releases == 2, "Mute released twice");

	/* module stop with a change in place */
	btt_btdisc_sync(BTT_DISC_PAUSE);
	CHECK(!stock(), "Pause not back in");
	btt_btdisc_restore();
	CHECK(stock(), "restore left bytes");
	btt_btdisc_restore();   /* nothing in: releases nothing more */
	CHECK(g_releases == g_injects, "releases %d, injects %d", g_releases, g_injects);
}

/* every byte of each window, one at a time: a different byte anywhere means no change for that mode */
static void test_windows(void)
{
	for (int mode = BTT_DISC_PAUSE; mode <= BTT_DISC_SPEAKER; mode++) {
		uint32_t win = mode == BTT_DISC_PAUSE ? OFF_PAUSE_WIN : OFF_SPK_WIN;
		size_t n = mode == BTT_DISC_PAUSE ? sizeof(k_pause_win) : sizeof(k_spk_win);

		for (size_t i = 0; i < n; i++) {
			reset();
			g_text_mem[win + i] ^= 0x01;
			memcpy(g_pristine, g_text_mem, sizeof(g_text_mem));
			for (int k = 0; k < 10; k++)
				btt_btdisc_sync(mode);
			CHECK(g_injects == 0 && stock(), "mode %d: byte %d of the window differs and a change was written", mode, (int)i);
			CHECK(g_lookups == 1, "mode %d byte %d: the code was looked at %d times in ten passes", mode, (int)i, g_lookups);
			/* the other mode is judged on its own window: it works if its own window is stock */
			int other = mode == BTT_DISC_PAUSE ? BTT_DISC_SPEAKER : BTT_DISC_PAUSE;
			btt_btdisc_sync(BTT_DISC_MUTE);
			btt_btdisc_sync(other);
			CHECK(g_injects == 1, "mode %d: the other mode (%d) was refused too (%d injects)", mode, other, g_injects);
			btt_btdisc_restore();
		}
	}
}

static void test_other_plugin_speaker(void)
{
	/* Play on Speaker already made by someone else: the same four bytes at the site */
	reset();
	memcpy(g_text_mem + OFF_SPK, k_spk_new, 4);
	memcpy(g_pristine, g_text_mem, sizeof(g_text_mem));
	for (int i = 0; i < 10; i++)
		btt_btdisc_sync(BTT_DISC_SPEAKER);
	CHECK(g_injects == 0 && has(OFF_SPK, k_spk_new), "wrote over another plugin's change (%d injects)", g_injects);
	btt_btdisc_sync(BTT_DISC_MUTE);
	btt_btdisc_restore();
	CHECK(g_releases == 0 && has(OFF_SPK, k_spk_new), "removed another plugin's change");
	/* Pause still works beside it */
	btt_btdisc_sync(BTT_DISC_PAUSE);
	CHECK(g_injects == 1 && has(OFF_PAUSE, k_pause_new), "Pause refused beside another plugin's Speaker change");
}

static void test_failures(void)
{
	/* no module */
	reset();
	g_have_module = 0;
	for (int i = 0; i < 20; i++)
		btt_btdisc_sync(BTT_DISC_SPEAKER);
	CHECK(g_injects == 0 && g_lookups == 1 && stock(), "missing module: %d injects, %d lookups", g_injects, g_lookups);
	/* the lookup of the segment fails */
	reset();
	g_offset_ok = 0;
	for (int i = 0; i < 20; i++)
		btt_btdisc_sync(BTT_DISC_PAUSE);
	CHECK(g_injects == 0 && g_lookups == 1, "no segment: %d injects, %d lookups", g_injects, g_lookups);
	/* a refused write: not retried every pass, retried after the mode changes */
	reset();
	g_fail_inject = 1;
	int tries = 0;
	for (int i = 0; i < 20; i++)
		btt_btdisc_sync(BTT_DISC_PAUSE);
	CHECK(g_injects == 0 && stock(), "a refused write left bytes");
	g_fail_inject = 0;
	for (int i = 0; i < 5; i++, tries++)
		btt_btdisc_sync(BTT_DISC_PAUSE);
	CHECK(g_injects == 0, "a refused write was retried without a change of mode");
	btt_btdisc_sync(BTT_DISC_MUTE);
	btt_btdisc_sync(BTT_DISC_PAUSE);
	CHECK(g_injects == 1 && has(OFF_PAUSE, k_pause_new), "not tried again after the mode went through Mute");
	CHECK(tries == 5, "test bookkeeping");
}

int main(void)
{
	test_mute_is_stock();
	test_bytes_and_target();
	test_each_mode();
	test_windows();
	test_other_plugin_speaker();
	test_failures();
	printf("test_btdisc: ok\n");
	return 0;
}
