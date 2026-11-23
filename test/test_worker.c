/*
 * test_worker.c - host test of the kernel module's start-up, worker and stop logic
 * (kernel/main.c), with the console's kernel calls replaced by fakes.
 *
 *   make test      (cc -Itest/host test/test_worker.c, then run)
 *
 * main.c is compiled into this file as it is. The fakes keep a simulated clock that the
 * worker's wait advances by half a second at a time, so a run of minutes takes no time, and
 * they stand in for the Bluetooth changes (btfix.c, bteq.c, btvol.c: tested on their own in
 * test_btfix.c, test_bteq.c and test_btvol.c). What is proved:
 *
 *   - Nothing that installs a hook or injects code the Bluetooth stack runs (everything but the two code
 *     constants) is applied before the Home screen is up (the boot guard is clear) and 45 s of uptime:
 *     every call is checked at the moment it is made, in a start with the Home screen found early,
 *     found late and never found;
 *   - the two code constants (the timing fix and the send queue's limit: the btt_bt_sync call) go in as soon as
 *     the boot guard is clear and 12 s of uptime have passed, that is at max(guard clear, 12 s), while the
 *     controller response, asked for in the same call, is held back until 45 s; changes made to the settings
 *     in between (latency, master switch) are followed at once;
 *   - the boot guard goes '1' at start and '0' when SceShell has been up for 5 s or the module
 *     has run for 30 s, is overwritten in place (never truncated) at a fixed length;
 *   - the arguments follow the settings (master switch, latency level, controller response, the
 *     sound preset and its Custom bands, channels, balance, volume curve and maximum, connection
 *     quality), loaded at
 *     start, changed by bttSet, saved when changed; a value out of range is refused and changes
 *     nothing; with every new setting at its default the arguments are the stock ones; a 0.1
 *     settings file (8 keys, low latency 1) loads, with the new keys at their defaults, and 1 is Low;
 *     so does a file from before the audio quality key (13 keys), with Standard; a file from before the audio quality
 *     order (no marker after the name: 0 Best, 1 Balanced, 2 Stable) is converted (Standard, Balanced, Stable; 3 is no value);
 *   - what happens when headphones disconnect, the reconnecting after standby and the Bluetooth name follow
 *     the same rules (their arguments at every call, default = stock: Mute, off, no name), the name goes through
 *     the syscalls (bttSetName refuses what is not a valid name and bttGetName gives the console's own when none
 *     is set), and the settings file carries the name after the ints: an older file (fewer keys, no name) still
 *     loads with the new settings at their defaults, a file from a later version (more keys) is read by its own
 *     count and still gives the name, a file with an invalid name or with more keys than any version has gives
 *     the console's own, and what is saved is read back;
 *   - switching Bluetooth Turbo off takes every change out again (timing fix off, latency
 *     Standard, controller response Standard, sound flat) at once, and module stop restores
 *     all of them, once, after the last pass;
 *   - a start that follows one that didn't finish (boot guard '1') runs nothing at all, and its
 *     stop changes nothing.
 *
 * Each scenario runs in its own process: main.c keeps state in statics.
 */
#define BTT_HOST_TEST 1
#include "../kernel/main.c"

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#define US(s) ((uint64_t)(s) * 1000000ull)

/* The measurement behind the early gate, written out so that a change of the constant in main.c fails here:
 * the shared radio chip is written by another plugin from 7.9 s to 9.0 s, so 12 s is a few seconds after. */
#define EARLY_SEC 12
_Static_assert(BTT_EARLY_UPTIME == EARLY_SEC, "the early gate is 12 s");

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

/* ---- the scenario ---------------------------------------------------------- */

typedef struct {
	const char *name;
	int  boot_sec;           /* uptime when the module starts                    */
	int  shell_sec;          /* uptime SceShell appears, -1 never                */
	int  end_sec;            /* uptime the run ends                              */
	char guard_before;       /* preset boot_guard first byte, 0 none             */
	int  saved;              /* 1: settings file preset (Low latency, Custom sound, bands, the 0.2 keys, and the newer ones with a name),
	                          * 2: a 0.1 file: 8 keys, low latency 1, 3: a file from before the quality key: 13 keys, 4: 14 keys, the quality key in the old order,
	                          *    a file from before the disconnect and reconnect keys and the name: no name,
	                          * 5: a file from a later version: three more keys than this build has, then the name, 6: as 1 with a name that is not valid,
	                          * 7: as 1 but claiming 40 keys (more than any version has), so no name and no order marker are read,
	                          * 8, 9, 10: files from before the audio quality order (no marker after the name) holding the old
	                          * quality 1 (Balanced), 0 (Best) and 3 (not a value of that order), 11: a current file holding Highest,
	                          * 12: a file written by an earlier build, before the low battery key: the keys up to the internal
	                          *     battery mode (stored as 0), then the name and the marker; the new key must be at its default, and
	                          *     the mode on,
	                          * 13: a current file with Read Headset Battery off (the stored mode says on, and is not read): the mode is off */
	int  toggle;             /* 1: scripted changes at 60 / 65 / 68 / 70 / 75 / 80 s */
	int  early;              /* 1: scripted changes between the early and the late gate: Lowest and Fast at 20 s, the master
	                          * switch off at 25 s and on at 30 s; 2: the master switch off from the start until EARLY_OFF_UNTIL s */
} Scn;
#define EARLY_OFF_UNTIL 20   /* scenario early == 2: the master switch is off from the start until this uptime (s); nothing of the add-ons' self-tests may be asked meanwhile */

static const Scn *S;
static uint64_t g_now;

/* ---- fake file system -------------------------------------------------------- */

static struct { char path[96]; unsigned char data[512]; int len; int used; } g_files[8];
static struct { int file, pos; } g_fds[8];
static int g_guard_truncs;     /* opens of the boot guard that truncate it */

static int file_find(const char *p)
{
	for (int i = 0; i < 8; i++)
		if (g_files[i].used && !strcmp(g_files[i].path, p))
			return i;
	return -1;
}

SceUID ksceIoOpen(const char *path, int flags, int mode)
{
	int f = file_find(path);
	if (f < 0) {
		if (!(flags & SCE_O_CREAT))
			return -1;
		for (f = 0; f < 8 && g_files[f].used; f++) ;
		CHECK(f < 8, "file table full");
		g_files[f].used = 1;
		g_files[f].len = 0;
		snprintf(g_files[f].path, sizeof(g_files[f].path), "%s", path);
	}
	if ((flags & SCE_O_TRUNC) && !strcmp(path, BTT_GUARD_PATH))
		g_guard_truncs++;
	if (flags & SCE_O_TRUNC)
		g_files[f].len = 0;
	for (int i = 0; i < 8; i++)
		if (!g_fds[i].file) {
			g_fds[i].file = f + 1;
			g_fds[i].pos = 0;
			return i + 1;
		}
	CHECK(0, "fd table full");
	return -1;
}

int ksceIoRead(SceUID fd, void *buf, SceSize n)
{
	int f = g_fds[fd - 1].file - 1, left = g_files[f].len - g_fds[fd - 1].pos;
	if (left < 0) left = 0;
	if ((int)n > left) n = left;
	memcpy(buf, g_files[f].data + g_fds[fd - 1].pos, n);
	g_fds[fd - 1].pos += n;
	return n;
}

int ksceIoWrite(SceUID fd, const void *buf, SceSize n)
{
	int f = g_fds[fd - 1].file - 1, p = g_fds[fd - 1].pos;
	CHECK(p + (int)n <= (int)sizeof(g_files[f].data), "file too big");
	memcpy(g_files[f].data + p, buf, n);
	g_fds[fd - 1].pos += n;
	if (p + (int)n > g_files[f].len)
		g_files[f].len = p + n;
	return n;
}

int ksceIoClose(SceUID fd) { g_fds[fd - 1].file = 0; return 0; }
int ksceIoMkdir(const char *path, int mode) { return 0; }

static void put_file(const char *path, const void *data, int len)
{
	SceUID fd = ksceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0);
	ksceIoWrite(fd, data, len);
	ksceIoClose(fd);
}

static int get_file(const char *path, void *out, int max)
{
	SceUID fd = ksceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0)
		return -1;
	int n = ksceIoRead(fd, out, max);
	ksceIoClose(fd);
	return n;
}

/* ---- the rest of the module's world ----------------------------------------------- */

static int g_shell_present;
int btt_shell_up(void) { return g_shell_present; }
void btt_fwlog_poll(void) { }
void btt_klog_init(void) { }
static char g_klog[8192];      /* every line the module logged, one after the other */
static int count_in_log(const char *needle)
{
	int n = 0;
	for (const char *p = g_klog; (p = strstr(p, needle)); p += strlen(needle))
		n++;
	return n;
}
void btt_klog_line(const char *line)
{
	if (strlen(g_klog) + strlen(line) + 2 < sizeof(g_klog)) {
		strcat(g_klog, line);
		strcat(g_klog, "\n");
	}
	if (getenv("BTT_TEST_VERBOSE"))
		printf("    [%3llu.%03llu] %s\n", (unsigned long long)(g_now / 1000000),
		       (unsigned long long)(g_now / 1000 % 1000), line);
}

/* ---- the two Bluetooth changes ------------------------------------------------------ */

typedef struct { uint64_t us; int ts, lat, ctrl; } FixCall;
typedef struct { uint64_t us; int enable, sound, lowlat, band[5], chan, bal, curve, vmax, ctrl; } EqCall;
typedef struct { uint64_t us; int enable, curve, vmax; } VolCall;
typedef struct { uint64_t us; int level; } QualCall;
typedef struct { uint64_t us; int mode; } DiscCall;
typedef struct { uint64_t us; int on; } RcCall;
typedef struct { uint64_t us; char name[BTT_NAME_MAX + 2]; } NameCall;
static FixCall g_fix[1024];
static EqCall g_eq[1024];
static VolCall g_vol[1024];
static QualCall g_qual[1024];
static DiscCall g_disc[1024];
static RcCall g_rc[1024];
static NameCall g_nm[1024];
static int g_nfix, g_neq, g_nvol, g_nqual, g_fix_restore, g_eq_restore, g_vol_restore, g_qual_restore;
static int g_ndisc, g_nrc, g_nnm, g_disc_restore, g_rc_restore, g_nm_restore;
static int g_order;                 /* counts calls, to see that restore came last */
static int g_last_sync_order, g_restore_order;

static void must_be_early(const char *what)
{
	/* The rule under test for the two code constants: the boot guard clear and 12 s in. */
	CHECK(g_guard_clear && g_now >= US(EARLY_SEC),
	      "%s at %llu s with the boot guard %s", what, (unsigned long long)(g_now / 1000000),
	      g_guard_clear ? "clear" : "not clear");
}

