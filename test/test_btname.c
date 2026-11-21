/*
 * test_btname.c - host test of the Bluetooth name (kernel/btname.c) and of the rule for what a name may be
 * (btt_name_valid in include/bluetoothturbo.h), compiled as they are, with taiHEN's calls replaced by fakes
 * that really patch and unpatch a stand-in for the Bluetooth code's text.
 *
 *   make test      (cc -Itest/host test/test_btname.c, then run)
 *
 * What is proved:
 *   - the rule: "" and names of up to 16 printable ASCII characters (space to tilde) are valid; 17 characters,
 *     a control character, a byte above 0x7E (accented text as UTF-8, or a tab, or a newline) and NULL are not;
 *   - no name, or the empty name, writes nothing and does not even look at the system;
 *   - a name is written as one 17-byte inject at the text's place: the characters, then NULs to 17 bytes, so
 *     a 16-character name has its NUL in the last byte and a shorter one is NUL padded (both uses of the text,
 *     the local name command measuring up to the NUL and the inquiry response copying 16 bytes, then see
 *     the name); the bytes around it never change;
 *   - the same name is written once, however many passes follow; a different one takes the old text out first;
 *     the empty name (or module stop) puts the stock text back, byte for byte (compared whole);
 *   - nothing is written unless the text and the bytes around it are the stock ones: every byte of the window
 *     is tried; a mismatch is not retried every pass but a different name is tried again;
 *   - a missing module, a failed segment lookup and a refused write change nothing and are not retried
 *     every pass; a name that is not valid is never written.
 */
#define BTT_HOST_INJECT 1
#include <btt_host.h>
#include <stdlib.h>

#include "../kernel/btname.c"

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

#define TEXT_SIZE 0x21000
static uint8_t g_text_mem[TEXT_SIZE];
static uint8_t g_pristine[TEXT_SIZE];

static int g_have_module = 1, g_offset_ok = 1, g_lookups, g_fail_inject, g_injects, g_releases;
static struct { int used; uint32_t off; size_t n; uint8_t orig[32]; } g_t_inj[8];