static void must_be_late(const char *what)
{
	/* The rule under test: never before the Home screen is up and 45 s in. */
	CHECK(g_guard_clear && g_now >= US(BTT_MIN_UPTIME),
	      "%s at %llu s with the boot guard %s", what, (unsigned long long)(g_now / 1000000),
	      g_guard_clear ? "clear" : "not clear");
}

void btt_bt_sync(int ts_fix, int latency, int ctrl_fast)
{
	must_be_early("btt_bt_sync");
	/* the controller response is a change of the Bluetooth code's own: it waits for the late gate */
	CHECK(!ctrl_fast || g_now >= US(BTT_MIN_UPTIME), "controller response asked for at %llu s", (unsigned long long)(g_now / 1000000));
	CHECK(g_nfix < 1024, "too many calls");
	g_fix[g_nfix++] = (FixCall){ g_now, ts_fix, latency, ctrl_fast };
	g_last_sync_order = ++g_order;
}
void btt_bt_restore(void) { g_fix_restore++; g_restore_order = ++g_order; }

void btt_bteq_sync(const int *settings)
{
	must_be_late("btt_bteq_sync");
	CHECK(settings == g_set, "btt_bteq_sync not given the module's settings");
	CHECK(g_neq < 1024, "too many calls");
	EqCall *c = &g_eq[g_neq++];
	c->us = g_now;
	c->enable = settings[BTT_KEY_ENABLE];
	c->sound = settings[BTT_KEY_SOUND];
	c->lowlat = settings[BTT_KEY_LOWLAT];
	for (int i = 0; i < 5; i++)
		c->band[i] = settings[BTT_KEY_BAND1 + i];
	c->chan = settings[BTT_KEY_CHANNELS];
	c->bal = settings[BTT_KEY_BALANCE];
	g_last_sync_order = ++g_order;
}
void btt_bteq_restore(void) { g_eq_restore++; g_restore_order = ++g_order; }

void btt_btvol_sync(const int *settings)
{
	must_be_late("btt_btvol_sync");
	CHECK(settings == g_set, "btt_btvol_sync not given the module's settings");
	CHECK(g_nvol < 1024, "too many calls");
	g_vol[g_nvol++] = (VolCall){ g_now, settings[BTT_KEY_ENABLE], settings[BTT_KEY_VOLCURVE], settings[BTT_KEY_VOLMAX] };
	g_last_sync_order = ++g_order;
}
void btt_btvol_restore(void) { g_vol_restore++; g_restore_order = ++g_order; }

void btt_btqual_sync(int level)
{
	must_be_late("btt_btqual_sync");
	CHECK(g_nqual < 1024, "too many calls");
	g_qual[g_nqual++] = (QualCall){ g_now, level };
	g_last_sync_order = ++g_order;
}
void btt_btqual_restore(void) { g_qual_restore++; g_restore_order = ++g_order; }

void btt_btdisc_sync(int mode)
{
	must_be_late("btt_btdisc_sync");
	CHECK(g_ndisc < 1024, "too many calls");
	g_disc[g_ndisc++] = (DiscCall){ g_now, mode };
	g_last_sync_order = ++g_order;
}
void btt_btdisc_restore(void) { g_disc_restore++; g_restore_order = ++g_order; }

void btt_btreconn_sync(int on)
{
	must_be_late("btt_btreconn_sync");
	CHECK(g_nrc < 1024, "too many calls");
	g_rc[g_nrc++] = (RcCall){ g_now, on };
	g_last_sync_order = ++g_order;
}
void btt_btreconn_restore(void) { g_rc_restore++; g_restore_order = ++g_order; }

void btt_btname_sync(const char *name)
{
	must_be_late("btt_btname_sync");
	CHECK(g_nnm < 1024 && name && strlen(name) <= BTT_NAME_MAX, "too many calls, or a name that is not a string of at most 16");
	g_nm[g_nnm].us = g_now;
	strcpy(g_nm[g_nnm++].name, name);
	g_last_sync_order = ++g_order;
}
void btt_btname_restore(void) { g_nm_restore++; g_restore_order = ++g_order; }

/* High and Highest: called on every pass with the quality level while Bluetooth Turbo is on, else Standard */
static struct { uint64_t us; int level; } g_hq[1024];
static int g_nhq, g_hq_restore;
void btt_bthq_sync(int level)
{
	must_be_late("btt_bthq_sync");
	CHECK(g_nhq < 1024, "too many calls");
	g_hq[g_nhq].us = g_now;
	g_hq[g_nhq++].level = level;
	g_last_sync_order = ++g_order;
}
void btt_bthq_restore(void) { g_hq_restore++; g_restore_order = ++g_order; }

/* The headset battery code: called on every pass from the late gate with the internal mode, 4 while Bluetooth Turbo is on, else 0 */
static struct { uint64_t us; int mode; } g_hfp[1024];
static int g_nhfp, g_hfp_restore;
void btt_hfp_sync(int mode)
{
	must_be_late("btt_hfp_sync");
	CHECK(g_nhfp < 1024, "too many calls");
	g_hfp[g_nhfp].us = g_now;
	g_hfp[g_nhfp++].mode = mode;
	g_last_sync_order = ++g_order;
}
void btt_hfp_restore(void) { g_hfp_restore++; g_restore_order = ++g_order; }

/* The optional add-ons (AAC: btaac_link.c, tested on its own in test_btaac_link.c; aptX and LDAC: btaptx_link.c and btldac_link.c, tested in test_btaac_stream.c):
 * looked for once the early gate is open (called on every pass from then on; the real call does its work once). Their self-tests are asked from the early gate,
 * one at a time (the next only after the one before has its verdict), only while the master switch is on, and never again; the fakes are the add-ons' side: the AAC
 * and LDAC verdicts come a little after the ask (their own threads), the aptX one at once (it runs inside the call). With BTT_TEST_SELFTEST_STUCK in the environment
 * the AAC add-on never gives a verdict: the next test starts BTT_SELFTEST_WAIT seconds after it was asked. The modules themselves are not started or stopped from
 * here: they have their own entry. */
#define AAC_VERDICT_US   600000ull
#define LDAC_VERDICT_US  1000000ull
static int selftest_stuck(void) { return getenv("BTT_TEST_SELFTEST_STUCK") != NULL; }
static struct { uint64_t us; int on, codec; } g_aac[1024];
static int g_naac, g_aac_find, g_aac_avail_fake;
void btt_aac_link_find(void) { must_be_early("btt_aac_link_find"); g_aac_find++; }
void btt_aac_link_sync(int on, int codec)
{
	must_be_early("btt_aac_link_sync");
	CHECK(g_naac < 1024, "too many calls");
	g_aac[g_naac].us = g_now;
	g_aac[g_naac].on = on;
	g_aac[g_naac++].codec = codec;
	g_last_sync_order = ++g_order;
}
int btt_aac_selftest_done(void) { return g_naac && !selftest_stuck() && g_now >= g_aac[0].us + AAC_VERDICT_US; }
int btt_aac_avail(void) { return g_aac_avail_fake; }

/* The optional aptX module (btaptx_link.c, tested in test_btaac_stream.c): looked for on the same passes and asked on the same late passes as the AAC one. */
static int g_apt_find, g_napt, g_apt_avail_fake;
static struct { uint64_t us; int on; } g_apt[8];
void btt_aptx_link_find(void) { must_be_early("btt_aptx_link_find"); g_apt_find++; }
void btt_aptx_link_sync(int on, int codec)
{
	must_be_early("btt_aptx_link_sync");
	(void)codec;
	CHECK(g_napt < 8, "too many calls");
	g_apt[g_napt].us = g_now;
	g_apt[g_napt++].on = on;
}
int btt_aptx_selftest_done(void) { return g_napt > 0; }   /* the aptX test runs inside the ask */
int btt_aptx_avail(void) { return g_apt_avail_fake; }
int btt_aptx_hd_usable(void) { return 0; }
/* The optional LDAC module (btldac_link.c, tested in test_btaac_stream.c): looked for and asked on the same passes as the others; its setting and the 5-slot packet types are passed on late. */
static int g_ld_find, g_nld, g_ld_avail_fake;
static struct { uint64_t us; int on; } g_ld[8];
void btt_ldac_link_find(void) { must_be_early("btt_ldac_link_find"); g_ld_find++; }
void btt_ldac_link_sync(int on, int codec)
{
	must_be_early("btt_ldac_link_sync");
	(void)codec;
	CHECK(g_nld < 8, "too many calls");
	g_ld[g_nld].us = g_now;
	g_ld[g_nld++].on = on;
}
int btt_ldac_selftest_done(void) { return g_nld && g_now >= g_ld[0].us + LDAC_VERDICT_US; }
int btt_ldac_avail(void) { return g_ld_avail_fake; }
int btt_ldac_codec_effective(int codec) { (void)codec; return 0; }
void btt_aac_stream_set_ldac_mode(unsigned mode) { must_be_late("btt_aac_stream_set_ldac_mode"); (void)mode; }
/* aptX HD When Available: handed on every late pass, before the stream pass and the apply pass */
static int g_aptxhd_log[1024], g_naptxhd;
void btt_aac_stream_set_aptx_hd(int on) { must_be_late("btt_aac_stream_set_aptx_hd"); CHECK(g_naptxhd < 1024, "too many calls"); g_aptxhd_log[g_naptxhd++] = on; }
/* Music Source Only: handed on every late pass, before the stream pass */
static int g_srconly_log[1024], g_nsrconly;
void btt_aac_stream_set_source_only(int on) { must_be_late("btt_aac_stream_set_source_only"); CHECK(g_nsrconly < 1024, "too many calls"); g_srconly_log[g_nsrconly++] = on; }
void btt_bthq_set_ldac(int on) { must_be_late("btt_bthq_set_ldac"); (void)on; }

/* AAC streaming (btaac_stream.c) and the link information (btlink.c), tested on their own in test_btaac_stream.c: called on every late pass with the
 * master switch, the codec, the accelerated setting and the latency; the worker looks every 0.1 s instead of every 0.5 s while an AAC stream is configured;
 * restored first at stop. */
static struct { uint64_t us; int on, codec, accel; } g_stream[1024];
static struct { uint64_t us; int on, latency; } g_link[1024];
static int g_nstream, g_nlink, g_stream_restore, g_stream_fast_fake, g_stream_wait_fast, g_stream_restore_first;
void btt_aac_stream_pass(int on, int codec, int accel)
{
	must_be_late("btt_aac_stream_pass");
	CHECK(g_nstream < 1024, "too many calls");
	g_stream[g_nstream].us = g_now;
	g_stream[g_nstream].on = on;
	g_stream[g_nstream].codec = codec;
	g_stream[g_nstream++].accel = accel;
	g_last_sync_order = ++g_order;
}
int btt_aac_stream_fast(void) { return g_stream_fast_fake; }
/* the send queue's limit follows the stream's codec (an early call each pass, before btt_bt_sync) and the engine's output bound follows the latency level (a late call) */
static int g_nsetaac, g_nlat, g_lat_level[1024], g_is_aac_fake;
int btt_aac_stream_is_aac(void) { return g_is_aac_fake; }
void btt_bt_set_aac(int aac) { must_be_early("btt_bt_set_aac"); CHECK(aac == g_is_aac_fake, "the stream's codec is passed on"); g_nsetaac++; }
void btt_aac_stream_set_latency(int level) { must_be_late("btt_aac_stream_set_latency"); CHECK(g_nlat < 1024, "too many calls"); g_lat_level[g_nlat++] = level; }
/* the AAC bit rate (kb/s) the worker last handed on: a late call, every pass */
static unsigned g_kbps_log[1024];
static int g_nkbps;
void btt_aac_stream_set_kbps(unsigned kbps) { must_be_late("btt_aac_stream_set_kbps"); CHECK(g_nkbps < 1024, "too many calls"); g_kbps_log[g_nkbps++] = kbps; }
int btt_aac_stream_wants_pcm_hook(void) { return 0; }
void btt_aac_stream_restore(void) { g_stream_restore_first = g_order == g_last_sync_order; g_stream_restore++; g_restore_order = ++g_order; }
static int g_ncodecs;
void btt_codecs_pass(void) { must_be_late("btt_codecs_pass"); g_ncodecs++; }   /* the codec lists' file: the worker's, every late pass */
static int g_nfall;
void btt_fall_pass(int on, int codec) { must_be_late("btt_fall_pass"); (void)on; (void)codec; g_nfall++; }   /* the codec fallback notice (btfall.c, test_btfall.c): after the link information, every late pass */
void btt_link_pass(int on, int latency)
{
	must_be_late("btt_link_pass");
	CHECK(g_nlink < 1024, "too many calls");
	g_link[g_nlink].us = g_now;
	g_link[g_nlink].on = on;
	g_link[g_nlink++].latency = latency;
	g_last_sync_order = ++g_order;
}

/* the settings that apply at once: the five settings the worker hands on, every late pass; the no-mute hold is set by the environment (a second run of the test) */
static struct { uint64_t us; int on, codec, kbps, accel, quality, aptx_hd; } g_apply[1024];
static int g_napply;
static int g_refresh_pending, g_refresh_taken;   /* Refresh Codecs: noted by the syscall, taken by the next apply pass */
static int apply_hold(void) { return getenv("BTT_TEST_APPLY_HOLD") != NULL; }
void btt_btapply_pass(int on, int codec, int kbps, int accel, int quality, int aptx_hd)
{
	must_be_late("btt_btapply_pass");
	g_refresh_taken += g_refresh_pending;
	g_refresh_pending = 0;
	CHECK(g_napply < 1024, "too many calls");
	g_apply[g_napply].us = g_now;
	g_apply[g_napply].on = on;
	g_apply[g_napply].codec = codec;
	g_apply[g_napply].kbps = kbps;
	g_apply[g_napply].accel = accel;
	g_apply[g_napply].quality = quality;
	g_apply[g_napply++].aptx_hd = aptx_hd;
	g_last_sync_order = ++g_order;
}
int btt_btapply_hold(void) { return apply_hold(); }
/* Refresh Codecs: the syscall's request, taken by the next btt_btapply_pass (the real one is in btapply.c, tested in test_btapply.c): one at a time. */
int btt_btapply_request_refresh(void)
{
	if (g_refresh_pending)
		return BTT_ERR_BUSY;
	g_refresh_pending = 1;
	return BTT_OK;
}

/* the system's checked copies between the caller's memory and the kernel's: plain copies here */
int ksceKernelMemcpyKernelToUser(void *dst, const void *src, size_t len) { memcpy(dst, src, len); return 0; }
SceSSize ksceKernelStrncpyUserToKernel(char *dst, const char *src, SceSize len)
{
	SceSize n = 0;
	while (n < len && src[n]) {
		dst[n] = src[n];
		n++;
	}
	if (n < len)
		dst[n] = 0;
	return (SceSSize)n;
}

/* The flush timeout: called on
 * every pass with 1 exactly while Bluetooth Turbo is on and the latency is Lowest. */
static struct { uint64_t us; int lowest; } g_flush[1024];
static int g_nflush, g_flush_restore;
void btt_btflush_sync(int lowest)
{
	must_be_late("btt_btflush_sync");
	CHECK(g_nflush < 1024, "too many calls");
	g_flush[g_nflush].us = g_now;
	g_flush[g_nflush++].lowest = lowest;
	g_last_sync_order = ++g_order;
}
void btt_btflush_restore(void) { g_flush_restore++; g_restore_order = ++g_order; }

/* The link log (btevlog.c, tested on its own in test_btevlog.c): called on every late pass with the master switch; its "a link is new" answer makes the worker pass faster, like the AAC stream's. */
static struct { uint64_t us; int on; } g_evlog[1024];
static int g_nevlog, g_evlog_restore, g_evlog_fast_fake, g_evlog_wait_fast;
void btt_btevlog_pass(int on)
{
	must_be_late("btt_btevlog_pass");
	CHECK(g_nevlog < 1024, "too many calls");
	g_evlog[g_nevlog].us = g_now;
	g_evlog[g_nevlog++].on = on;
	g_last_sync_order = ++g_order;
}
int btt_btevlog_fast(void) { return g_evlog_fast_fake; }
void btt_btevlog_restore(void) { g_evlog_restore++; g_restore_order = ++g_order; }

/* ---- the simulated kernel -------------------------------------------------------- */

static uint64_t g_guard_clear_seen_us;
static int g_threads_created;
static SceKernelThreadEntry g_entry;

uint64_t ksceKernelGetSystemTimeWide(void) { return g_now; }
SceUID ksceKernelCreateEventFlag(const char *n, unsigned a, unsigned p, void *o) { return 1; }
int ksceKernelDeleteEventFlag(SceUID ev) { return 0; }
int ksceKernelSetEventFlag(SceUID ev, unsigned bits) { return 0; }
SceUID ksceKernelCreateThread(const char *n, SceKernelThreadEntry e, int p, SceSize s, unsigned a, int c, void *o)
{
	g_threads_created++;
	g_entry = e;
	return 2;
}
/* The worker runs to its end inside the start call; its waits drive the clock. */
int ksceKernelStartThread(SceUID th, SceSize args, void *argp) { g_entry(0, NULL); return 0; }
int ksceKernelWaitThreadEnd(SceUID th, int *r, SceUInt *t) { return 0; }
int ksceKernelDeleteThread(SceUID th) { return 0; }

static int g_refused;   /* bttSet refusals seen in the scripted changes */