int taiGetModuleInfoForKernel(SceUID pid, const char *name, tai_module_info_t *info)
{
	g_lookups++;
	CHECK(pid == KERNEL_PID && !strcmp(name, "SceBt"), "wrong module looked up: %s", name);
	if (!g_have_module)
		return -1;
	info->modid = 0x777;
	return 0;
}
int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr)
{
	CHECK(modid == 0x777 && segidx == 0 && offset == 0, "wrong segment asked for");
	if (!g_offset_ok)
		return -1;
	*addr = (uintptr_t)g_text_mem;
	return 0;
}
SceUID taiInjectDataForKernel(SceUID pid, SceUID modid, int segidx, uint32_t offset, const void *data, size_t size)
{
	CHECK(pid == KERNEL_PID && modid == 0x777 && segidx == 0, "wrong inject target");
	CHECK(size == 17 && offset == OFF_NAME, "inject of %d bytes at 0x%x", (int)size, (unsigned)offset);
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

static void reset(void)
{
	memset(g_text_mem, 0x5A, sizeof(g_text_mem));
	memcpy(g_text_mem + OFF_NAME_WIN, k_name_win, sizeof(k_name_win));
	memcpy(g_pristine, g_text_mem, sizeof(g_text_mem));
	g_have_module = g_offset_ok = 1;
	g_lookups = g_fail_inject = g_injects = g_releases = 0;
	memset(g_t_inj, 0, sizeof(g_t_inj));
	gn_mod = -1; gn_text = 0; gn_state = -1; gn_inj = -1; gn_failed_set = 0;
}

static int stock(void) { return !memcmp(g_text_mem, g_pristine, sizeof(g_text_mem)); }

static void test_rule(void)
{
	CHECK(btt_name_valid(""), "empty refused");
	CHECK(btt_name_valid("PlayStation Vita"), "the console's own name refused");
	CHECK(btt_name_valid("0123456789abcdef"), "16 characters refused");
	CHECK(!btt_name_valid("0123456789abcdefg"), "17 characters accepted");
	CHECK(btt_name_valid(" ~!"), "space, tilde and bang refused");
	CHECK(!btt_name_valid("a\tb") && !btt_name_valid("a\nb") && !btt_name_valid("a\x1f"), "a control character accepted");
	CHECK(!btt_name_valid("a\x7f"), "DEL accepted");
	CHECK(!btt_name_valid("caf\xc3\xa9"), "UTF-8 accented text accepted");
	CHECK(!btt_name_valid("\x80"), "a high byte accepted");
	CHECK(!btt_name_valid((const char *)0), "NULL accepted");
}

static void test_default_is_stock(void)
{
	reset();
	for (int i = 0; i < 30; i++) {
		btt_btname_sync("");
		btt_btname_sync(0);
	}
	CHECK(g_lookups == 0 && g_injects == 0 && stock(), "no name looked at or changed the system");
	btt_btname_restore();
	CHECK(g_releases == 0, "a restore with nothing in released something");
	/* names that are not valid are never written */
	btt_btname_sync("0123456789abcdefg");
	btt_btname_sync("caf\xc3\xa9");
	CHECK(g_injects == 0 && g_lookups == 0 && stock(), "an invalid name was written");
}

static void test_write(void)
{
	static const char *const names[] = { "A", "Vita", "My Vita 2", "0123456789abcdef", "PlayStation Vita" };

	for (unsigned k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
		reset();
		for (int i = 0; i < 10; i++)
			btt_btname_sync(names[k]);
		CHECK(g_injects == 1 && g_lookups == 1, "%s: %d injects, %d lookups", names[k], g_injects, g_lookups);
		uint8_t want[17] = { 0 };
		memcpy(want, names[k], strlen(names[k]));
		CHECK(!memcmp(g_text_mem + OFF_NAME, want, 17), "%s: not the characters then NULs", names[k]);
		CHECK(g_text_mem[OFF_NAME + 16] == 0, "%s: no NUL in the 17th byte", names[k]);
		for (int i = 0; i < TEXT_SIZE; i++)
			if (g_text_mem[i] != g_pristine[i])
				CHECK(i >= (int)OFF_NAME && i < (int)OFF_NAME + 17, "%s: a byte outside the text changed at 0x%x", names[k], i);
		btt_btname_sync("");
		CHECK(stock() && g_releases == 1, "%s: the empty name did not restore the stock text", names[k]);
		btt_btname_sync("");
		CHECK(g_releases == 1, "released twice");
	}
}

static void test_change_and_stop(void)
{
	reset();
	btt_btname_sync("First");
	btt_btname_sync("Second");
	CHECK(g_injects == 2 && g_releases == 1, "a change of name: %d injects, %d releases", g_injects, g_releases);
	CHECK(!memcmp(g_text_mem + OFF_NAME, "Second\0\0\0\0\0\0\0\0\0\0\0", 17), "the new name is not in");
	btt_btname_restore();
	CHECK(stock(), "stop left the name in");
	btt_btname_restore();
	CHECK(g_releases == g_injects, "releases %d, injects %d", g_releases, g_injects);
}

static void test_window(void)
{
	for (size_t i = 0; i < sizeof(k_name_win); i++) {
		reset();
		g_text_mem[OFF_NAME_WIN + i] ^= 0x01;
		memcpy(g_pristine, g_text_mem, sizeof(g_text_mem));
		for (int k = 0; k < 10; k++)
			btt_btname_sync("Vita");
		CHECK(g_injects == 0 && stock(), "byte %d of the window differs and a name was written", (int)i);
		CHECK(g_lookups == 1, "byte %d: looked at %d times in ten passes", (int)i, g_lookups);
		btt_btname_sync("Other");
		CHECK(g_injects == 0, "byte %d: another name was written on a code that is not the stock code", (int)i);
	}
}

static void test_failures(void)
{
	reset();
	g_have_module = 0;
	for (int i = 0; i < 20; i++)
		btt_btname_sync("Vita");
	CHECK(g_injects == 0 && g_lookups == 1, "missing module: %d injects, %d lookups", g_injects, g_lookups);
	reset();
	g_offset_ok = 0;
	for (int i = 0; i < 20; i++)
		btt_btname_sync("Vita");
	CHECK(g_injects == 0 && g_lookups == 1, "no segment: %d injects, %d lookups", g_injects, g_lookups);
	/* a refused write is not retried every pass; a different name is tried */
	reset();
	g_fail_inject = 1;
	for (int i = 0; i < 20; i++)
		btt_btname_sync("Vita");
	CHECK(g_injects == 0 && stock(), "a refused write left bytes");
	g_fail_inject = 0;
	btt_btname_sync("Vita");
	CHECK(g_injects == 0, "a refused name was retried without a change");
	btt_btname_sync("Vita 2");
	CHECK(g_injects == 1, "a different name was not tried");
	/* off and on again for the same name: tried again */
	btt_btname_sync("");
	g_fail_inject = 0;
	btt_btname_sync("Vita");
	CHECK(g_injects == 2, "the name was not tried again after the empty name");
}

int main(void)
{
	test_rule();
	test_default_is_stock();
	test_write();
	test_change_and_stop();
	test_window();
	test_failures();
	printf("test_btname: ok\n");
	return 0;
}