int ksceKernelWaitEventFlag(SceUID ev, unsigned bits, unsigned mode, unsigned *res, SceUInt *timeout)
{
	if (g_now >= US(S->end_sec)) {
		*res = EV_STOP;
		return 0;
	}
	CHECK(timeout && *timeout == (g_stream_fast_fake || g_evlog_fast_fake ? BTT_FAST_MS : BTT_CHECK_MS) * 1000u, "the worker waited %u us with fast = %d, %d", timeout ? (unsigned)*timeout : 0u,
	      g_stream_fast_fake, g_evlog_fast_fake);
	g_stream_wait_fast += g_stream_fast_fake;
	g_evlog_wait_fast += g_evlog_fast_fake;
	g_now += BTT_CHECK_MS * 1000ull;     /* (the grid of the scripted changes stays 0.5 s whatever the worker asked for) */
	unsigned sec = (unsigned)(g_now / 1000000);
	g_stream_fast_fake = g_now >= US(70) && g_now < US(72);   /* an AAC stream configured for two seconds */
	g_evlog_fast_fake = g_now >= US(84) && g_now < US(85);    /* a new link, for one second */
	g_shell_present = S->shell_sec >= 0 && (int)sec >= S->shell_sec;
	if (g_guard_clear && !g_guard_clear_seen_us)
		g_guard_clear_seen_us = g_now;
	if (S->early == 2) {
		if (g_now == US(S->boot_sec) + BTT_CHECK_MS * 1000ull)
			CHECK(bttSet(BTT_KEY_ENABLE, 0) == BTT_OK, "bttSet enable off at the start");
		if (g_now == US(EARLY_OFF_UNTIL))
			CHECK(bttSet(BTT_KEY_ENABLE, 1) == BTT_OK, "bttSet enable on");
	}
	if (S->early == 1) {
		if (g_now == US(20)) {
			CHECK(bttSet(BTT_KEY_LOWLAT, BTT_LAT_LOWEST) == BTT_OK, "bttSet latency early");
			CHECK(bttSet(BTT_KEY_CTRL, BTT_CTRL_FAST) == BTT_OK, "bttSet controller response early");
		}
		if (g_now == US(25))
			CHECK(bttSet(BTT_KEY_ENABLE, 0) == BTT_OK, "bttSet enable off early");
		if (g_now == US(30)) {
			CHECK(bttSet(BTT_KEY_ENABLE, 1) == BTT_OK, "bttSet enable on early");
			/* Refresh Codecs waits for the start-up period: nothing is noted before it is over */
			CHECK(bttSet(BTT_KEY_REFRESH_CODECS, 1) == BTT_ERR_BUSY && !g_refresh_pending, "Refresh Codecs accepted in the start-up period");
		}
	}
	if (S->toggle) {
		if (g_now == US(60)) {
			CHECK(bttSet(BTT_KEY_LOWLAT, 2) == BTT_OK, "bttSet latency");
			CHECK(bttSet(BTT_KEY_CTRL, 1) == BTT_OK, "bttSet controller response");
		}
		if (g_now == US(62))
			CHECK(bttSet(BTT_KEY_QUALITY, BTT_Q_BALANCED) == BTT_OK, "bttSet quality");
		if (g_now == US(63))
			CHECK(bttSet(BTT_KEY_QUALITY, BTT_Q_HIGHEST) == BTT_OK, "bttSet quality highest");
		if (g_now == US(64)) {
			char got[24];
			CHECK(bttSet(BTT_KEY_DISCONNECT, BTT_DISC_PAUSE) == BTT_OK && bttSet(BTT_KEY_RECONNECT, 1) == BTT_OK, "bttSet disconnect/reconnect");
			CHECK(bttSetName("My Vita") == BTT_OK, "bttSetName");
			CHECK(bttGetName(got, sizeof(got)) == 7 && !strcmp(got, "My Vita"), "bttGetName gave '%s'", got);
			/* refused, and nothing changes */
			CHECK(bttSet(BTT_KEY_DISCONNECT, BTT_DISC_COUNT) == BTT_ERR_RANGE && bttSet(BTT_KEY_RECONNECT, 2) == BTT_ERR_RANGE &&
			      bttSet(BTT_KEY_DISCONNECT, -1) == BTT_ERR_RANGE, "a disconnect or reconnect value out of range accepted");
			CHECK(bttSetName("0123456789abcdefg") == BTT_ERR_RANGE && bttSetName("caf\xc3\xa9") == BTT_ERR_RANGE &&
			      bttSetName("tab\there") == BTT_ERR_RANGE && bttSetName(NULL) == BTT_ERR_ARG, "a name that is not valid accepted");
			CHECK(bttGetName(NULL, 10) == BTT_ERR_ARG && bttGetName(got, 0) == BTT_ERR_ARG, "bttGetName took a bad argument");
			CHECK(bttGetName(got, sizeof(got)) == 7 && !strcmp(got, "My Vita") && bttGet(BTT_KEY_DISCONNECT) == BTT_DISC_PAUSE &&
			      bttGet(BTT_KEY_RECONNECT) == 1, "a refused value changed something");
			CHECK(bttGetName(got, 4) == 3 && !strcmp(got, "My "), "bttGetName did not cut to the size given: '%s'", got);
		}
		if (g_now == US(61)) {
			/* the Settings module reports a page it did not change: each reason is logged once, a value that is no
			 * reason is refused and logs nothing */
			CHECK(bttSet(BTT_KEY_PAGE_SKIP, BTT_SKIP_SIZE) == BTT_OK && bttSet(BTT_KEY_PAGE_SKIP, BTT_SKIP_SIZE) == BTT_OK &&
			      bttSet(BTT_KEY_PAGE_SKIP, BTT_SKIP_ANCHOR) == BTT_OK && bttSet(BTT_KEY_PAGE_SKIP, BTT_SKIP_DEVROWS_CLOSE) == BTT_OK &&
			      bttSet(BTT_KEY_PAGE_SKIP, BTT_SKIP_DEVROWS_ROOM) == BTT_OK, "a page skip reason refused");
			CHECK(bttSet(BTT_KEY_PAGE_SKIP, 0) == BTT_ERR_RANGE && bttSet(BTT_KEY_PAGE_SKIP, BTT_SKIP_COUNT) == BTT_ERR_RANGE &&
			      bttSet(BTT_KEY_PAGE_SKIP, -1) == BTT_ERR_RANGE, "a page skip value that is no reason accepted");
		}
		if (g_now == US(61)) {
			/* Refresh Codecs: write only, only the value 1, one request at a time (the second is refused until the pass has taken the first), not stored */
			CHECK(bttSet(BTT_KEY_REFRESH_CODECS, 0) == BTT_ERR_RANGE && bttSet(BTT_KEY_REFRESH_CODECS, 2) == BTT_ERR_RANGE &&
			      bttSet(BTT_KEY_REFRESH_CODECS, -1) == BTT_ERR_RANGE && !g_refresh_pending, "Refresh Codecs took a value that is not 1");
			CHECK(bttGet(BTT_KEY_REFRESH_CODECS) == BTT_ERR_ARG, "Refresh Codecs can be read");
			CHECK(bttSet(BTT_KEY_REFRESH_CODECS, 1) == BTT_OK && g_refresh_pending, "Refresh Codecs refused after the start-up period");
			CHECK(bttSet(BTT_KEY_REFRESH_CODECS, 1) == BTT_ERR_BUSY, "a second Refresh Codecs before the pass was accepted");
		}
		if (g_now == US(85)) {
			CHECK(bttSet(BTT_KEY_PAGE_SKIP, BTT_SKIP_ALLOC) == BTT_OK && bttSet(BTT_KEY_PAGE_SKIP, BTT_SKIP_SIZE) == BTT_OK,
			      "a later page skip reason refused");
		}
		if (g_now == US(65)) {
			CHECK(bttSet(BTT_KEY_CHANNELS, BTT_CH_MONO) == BTT_OK && bttSet(BTT_KEY_BALANCE, 2) == BTT_OK, "bttSet channels");
			/* refused, and nothing changes */
			CHECK(bttSet(BTT_KEY_CHANNELS, BTT_CH_COUNT) == BTT_ERR_RANGE && bttSet(BTT_KEY_BALANCE, BTT_BAL_STEPS) == BTT_ERR_RANGE &&
			      bttSet(BTT_KEY_VOLCURVE, BTT_VC_COUNT) == BTT_ERR_RANGE && bttSet(BTT_KEY_VOLMAX, BTT_VOLMAX_COUNT) == BTT_ERR_RANGE &&
			      bttSet(BTT_KEY_CTRL, BTT_CTRL_COUNT) == BTT_ERR_RANGE && bttSet(BTT_KEY_LOWLAT, BTT_LAT_COUNT) == BTT_ERR_RANGE &&
			      bttSet(BTT_KEY_QUALITY, BTT_Q_COUNT) == BTT_ERR_RANGE && bttSet(BTT_KEY_QUALITY, -1) == BTT_ERR_RANGE,
			      "a new key accepted a value out of range");
			CHECK(bttGet(BTT_KEY_CHANNELS) == BTT_CH_MONO && bttGet(BTT_KEY_BALANCE) == 2 && bttGet(BTT_KEY_LOWLAT) == 2 &&
			      bttGet(BTT_KEY_CTRL) == 1 && bttGet(BTT_KEY_QUALITY) == BTT_Q_HIGHEST, "a refused value changed something");
		}
		if (g_now == US(66)) {
			CHECK(bttSet(BTT_KEY_QUALITY, BTT_Q_HIGH) == BTT_OK && bttSet(BTT_KEY_QUALITY, BTT_Q_COUNT) == BTT_ERR_RANGE &&
			      bttGet(BTT_KEY_QUALITY) == BTT_Q_HIGH, "bttSet quality high");
			/* the two battery keys: on by default, whatever the file said; refused out of range; the mode can be forced off */
			CHECK(bttGet(BTT_KEY_LOWBATT) == 1 && bttGet(BTT_KEY_READBATT) == 1 && bttGet(BTT_KEY_HFP) == BTT_HFP_MODE_ON,
			      "the battery keys are not on by default");
			CHECK(bttSet(BTT_KEY_LOWBATT, 0) == BTT_OK && bttSet(BTT_KEY_LOWBATT, 2) == BTT_ERR_RANGE && bttSet(BTT_KEY_LOWBATT, -1) == BTT_ERR_RANGE &&
			      bttGet(BTT_KEY_LOWBATT) == 0, "bttSet low battery notifications");
			/* Read Headset Battery off puts the mode at 0, on at its whole; a value out of range is refused and changes nothing */
			CHECK(bttSet(BTT_KEY_READBATT, 0) == BTT_OK && bttGet(BTT_KEY_READBATT) == 0 && bttGet(BTT_KEY_HFP) == 0,
			      "Read Headset Battery off did not put the mode at 0");
			CHECK(bttSet(BTT_KEY_READBATT, 2) == BTT_ERR_RANGE && bttSet(BTT_KEY_READBATT, -1) == BTT_ERR_RANGE &&
			      bttGet(BTT_KEY_READBATT) == 0 && bttGet(BTT_KEY_HFP) == 0, "a refused Read Headset Battery value changed something");
			CHECK(bttSet(BTT_KEY_HFP, BTT_HFP_MODE_MAX + 1) == BTT_ERR_RANGE && bttGet(BTT_KEY_HFP) == 0, "bttSet the battery mode");
			/* the AAC keys: SBC and the NEON build on by default whatever an older file said; range checked; stored */
			CHECK(bttGet(BTT_KEY_CODEC) == BTT_CODEC_SBC && bttGet(BTT_KEY_AAC_ACCEL) == 1, "the AAC keys are not at their defaults (%d, %d)",
			      bttGet(BTT_KEY_CODEC), bttGet(BTT_KEY_AAC_ACCEL));
			CHECK(bttSet(BTT_KEY_CODEC, BTT_CODEC_COUNT) == BTT_ERR_RANGE && bttSet(BTT_KEY_CODEC, -1) == BTT_ERR_RANGE &&
			      bttSet(BTT_KEY_AAC_ACCEL, 2) == BTT_ERR_RANGE && bttSet(BTT_KEY_AAC_ACCEL, -1) == BTT_ERR_RANGE &&
			      bttGet(BTT_KEY_CODEC) == BTT_CODEC_SBC && bttGet(BTT_KEY_AAC_ACCEL) == 1, "an AAC key accepted a value out of range");
			/* what the Settings page may offer of AAC: read only, answered by the AAC link (here a fake), never a stored key */
			g_aac_avail_fake = BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_TESTED;
			CHECK(bttGet(BTT_KEY_AAC_AVAIL) == (BTT_AAC_AVAIL_MODULE | BTT_AAC_AVAIL_TESTED), "bttGet of the AAC availability");
			g_aac_avail_fake = 0;
			CHECK(bttGet(BTT_KEY_AAC_AVAIL) == 0 && bttSet(BTT_KEY_AAC_AVAIL, 1) == BTT_ERR_ARG && bttSet(BTT_KEY_AAC_AVAIL, 0) == BTT_ERR_ARG &&
			      bttGet(BTT_KEY_AAC_AVAIL) == 0, "the availability is read only: 0 without the AAC module");
			/* the same for aptX: the codec key takes its value, the availability is read only */
			g_apt_avail_fake = BTT_APTX_AVAIL_MODULE | BTT_APTX_AVAIL_TESTED;
			CHECK(bttGet(BTT_KEY_APTX_AVAIL) == (BTT_APTX_AVAIL_MODULE | BTT_APTX_AVAIL_TESTED) && bttSet(BTT_KEY_APTX_AVAIL, 1) == BTT_ERR_ARG, "bttGet of the aptX availability, read only");
			g_apt_avail_fake = 0;
			CHECK(bttGet(BTT_KEY_APTX_AVAIL) == 0 && bttSet(BTT_KEY_CODEC, BTT_CODEC_APTX) == BTT_OK && bttGet(BTT_KEY_CODEC) == BTT_CODEC_APTX && bttSet(BTT_KEY_CODEC, BTT_CODEC_SBC) == BTT_OK,
			      "the codec key takes aptX");
			CHECK(bttSet(BTT_KEY_CODEC, BTT_CODEC_AAC) == BTT_OK && bttSet(BTT_KEY_AAC_ACCEL, 0) == BTT_OK &&
			      bttGet(BTT_KEY_CODEC) == BTT_CODEC_AAC && bttGet(BTT_KEY_AAC_ACCEL) == 0, "bttSet the AAC keys");
			/* the AAC bit rate: an index into 128/192/256/320 kb/s, 256 by default unless the file says otherwise, range checked, stored */
			CHECK(bttGet(BTT_KEY_AAC_BITRATE) == ((S->saved == 1 || S->saved >= 5) && S->saved != 12 ? BTT_AACBR_192 : BTT_AACBR_256),
			      "the AAC bit rate is not what the file or the default says (%d)", bttGet(BTT_KEY_AAC_BITRATE));
			{
				int before = bttGet(BTT_KEY_AAC_BITRATE);
				CHECK(bttSet(BTT_KEY_AAC_BITRATE, BTT_AACBR_COUNT) == BTT_ERR_RANGE && bttSet(BTT_KEY_AAC_BITRATE, -1) == BTT_ERR_RANGE &&
				      bttGet(BTT_KEY_AAC_BITRATE) == before, "the AAC bit rate accepted a value out of range");
			}
			CHECK(bttSet(BTT_KEY_AAC_BITRATE, BTT_AACBR_320) == BTT_OK && bttGet(BTT_KEY_AAC_BITRATE) == BTT_AACBR_320, "bttSet the AAC bit rate");
		}
		if (g_now == US(67)) {
			/* a mode change made directly through BTT_KEY_HFP moves the internal mode by itself: the user's key is not rewritten */
			CHECK(bttSet(BTT_KEY_HFP, 3) == BTT_OK && bttGet(BTT_KEY_HFP) == 3 && bttGet(BTT_KEY_READBATT) == 0,
			      "a mode change made directly through BTT_KEY_HFP rewrote Read Headset Battery");
		}
		if (g_now == US(68))
			CHECK(bttSet(BTT_KEY_VOLCURVE, BTT_VC_FINER) == BTT_OK && bttSet(BTT_KEY_VOLMAX, 3) == BTT_OK, "bttSet volume");
		if (g_now == US(70)) {
			CHECK(bttSet(BTT_KEY_SOUND, BTT_SOUND_BASS) == BTT_OK, "bttSet sound");
			/* refused, and nothing changes */
			CHECK(bttSet(BTT_KEY_SOUND, BTT_SOUND_COUNT) == BTT_ERR_RANGE, "sound out of range accepted");
			CHECK(bttSet(BTT_KEY_BAND1, BTT_BAND_STEPS) == BTT_ERR_RANGE, "band out of range accepted");
			CHECK(bttSet(BTT_KEY_COUNT, 0) == BTT_ERR_ARG && bttSet(-1, 0) == BTT_ERR_ARG, "unknown key accepted");
			CHECK(bttSet(BTT_KEY_LOWLAT, -1) == BTT_ERR_RANGE, "negative accepted");
			CHECK(bttGet(BTT_KEY_SOUND) == BTT_SOUND_BASS && bttGet(BTT_KEY_LOWLAT) == 2, "a refused value changed something");
			CHECK(bttGet(BTT_KEY_COUNT) == BTT_ERR_ARG && bttGet(500) == BTT_ERR_ARG, "unknown key read");
			g_refused = 1;
		}
		if (g_now == US(72)) {
			CHECK(bttSet(BTT_KEY_QUALITY, BTT_Q_STABLE) == BTT_OK, "bttSet quality again");
			CHECK(bttSet(BTT_KEY_DISCONNECT, BTT_DISC_SPEAKER) == BTT_OK, "bttSet disconnect again");
			/* the console's own name, spelled out, is the same as none */
			CHECK(bttSetName("PlayStation Vita") == BTT_OK, "bttSetName default");
		}
		if (g_now == US(74)) {
			CHECK(bttSetName("Second") == BTT_OK, "bttSetName again");
			CHECK(bttSet(BTT_KEY_LOWBATT, 1) == BTT_OK && bttSet(BTT_KEY_READBATT, 1) == BTT_OK && bttGet(BTT_KEY_HFP) == BTT_HFP_MODE_ON,
			      "Read Headset Battery on did not put the mode on");
		}
		if (g_now == US(75)) {
			CHECK(bttSet(BTT_KEY_SOUND, BTT_SOUND_CUSTOM) == BTT_OK, "bttSet custom");
			CHECK(bttSet(BTT_KEY_BAND1, 2) == BTT_OK && bttSet(BTT_KEY_BAND5, 10) == BTT_OK, "bttSet bands");
			/* aptX HD When Available: on by default (also with an older file, which has no such key), range checked, stored */
			CHECK(bttGet(BTT_KEY_APTX_HD) == 1, "aptX HD When Available is not on by default (%d)", bttGet(BTT_KEY_APTX_HD));
			CHECK(bttSet(BTT_KEY_APTX_HD, 2) == BTT_ERR_RANGE && bttSet(BTT_KEY_APTX_HD, -1) == BTT_ERR_RANGE && bttGet(BTT_KEY_APTX_HD) == 1,
			      "aptX HD When Available accepted a value out of range");
			CHECK(bttSet(BTT_KEY_APTX_HD, 0) == BTT_OK && bttGet(BTT_KEY_APTX_HD) == 0, "bttSet aptX HD When Available");
			/* Music Source Only: on by default (an older file has no such key either), range checked, stored */
			CHECK(bttGet(BTT_KEY_SOURCE_ONLY) == 1, "Music Source Only is not on by default (%d)", bttGet(BTT_KEY_SOURCE_ONLY));
			CHECK(bttSet(BTT_KEY_SOURCE_ONLY, 2) == BTT_ERR_RANGE && bttSet(BTT_KEY_SOURCE_ONLY, -1) == BTT_ERR_RANGE && bttGet(BTT_KEY_SOURCE_ONLY) == 1,
			      "Music Source Only accepted a value out of range");
			CHECK(bttSet(BTT_KEY_SOURCE_ONLY, 0) == BTT_OK && bttGet(BTT_KEY_SOURCE_ONLY) == 0, "bttSet Music Source Only");
		}
		if (g_now == US(80))
			CHECK(bttSet(BTT_KEY_ENABLE, 0) == BTT_OK, "bttSet enable");
	}
	*res = 0;
	return 0;
}

/* ---- running one scenario ---------------------------------------------------------- */

static int guard_first_byte(void)
{
	char b[8];
	return get_file(BTT_GUARD_PATH, b, sizeof(b)) > 0 ? b[0] : 0;
}

static const int SAVED_BANDS[5] = { 0, 3, 6, 9, 12 };

/* the file as this version writes it: header, `keys` ints, and a name after them when `name` is given, then the marker
 * of the audio quality order when `marker` is set (files written before it have none, and hold the old order of the
 * quality key: 0 Best, 1 Balanced, 2 Stable) */
static int build_file(unsigned char *out, int keys, const int *vals, const char *name, int claimed, int marker)
{
	uint32_t w = BTT_MAGIC;
	int n = 0;

	memcpy(out, &w, 4);
	w = (uint32_t)claimed;
	memcpy(out + 4, &w, 4);
	memcpy(out + 8, vals, 4 * (size_t)keys);
	n = 8 + 4 * keys;
	if (name) {
		w = BTT_NAME_MAGIC;
		memcpy(out + n, &w, 4);
		memset(out + n + 4, 0, BTT_NAME_MAX);
		memcpy(out + n + 4, name, strlen(name));
		n += 4 + BTT_NAME_MAX;
		if (marker) {
			w = BTT_QUAL_MAGIC;
			memcpy(out + n, &w, 4);
			n += 4;
		}
	}
	return n;
}

/* The quality key's value in the preset file of `version`, in the order of that file's own format (versions 4 and 7 to 10
 * are files of the old order: 0 Best, 1 Balanced, 2 Stable; the others are current files), and the BttQuality level the
 * module must make of it. */
static int stored_quality(int version)
{
	switch (version) {
	case 4: case 9:  return 0;                 /* old order: Best */
	case 7:          return 2;                 /* old order: Stable */
	case 8:          return 1;                 /* old order: Balanced */
	case 10:         return 3;                 /* old order: not a value */
	case 11:         return BTT_Q_HIGHEST;
	default:         return BTT_Q_STABLE;
	}
}
static int meant_quality(int version)
{
	switch (version) {
	case 4: case 9: case 10: return BTT_Q_STANDARD;   /* Best is Standard; a value of no order is the default */
	case 8:                  return BTT_Q_BALANCED;
	case 11:                 return BTT_Q_HIGHEST;
	default:                 return BTT_Q_STABLE;     /* 1, 5, 6 as saved; 7 is the old Stable */
	}
}

static void settings_preset(int version)
{
	int vals[BTT_KEY_COUNT + 8];
	unsigned char buf[8 + 4 * (BTT_KEY_COUNT + 8) + 4 + BTT_NAME_MAX + 4 + 64];
	/* a 0.1 file holds 8 keys, one from before the connection quality key 13, from before the newer keys 14 */
	int keys = version == 2 ? BTT_KEY_BAND5 + 1 : version == 3 ? BTT_KEY_CTRL + 1 : version == 4 ? BTT_KEY_QUALITY + 1 :
	           version == 12 ? BTT_KEY_HFP + 1 : BTT_KEY_COUNT;
	memcpy(vals, g_defaults, sizeof(g_defaults));
	vals[BTT_KEY_HFP] = version == 13 ? BTT_HFP_MODE_ON : 0;   /* the internal mode is never read from a file: an old file that says off is not off */
	if (version == 13)
		vals[BTT_KEY_READBATT] = 0;
	vals[BTT_KEY_LOWLAT] = 1;
	vals[BTT_KEY_SOUND] = BTT_SOUND_CUSTOM;
	for (int i = 0; i < 5; i++)
		vals[BTT_KEY_BAND1 + i] = SAVED_BANDS[i];
	if (version == 1 || version >= 5) {
		vals[BTT_KEY_CHANNELS] = BTT_CH_SWAP;
		vals[BTT_KEY_BALANCE] = 6;
		vals[BTT_KEY_VOLCURVE] = BTT_VC_LOUDER;
		vals[BTT_KEY_VOLMAX] = 4;
		vals[BTT_KEY_CTRL] = BTT_CTRL_FAST;
		vals[BTT_KEY_QUALITY] = stored_quality(version);
		vals[BTT_KEY_DISCONNECT] = BTT_DISC_SPEAKER;
		vals[BTT_KEY_RECONNECT] = 1;
		vals[BTT_KEY_AAC_BITRATE] = BTT_AACBR_192;
	} else {
		/* an older file ends before the newer keys: whatever follows in memory must not be read */
		for (int i = keys; i < BTT_KEY_COUNT; i++)
			vals[i] = 99;
		if (keys > BTT_KEY_QUALITY)
			vals[BTT_KEY_QUALITY] = stored_quality(version);   /* version 4: a file of the old order holding Best */
	}
	int n;
	if (version == 5) {   /* a later version: three keys more than this build has, with values nobody here knows */
		for (int i = BTT_KEY_COUNT; i < BTT_KEY_COUNT + 3; i++)
			vals[i] = 77;
		n = build_file(buf, BTT_KEY_COUNT + 3, vals, "FutureName", BTT_KEY_COUNT + 3, 1);
	} else if (version == 6) {
		n = build_file(buf, keys, vals, "bad\tname", keys, 1);
	} else if (version == 7) {
		n = build_file(buf, keys, vals, "IgnoredName", 40, 0);
	} else if (version >= 8 && version <= 10) {
		n = build_file(buf, keys, vals, "SavedName", keys, 0);
	} else {
		n = build_file(buf, keys, vals, version == 1 || version == 11 || version == 12 || version == 13 ? "SavedName" : 0, keys,
		               version == 1 || version == 11 || version == 12 || version == 13);
	}
	put_file(BTT_SETTINGS, buf, n);
}

typedef struct { int enable, sound, lat, band[5], chan, bal, curve, vmax, ctrl, qual, disc, rc, hfp, aptxhd, srconly; unsigned kbps; char name[BTT_NAME_MAX + 2]; } Want;

/* the scenarios whose file carries every key that has a value of its own */
static int full_file(const Scn *s) { return s->saved == 1 || s->saved >= 5; }

/* What the settings are at time `us`, following the scripted changes. */
static void expect(const Scn *s, uint64_t us, Want *w)
{
	w->enable = 1;
	w->sound = s->saved ? BTT_SOUND_CUSTOM : BTT_SOUND_ORIGINAL;
	w->lat = s->saved ? BTT_LAT_LOW : BTT_LAT_STANDARD;
	for (int i = 0; i < 5; i++)
		w->band[i] = s->saved ? SAVED_BANDS[i] : BTT_BAND_FLAT;
	w->chan = full_file(s) ? BTT_CH_SWAP : BTT_CH_STEREO;
	w->bal = full_file(s) ? 6 : BTT_BAL_CENTRE;
	w->curve = full_file(s) ? BTT_VC_LOUDER : BTT_VC_STANDARD;
	w->vmax = full_file(s) ? 4 : 0;
	w->ctrl = full_file(s) ? BTT_CTRL_FAST : BTT_CTRL_STANDARD;
	w->qual = full_file(s) ? meant_quality(s->saved) : BTT_Q_STANDARD;
	w->disc = full_file(s) ? BTT_DISC_SPEAKER : BTT_DISC_MUTE;
	w->rc = full_file(s) ? 1 : 0;
	w->kbps = full_file(s) && s->saved != 12 ? BTT_AACBR_KBPS(BTT_AACBR_192) : BTT_AACBR_KBPS(BTT_AACBR_256);   /* a file of 12 ends before the key */
	w->aptxhd = 1;   /* no file has this key before this version: it is on */
	w->srconly = 1;  /* ... and neither has this one */
	w->hfp = s->saved == 13 ? 0 : BTT_HFP_MODE_ON;      /* the file's Read Headset Battery decides; a file without the key has it on */
	strcpy(w->name, s->saved == 1 || (s->saved >= 8 && s->saved <= 13) ? "SavedName" : s->saved == 5 ? "FutureName" : "");
	if (s->toggle && us >= US(62)) w->qual = BTT_Q_BALANCED;
	if (s->toggle && us >= US(63)) w->qual = BTT_Q_HIGHEST;
	if (s->toggle && us >= US(66)) w->qual = BTT_Q_HIGH;
	if (s->toggle && us >= US(72)) w->qual = BTT_Q_STABLE;
	if (s->toggle && us >= US(60)) { w->lat = BTT_LAT_LOWEST; w->ctrl = BTT_CTRL_FAST; }
	if (s->toggle && us >= US(65)) { w->chan = BTT_CH_MONO; w->bal = 2; }
	if (s->toggle && us >= US(68)) { w->curve = BTT_VC_FINER; w->vmax = 3; }
	if (s->toggle && us >= US(70)) w->sound = BTT_SOUND_BASS;
	if (s->toggle && us >= US(75)) { w->sound = BTT_SOUND_CUSTOM; w->band[0] = 2; w->band[4] = 10; w->aptxhd = 0; w->srconly = 0; }
	if (s->toggle && us >= US(64)) { w->disc = BTT_DISC_PAUSE; w->rc = 1; strcpy(w->name, "My Vita"); }
	if (s->toggle && us >= US(72)) w->disc = BTT_DISC_SPEAKER;
	if (s->toggle && us >= US(72)) w->name[0] = 0;      /* the console's own name spelled out is no name */
	if (s->toggle && us >= US(74)) strcpy(w->name, "Second");
	if (s->toggle && us >= US(66) && us < US(74)) w->hfp = 0;     /* Read Headset Battery off, then back on */
	if (s->toggle && us >= US(67) && us < US(74)) w->hfp = 3;     /* a mode change made directly through BTT_KEY_HFP in between: the mode in force follows it */
	if (s->toggle && us >= US(66)) w->kbps = BTT_AACBR_KBPS(BTT_AACBR_320);
	if (s->toggle && us >= US(80)) w->enable = 0;
	if (s->early == 1 && us >= US(20)) { w->lat = BTT_LAT_LOWEST; w->ctrl = BTT_CTRL_FAST; }
	if (s->early == 1 && us >= US(25) && us < US(30)) w->enable = 0;
	if (s->early == 2 && us < US(EARLY_OFF_UNTIL)) w->enable = 0;
}

static void run(const Scn *s)
{
	S = s;
	g_now = US(s->boot_sec);
	if (s->guard_before)
		put_file(BTT_GUARD_PATH, &s->guard_before, 1);
	if (s->saved)
		settings_preset(s->saved);
	g_guard_truncs = 0;

	CHECK(module_start(0, NULL) == SCE_KERNEL_START_SUCCESS, "module_start");

	if (s->guard_before == '1') {
		/* The last start never finished: nothing runs, and the guard is lifted. */
		CHECK(g_threads_created == 0, "a worker started after a failed start");
		CHECK(g_nfix == 0 && g_neq == 0 && g_nvol == 0 && g_nqual == 0 && g_ndisc == 0 && g_nrc == 0 && g_nnm == 0, "Bluetooth touched after a failed start");
		CHECK(g_aac_find == 0 && g_naac == 0 && g_apt_find == 0 && g_napt == 0 && g_ld_find == 0 && g_nld == 0, "an add-on was looked for or called after a failed start");
		CHECK(guard_first_byte() == '0', "guard not cleared after the safe start");
		CHECK(module_stop(0, NULL) == SCE_KERNEL_STOP_SUCCESS, "module_stop");
		CHECK(g_nfix == 0 && g_neq == 0 && g_nvol == 0 && g_nqual == 0 && g_ndisc == 0 && g_nrc == 0 && g_nnm == 0, "stop after a failed start applied something");
		return;
	}

	CHECK(g_threads_created == 1, "worker not started");
	CHECK(g_apt_find == g_aac_find && g_ld_find == g_aac_find, "the aptX and LDAC modules were looked for (%d, %d) on other passes than the AAC one (%d)", g_apt_find, g_ld_find, g_aac_find);
	CHECK(g_aac_find == g_nfix, "the AAC module was looked for on %d passes, expected one on every pass of the early gate (%d)", g_aac_find, g_nfix);
	CHECK(g_now >= US(s->end_sec), "run ended early at %llu s", (unsigned long long)(g_now / 1000000));
	CHECK(g_guard_clear, "boot guard never cleared");
	CHECK(guard_first_byte() == '0', "guard file not back to '0'");
	CHECK(g_guard_truncs == 0, "the boot guard was truncated (a crash then leaves it empty)");
	/* The calls of the two code constants that came before the late gate are the early ones: every other change was
	 * called exactly on the passes from 45 s, together with the timing fix and latency call of the same pass. */
	int off = 0;
	while (off < g_nfix && g_fix[off].us < US(BTT_MIN_UPTIME))
		off++;
	CHECK(g_nfix > off && g_neq == g_nfix - off && g_nvol == g_neq && g_nqual == g_neq,
	      "the changes were called %d, %d, %d and %d times", g_nfix, g_neq, g_nvol, g_nqual);

	/* The guard clears 5 s after SceShell is first seen, or 30 s after start. */
	uint64_t want_clear = US(s->boot_sec + 30);
	if (s->shell_sec >= 0 && US(s->shell_sec + BTT_GUARD_SHELL_SECS) < want_clear)
		want_clear = US(s->shell_sec + BTT_GUARD_SHELL_SECS);
	CHECK(g_guard_clear_seen_us + 1500000 >= want_clear && g_guard_clear_seen_us <= want_clear + 1500000,
	      "guard cleared at %llu s, expected near %llu s", (unsigned long long)(g_guard_clear_seen_us / 1000000),
	      (unsigned long long)(want_clear / 1000000));

	/* The first call of each gate comes as soon as both of its conditions hold (the worker passes every half second):
	 * the two code constants at max(guard clear, 12 s), everything else at max(guard clear, 45 s). */
	uint64_t early_us = g_guard_clear_seen_us > US(EARLY_SEC) ? g_guard_clear_seen_us : US(EARLY_SEC);
	uint64_t open_us = g_guard_clear_seen_us > US(BTT_MIN_UPTIME) ? g_guard_clear_seen_us : US(BTT_MIN_UPTIME);
	CHECK(g_fix[0].us >= early_us - 1000000 && g_fix[0].us <= early_us + 1500000,
	      "first early call at %llu.%03llu s, expected near %llu s", (unsigned long long)(g_fix[0].us / 1000000),
	      (unsigned long long)(g_fix[0].us / 1000 % 1000), (unsigned long long)(early_us / 1000000));
	CHECK(g_eq[0].us >= open_us - 1000000 && g_eq[0].us <= open_us + 1500000,
	      "first late call at %llu.%03llu s, expected near %llu s", (unsigned long long)(g_eq[0].us / 1000000),
	      (unsigned long long)(g_eq[0].us / 1000 % 1000), (unsigned long long)(open_us / 1000000));

	/* The add-ons' self-tests start from the early gate, not the late one: each asked once (the master switch is on at the gate in every scenario), in the
	 * order AAC, aptX, LDAC, one at a time (the next only once the one before has its verdict, or BTT_SELFTEST_WAIT s after it was asked), and all done before
	 * the late gate when the early one opened early enough. */
	CHECK(g_naac == 1 && g_napt == 1 && g_nld == 1 && g_aac[0].on && g_apt[0].on && g_ld[0].on, "the add-ons' self-tests were asked %d, %d and %d times", g_naac, g_napt, g_nld);
	uint64_t ask_us = s->early == 2 && US(EARLY_OFF_UNTIL) > early_us ? US(EARLY_OFF_UNTIL) : early_us;   /* with the master switch off nothing is asked */
	CHECK(g_aac[0].us >= ask_us - 1000000 && g_aac[0].us <= ask_us + 1500000, "the AAC self-test was asked at %llu.%03llu s, expected near %llu s",
	      (unsigned long long)(g_aac[0].us / 1000000), (unsigned long long)(g_aac[0].us / 1000 % 1000), (unsigned long long)(ask_us / 1000000));
	{
		uint64_t wait = selftest_stuck() ? US(BTT_SELFTEST_WAIT) : AAC_VERDICT_US;

		CHECK(g_apt[0].us >= g_aac[0].us + wait && g_apt[0].us <= g_aac[0].us + wait + 1000000,
		      "the aptX self-test was asked at %llu.%03llu s, the AAC one at %llu.%03llu s (verdict after %llu ms)", (unsigned long long)(g_apt[0].us / 1000000),
		      (unsigned long long)(g_apt[0].us / 1000 % 1000), (unsigned long long)(g_aac[0].us / 1000000), (unsigned long long)(g_aac[0].us / 1000 % 1000),
		      (unsigned long long)(wait / 1000));
		CHECK(g_ld[0].us == g_apt[0].us, "the LDAC self-test was asked at %llu ms, the aptX one (done inside the ask) at %llu ms", (unsigned long long)(g_ld[0].us / 1000),
		      (unsigned long long)(g_apt[0].us / 1000));
		if (!selftest_stuck() && ask_us + US(3) < open_us)   /* the gates are not the same pass */
			CHECK(g_ld[0].us <= ask_us + US(3) && g_ld[0].us < g_eq[0].us, "the self-tests were not all asked before the late gate (LDAC at %llu s, late gate at %llu s)",
			      (unsigned long long)(g_ld[0].us / 1000000), (unsigned long long)(g_eq[0].us / 1000000));
		CHECK(count_in_log("the AAC self-test has no verdict") == (selftest_stuck() ? 1 : 0) && count_in_log("the aptX self-test has no verdict") == 0 &&
		      count_in_log("the LDAC self-test has no verdict") == 0, "the self-test wait was logged wrongly:\n%s", g_klog);
	}

	/* The arguments follow the settings, at every call. */
	int seen[4] = { 0 };
	for (int i = 0; i < g_nfix; i++) {
		Want w;
		int j = i - off;   /* the call of the other changes in the same pass (none before the late gate) */
		int late = g_fix[i].us >= US(BTT_MIN_UPTIME);
		expect(s, g_fix[i].us, &w);
		CHECK(g_fix[i].ts == w.enable && g_fix[i].lat == (w.enable ? w.lat : BTT_LAT_STANDARD) &&
		      g_fix[i].ctrl == (late && w.enable && w.ctrl == BTT_CTRL_FAST),
		      "fix call %d at %llu s: (%d,%d,%d), expected (%d,%d,%d)", i, (unsigned long long)(g_fix[i].us / 1000000),
		      g_fix[i].ts, g_fix[i].lat, g_fix[i].ctrl, w.enable, w.enable ? w.lat : 0, late && w.enable && w.ctrl == BTT_CTRL_FAST);
		if (j >= 0)
			CHECK(g_eq[j].us == g_fix[i].us && g_vol[j].us == g_fix[i].us && g_qual[j].us == g_fix[i].us, "the changes were called at different times");
		else
			continue;
		CHECK(g_qual[j].level == (w.enable ? w.qual : BTT_Q_STANDARD), "quality call %d at %llu s: %d, expected %d", i,
		      (unsigned long long)(g_qual[j].us / 1000000), g_qual[j].level, w.enable ? w.qual : BTT_Q_STANDARD);
		CHECK(g_eq[j].enable == w.enable && g_eq[j].sound == w.sound && g_eq[j].lowlat == w.lat,
		      "eq call %d at %llu s: enable %d sound %d latency %d, expected %d %d %d", i,
		      (unsigned long long)(g_eq[j].us / 1000000), g_eq[j].enable, g_eq[j].sound, g_eq[j].lowlat,
		      w.enable, w.sound, w.lat);
		for (int b = 0; b < 5; b++)
			CHECK(g_eq[j].band[b] == w.band[b], "eq call %d band %d: %d, expected %d", i, b, g_eq[j].band[b], w.band[b]);
		CHECK(g_eq[j].chan == w.chan && g_eq[j].bal == w.bal, "eq call %d channels %d balance %d, expected %d %d", i,
		      g_eq[j].chan, g_eq[j].bal, w.chan, w.bal);
		CHECK(g_vol[j].enable == w.enable && g_vol[j].curve == w.curve && g_vol[j].vmax == w.vmax,
		      "volume call %d: enable %d curve %d max %d, expected %d %d %d", i, g_vol[j].enable, g_vol[j].curve,
		      g_vol[j].vmax, w.enable, w.curve, w.vmax);
		CHECK(g_ndisc == g_neq && g_nrc == g_neq && g_nnm == g_neq, "disconnect, reconnect and name were called %d, %d and %d times", g_ndisc, g_nrc, g_nnm);
		CHECK(g_disc[j].us == g_fix[i].us && g_rc[j].us == g_fix[i].us && g_nm[j].us == g_fix[i].us, "disconnect, reconnect and name were called at other times");
		CHECK(g_disc[j].mode == (apply_hold() ? BTT_DISC_SPEAKER : w.enable ? w.disc : BTT_DISC_MUTE), "disconnect call %d at %llu s: %d, expected %d", i,
		      (unsigned long long)(g_fix[i].us / 1000000), g_disc[j].mode, apply_hold() ? BTT_DISC_SPEAKER : w.enable ? w.disc : BTT_DISC_MUTE);   /* an apply in hand holds the no-mute mode whatever is set */
		CHECK(g_napply == g_neq && g_apply[j].us == g_fix[i].us && g_apply[j].on == w.enable && g_apply[j].kbps == (int)w.kbps && g_apply[j].codec == g_stream[j].codec &&
		      g_apply[j].accel == g_stream[j].accel && g_apply[j].quality == w.qual && g_apply[j].aptx_hd == w.aptxhd,
		      "the apply pass was called with %d, %d, %d kb/s, %d, %d, aptX HD %d at call %d", g_apply[j].on, g_apply[j].codec, g_apply[j].kbps, g_apply[j].accel, g_apply[j].quality, g_apply[j].aptx_hd, i);
		CHECK(g_naptxhd == g_neq && g_aptxhd_log[j] == w.aptxhd, "aptX HD When Available was given %d to the stream at call %d, expected %d", g_aptxhd_log[j], i, w.aptxhd);
		CHECK(g_nsrconly == g_neq && g_srconly_log[j] == w.srconly, "Music Source Only was given %d to the stream at call %d, expected %d", g_srconly_log[j], i, w.srconly);
		CHECK(g_rc[j].on == (w.enable && w.rc), "reconnect call %d at %llu s: %d, expected %d", i,
		      (unsigned long long)(g_fix[i].us / 1000000), g_rc[j].on, w.enable && w.rc);
		CHECK(!strcmp(g_nm[j].name, w.enable ? w.name : ""), "name call %d at %llu s: '%s', expected '%s'", i,
		      (unsigned long long)(g_fix[i].us / 1000000), g_nm[j].name, w.enable ? w.name : "");
		CHECK(g_nhq == g_neq && g_hq[j].us == g_fix[i].us && g_hq[j].level == (w.enable ? w.qual : BTT_Q_STANDARD),
		      "the dual channel settings were called with %d at call %d", g_hq[j].level, i);
		CHECK(g_nhfp == g_neq && g_hfp[j].us == g_fix[i].us && g_hfp[j].mode == (w.enable ? w.hfp : 0),
		      "the headset battery code was called with mode %d at call %d, expected %d", g_hfp[j].mode, i, w.enable ? w.hfp : 0);
		CHECK(g_nstream == g_neq && g_stream[j].us == g_fix[i].us && g_stream[j].on == w.enable &&
		      (g_stream[j].accel == 0 || g_stream[j].accel == 1), "AAC streaming was called with %d, %d, %d at call %d", g_stream[j].on, g_stream[j].codec, g_stream[j].accel, i);
		CHECK(g_nsetaac == g_nfix && g_nlat == g_neq && g_lat_level[j] == (w.enable ? w.lat : BTT_LAT_STANDARD),
		      "the engine's output bound was given latency %d at call %d (expected %d), the codec flag %d times for %d passes", g_lat_level[j], i, w.enable ? w.lat : BTT_LAT_STANDARD, g_nsetaac, g_nfix);
		CHECK(g_nkbps == g_neq && g_kbps_log[j] == w.kbps, "the AAC bit rate was given %u kb/s at call %d, expected %u", g_kbps_log[j], i, w.kbps);
		CHECK(g_ncodecs == g_nlink, "the codec lists are looked at on every pass the link information is (%d, %d)", g_ncodecs, g_nlink);
		CHECK(g_nfall == g_nlink, "the codec fallback notice is looked at on every pass the link information is (%d, %d)", g_nfall, g_nlink);
		CHECK(g_nlink == g_neq && g_link[j].us == g_fix[i].us && g_link[j].on == w.enable && g_link[j].latency == (w.enable ? w.lat : 0),
		      "the link information was called with %d, %d at call %d", g_link[j].on, g_link[j].latency, i);
		CHECK(g_nflush == g_neq && g_flush[j].us == g_fix[i].us && g_flush[j].lowest == (w.enable && w.lat == BTT_LAT_LOWEST),
		      "flush call %d at %llu s: %d, expected %d", i, (unsigned long long)(g_fix[i].us / 1000000), g_flush[j].lowest,
		      w.enable && w.lat == BTT_LAT_LOWEST);
		CHECK(g_nevlog == g_neq && g_evlog[j].us == g_fix[i].us && g_evlog[j].on == w.enable, "the link log was called with %d at call %d (expected %d)", g_evlog[j].on, i, w.enable);
		seen[g_fix[i].us >= US(80) ? 3 : g_fix[i].us >= US(75) ? 2 : g_fix[i].us >= US(70) ? 1 : 0]++;
	}
	if (s->toggle) {   /* each phase was reached, and the refusals were tried */
		CHECK(seen[0] && seen[1] && seen[2] && seen[3], "phases seen %d %d %d %d", seen[0], seen[1], seen[2], seen[3]);
		CHECK(g_refused, "the refusals were not tried");
		CHECK(g_refresh_taken == 1 && !g_refresh_pending, "the Refresh Codecs request reached the apply pass %d times", g_refresh_taken);
		/* the refused second press is logged by the worker (the syscall cannot write the log) */
		CHECK(count_in_log("Refresh Codecs pressed while the last request was in hand: refused (1)") == 1 && count_in_log("start-up period: refused") == 0,
		      "the refused press was logged wrongly:\n%s", g_klog);
		/* the skip reasons: one line each, however often they were reported, and none for a reason never reported */
		CHECK(count_in_log("settings page: entry not added (page too large)") == 1 &&
		      count_in_log("settings page: entry not added (anchor missing)") == 1 &&
		      count_in_log("settings page: entry not added (no memory)") == 1 &&
		      count_in_log("settings page: entry not added (device rows: closing tag missing)") == 1 &&
		      count_in_log("settings page: entry not added (device rows: no room)") == 1 &&
		      count_in_log("settings page: entry not added") == 5, "the page skip reasons were logged wrongly:\n%s", g_klog);
	}
	if (s->early == 1)
		CHECK(count_in_log("Refresh Codecs pressed in the start-up period: refused (1)") == 1, "the press in the start-up period was not logged once:\n%s", g_klog);
	if (s->saved) {
		/* what the file gave is what the first call got (the name included) */
		Want w;
		expect(s, g_eq[0].us, &w);
		CHECK(!strcmp(g_nm[0].name, w.name), "the saved name came as '%s', expected '%s'", g_nm[0].name, w.name);
	}
	if (s->saved)
		CHECK(g_eq[0].sound == BTT_SOUND_CUSTOM && g_eq[0].band[3] == 9, "saved settings not used from the first call");
	if (!s->saved && !s->toggle && !s->early) {
		CHECK(count_in_log("settings page") == 0, "a page skip was logged that nobody reported");
		/* every new setting at its default: the stock arguments, on every call */
		for (int i = 0; i < g_nfix; i++)
			CHECK(g_fix[i].ts == 1 && g_fix[i].lat == BTT_LAT_STANDARD && g_fix[i].ctrl == 0, "a default setting is not the stock argument (fix call %d)", i);
		for (int i = 0; i < g_neq; i++)
			CHECK(g_eq[i].chan == BTT_CH_STEREO &&
			      g_eq[i].bal == BTT_BAL_CENTRE && g_vol[i].curve == BTT_VC_STANDARD && g_vol[i].vmax == 0 && g_qual[i].level == BTT_Q_STANDARD && g_hq[i].level == BTT_Q_STANDARD &&
			      g_disc[i].mode == (apply_hold() ? BTT_DISC_SPEAKER : BTT_DISC_MUTE) && g_rc[i].on == 0 && g_nm[i].name[0] == 0,
			      "a default setting is not the stock argument (call %d)", i);
	}

	/* Changes are saved: the ints by index, then the name's magic and its 16 bytes. */
	if (s->toggle) {
		int buf[2 + BTT_KEY_COUNT + 6];
		int len = get_file(BTT_SETTINGS, buf, sizeof(buf));
		CHECK(len == 8 + 4 * BTT_KEY_COUNT + 4 + BTT_NAME_MAX + 4, "settings not saved (%d bytes)", len);
		CHECK(buf[0] == BTT_MAGIC && buf[1] == BTT_KEY_COUNT, "saved header wrong");
		CHECK(buf[2 + BTT_KEY_COUNT] == BTT_NAME_MAGIC && !memcmp(buf + 3 + BTT_KEY_COUNT, "Second\0\0\0\0\0\0\0\0\0\0", BTT_NAME_MAX),
		      "the saved name is wrong");
		CHECK(buf[3 + BTT_KEY_COUNT + BTT_NAME_MAX / 4] == BTT_QUAL_MAGIC, "the saved file has no marker of the quality order");
		CHECK(buf[2 + BTT_KEY_DISCONNECT] == BTT_DISC_SPEAKER && buf[2 + BTT_KEY_RECONNECT] == 1, "the saved disconnect or reconnect wrong");
		CHECK(buf[2 + BTT_KEY_LOWBATT] == 1 && buf[2 + BTT_KEY_HFP] == BTT_HFP_MODE_ON, "the saved battery keys wrong");
		CHECK(buf[2 + BTT_KEY_CODEC] == BTT_CODEC_AAC && buf[2 + BTT_KEY_AAC_ACCEL] == 0 && buf[2 + BTT_KEY_AAC_BITRATE] == BTT_AACBR_320,
		      "the saved AAC keys wrong");
		CHECK(buf[2 + BTT_KEY_APTX_HD] == 0, "the saved aptX HD When Available wrong (%d)", buf[2 + BTT_KEY_APTX_HD]);
		CHECK(buf[2 + BTT_KEY_SOURCE_ONLY] == 0, "the saved Music Source Only wrong (%d)", buf[2 + BTT_KEY_SOURCE_ONLY]);
		CHECK(buf[2 + BTT_KEY_ENABLE] == 0 && buf[2 + BTT_KEY_LOWLAT] == 2 && buf[2 + BTT_KEY_SOUND] == BTT_SOUND_CUSTOM &&
		      buf[2 + BTT_KEY_BAND1] == 2 && buf[2 + BTT_KEY_BAND5] == 10 && buf[2 + BTT_KEY_CHANNELS] == BTT_CH_MONO &&
		      buf[2 + BTT_KEY_BALANCE] == 2 && buf[2 + BTT_KEY_VOLCURVE] == BTT_VC_FINER && buf[2 + BTT_KEY_VOLMAX] == 3 &&
		      buf[2 + BTT_KEY_CTRL] == BTT_CTRL_FAST && buf[2 + BTT_KEY_QUALITY] == BTT_Q_STABLE,
		      "saved settings wrong (enable %d latency %d sound %d)", buf[2 + BTT_KEY_ENABLE],
		      buf[2 + BTT_KEY_LOWLAT], buf[2 + BTT_KEY_SOUND]);
	}

	/* Stop: every change restored, once, after the last pass. */
	CHECK(g_fix_restore == 0 && g_eq_restore == 0 && g_vol_restore == 0 && g_qual_restore == 0 && g_disc_restore == 0 &&
	      g_rc_restore == 0 && g_nm_restore == 0, "restored before the stop");
	CHECK(module_stop(0, NULL) == SCE_KERNEL_STOP_SUCCESS, "module_stop");
	CHECK(g_fix_restore == 1 && g_eq_restore == 1 && g_vol_restore == 1 && g_qual_restore == 1, "not restored at stop (%d, %d, %d, %d)",
	      g_fix_restore, g_eq_restore, g_vol_restore, g_qual_restore);
	CHECK(g_disc_restore == 1 && g_rc_restore == 1 && g_nm_restore == 1, "disconnect, reconnect or name not restored at stop (%d, %d, %d)",
	      g_disc_restore, g_rc_restore, g_nm_restore);
	CHECK(g_hq_restore == 1, "High and Highest were not restored at stop");
	CHECK(g_flush_restore == 1, "the flush timeout was not restored at stop");
	CHECK(g_evlog_restore == 1, "the link log was not stopped at stop");
	CHECK(g_hfp_restore == 1, "the headset battery code was not restored at stop");
	CHECK(g_stream_restore == 1 && g_stream_restore_first, "AAC streaming was not restored once, before every other change, at stop");
	CHECK(g_stream_wait_fast > 0 || S->end_sec < 72, "the worker never looked every 0.1 s while an AAC stream was configured");
	CHECK(g_evlog_wait_fast > 0 || S->end_sec < 86, "the worker never looked every 0.1 s while a link was new");
	if (s->saved == 12)
		CHECK(bttGet(BTT_KEY_LOWBATT) == 1 && bttGet(BTT_KEY_READBATT) == 1 && bttGet(BTT_KEY_HFP) == BTT_HFP_MODE_ON,
		      "the keys a file from an earlier build lacks or holds as off are not on");
	if (s->saved == 13)
		CHECK(bttGet(BTT_KEY_READBATT) == 0 && bttGet(BTT_KEY_HFP) == 0, "a saved Read Headset Battery off was not loaded");
	CHECK(g_restore_order > g_last_sync_order, "restored before the last pass");
}

int main(void)
{
	static const Scn scn[] = {
		/* name                              boot shell end guard saved toggle early */
		{ "Home screen found at 12 s",         10,   12, 120,  0,   0,    1,  0 },
		{ "Home screen never found",            5,   -1, 100,  0,   0,    0,  0 },
		{ "started late, guard clears at 60",  30,   -1, 120,  0,   0,    0,  0 },
		{ "Home screen found at 50 s",          3,   50, 100,  0,   0,    0,  0 },
		{ "saved settings",                     3,    5,  90,  0,   1,    0,  0 },
		{ "saved settings, then changes",       8,   10, 120,  0,   1,    1,  0 },
		{ "a 0.1 settings file (8 keys)",       3,    5,  90,  0,   2,    0,  0 },
		{ "a file from before the quality key", 3,    5,  90,  0,   3,    0,  0 },
		{ "a file from before the name",        3,    5,  90,  0,   4,    0,  0 },
		{ "a file from a later version",        3,    5,  90,  0,   5,    0,  0 },
		{ "a saved name that is not valid",     3,    5,  90,  0,   6,    0,  0 },
		{ "a file claiming 40 keys",            3,    5,  90,  0,   7,    0,  0 },
		{ "old quality order: Balanced",        3,    5,  90,  0,   8,    0,  0 },
		{ "old quality order: Best is Standard", 3,   5,  90,  0,   9,    0,  0 },
		{ "old quality order: 3 is no value",   3,    5,  90,  0,  10,    0,  0 },
		{ "current file holding Highest",       3,    5,  90,  0,  11,    0,  0 },
		{ "a file from an earlier build, before the low battery key", 3, 5, 90, 0, 12,  0,  0 },
		{ "a file with Read Headset Battery off", 3,  5,  90,  0,  13,    0,  0 },
		{ "boot guard '1'",                     3,    5,  90, '1',  0,    0,  0 },
		/* the two code constants at max(guard clear, 12 s), settings changed between the gates, the rest at 45 s */
		{ "early: changes between the gates", 3,    5, 100,  0,   0,    0, 1 },
		{ "early: Home screen found at 6 s",    2,    6,  80,  0,   1,    0,  0 },
		{ "early: Home screen found at 30 s",   3,   30,  90,  0,   0,    0,  0 },
		{ "early: Home screen found at 41 s",   3,   41,  90,  0,   0,    0,  0 },
		{ "early: no Home screen, guard at 33 s", 3,  -1,  90,  0,   0,    0,  0 },
		{ "early: Bluetooth Turbo off until 20 s", 3, 5,  90,  0,   0,    0,  2 },
	};
	int fails = 0, n = (int)(sizeof(scn) / sizeof(scn[0]));

	for (int i = 0; i < n; i++) {
		fflush(stdout);
		pid_t p = fork();
		if (p == 0) {
			run(&scn[i]);
			exit(0);
		}
		int st = 0;
		waitpid(p, &st, 0);
		int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
		printf("%s %s\n", ok ? "ok  " : "FAIL", scn[i].name);
		fails += !ok;
	}
	printf("test_worker: %d scenarios, %d failed\n", n, fails);
	return fails ? 1 : 0;
}
