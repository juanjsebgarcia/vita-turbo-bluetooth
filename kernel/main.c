/*
 * main.c - Bluetooth Turbo kernel module (*KERNEL in config.txt).
 *
 * Owns the settings and the changes to the system's Bluetooth code:
 *   - loads ur0:data/bluetoothturbo/settings.bin at start-up, saves on change;
 *   - exports the SceBluetoothTurbo syscalls (bttGet/bttSet) for the Settings page;
 *   - a worker thread applies the settings in two steps. The two code constants, the timing fix and the
 *     send queue's limit (the latency, btfix.c), go in once the Home screen is up and BTT_EARLY_UPTIME
 *     seconds of uptime have passed. Everything else goes in once BTT_MIN_UPTIME seconds have passed
 *     (and the Home screen is up): the controller response (btfix.c), the sound filter, channels and balance on
 *     the audio sent to Bluetooth headphones (bteq.c) and the volume curve and maximum
 *     (btvol.c), the highest bitpool of the next connection (btqual.c), what the console does when
 *     headphones disconnect (btdisc.c), the reconnecting of headphones after standby (btreconn.c),
 *     the Bluetooth name (btname.c) and the reading of the headset's battery (bthfp_mod.c: on while Bluetooth
 *     Turbo is on and Read Headset Battery is on). Each is taken out again when switched off and at module stop;
 *   - looks for the optional AAC module (bluetoothturbo_aac.skprx) and, from the early gate, asks it for its self-test (the aptX and LDAC add-ons' too, one at a time: selftest_pass); without it nothing of AAC
 *     exists (btaac_link.c); bttGet(BTT_KEY_AAC_AVAIL) says what the Settings page may offer; with it, and the Bluetooth Codec key at AAC, the
 *     next headphones that offer AAC are sent AAC (btaac_stream.c); bttGetLinkInfo says what the audio link is now (btlink.c);
 *   - a boot guard: ur0:data/bluetoothturbo/boot_guard holds '1' from start-up until SceShell
 *     has been up for BTT_GUARD_SHELL_SECS (or the module has run for BTT_GUARD_SECS), then '0'.
 *     Finding '1' at start means the last start never got that far, so this start stands
 *     aside entirely (nothing else is affected: every module has its own guard).
 *
 * The module touches only the Bluetooth code, the audio sender's Bluetooth hand-offs (the audio blocks,
 * the headphones' volume number, the start of sound on a device) and two instructions of the audio
 * policy that handle Bluetooth headphones going away. It never calls the audio chip, the sound
 * driver or the wired volume tables, and it reads nothing of any other plugin's: it works alone
 * and beside the others in either load order (tools/check_coexistence.py).
 *
 * Nothing is written to flash. With the plugin removed, or "Bluetooth Turbo" off, the sender
 * runs exactly as stock.
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/io/stat.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "fwlog.h"
#include "shell.h"
#include "btfix.h"
#include "bteq.h"
#include "btvol.h"
#include "btqual.h"
#include "btdisc.h"
#include "btname.h"
#include "btreconn.h"
#include "bthq.h"
#include "btflush.h"
#include "btevlog.h"
#include "bthfp_mod.h"
#include "btbatt.h"
#include "btaac_link.h"
#include "btaptx_link.h"
#include "btldac_link.h"
#include "btaac_stream.h"
#include "btlink.h"
#include "btfall.h"
#include "btcdcfile.h"
#include "btapply.h"
#include "klog.h"

#define BTT_CHECK_MS        500
#define BTT_FAST_MS         100   /* the worker's period while an AAC stream is configured (btaac_stream.h) */
#define BTT_SAVE_DELAY_MS   1000

#define BTT_GUARD_PATH        BTT_DIR "/boot_guard"
#define BTT_GUARD_SECS        30   /* clear the guard after this long, whatever happens */
#define BTT_GUARD_SHELL_SECS  5    /* ... or this long after SceShell has started       */

/* Nothing that installs a hook or injects code the Bluetooth stack runs is applied before about 45 s of
 * uptime and the Home screen: shared hardware touched earlier than that brought a console down in tests of
 * another plugin (the audio chip), and the rule was kept for the Bluetooth sender, whatever is touched. */
#define BTT_MIN_UPTIME      45

/* The two code constants (the timing fix and the send queue's limit: one byte-checked write each, no hook, no
 * code of ours run by the stack) go in earlier, so a headset that reconnects at power-on plays from the start.
 * Measured on a console: Wi-Fi Turbo writes the shared radio chip from 7.9 s and has finished its
 * early writes at 9.0 s, on every boot, without trouble; 12 s is a few seconds after that. The boot guard
 * still has to be clear (the Home screen up) as well. */
#define BTT_EARLY_UPTIME    12

/* The add-ons' start-up self-tests (AAC, aptX, LDAC) are not Bluetooth changes: each runs its own encoder on its own data, with its own crash record, and
 * touches no Bluetooth code and no shared hardware (their imports are the thread, file and memory managers and the C library only, see CMakeLists.txt).
 * They start from the early gate, one at a time, so that a headset that reconnects at power-on finds the codecs ready as soon as the hooks go in. One that
 * has not given its verdict in this many seconds is left to finish, and the next is started. */
#define BTT_SELFTEST_WAIT   10

#define EV_WAKE   0x1
#define EV_STOP   0x2

/* settings.bin: magic, count, then `count` ints (one per key, by index), then the Bluetooth name:
 * BTT_NAME_MAGIC and BTT_NAME_MAX bytes of text, NUL padded, then BTT_QUAL_MAGIC. A file is read by its own count, so an
 * older file (fewer keys, no name) still loads, with the newer settings at their defaults, and an older module reading a
 * newer file stops at the ints it knows (it never reads past what it asked for). A file without BTT_QUAL_MAGIC was written
 * before Bluetooth Audio Quality took its order (0 Best, 1 Balanced, 2 Stable): its quality value is converted when it is
 * loaded (0 to Standard, 1 to Balanced, 2 to Stable; anything else is no value and stays the default). */
#define BTT_MAGIC       0x31545442   /* "BTT1" */
#define BTT_NAME_MAGIC  0x314E5442   /* "BTN1" */
#define BTT_QUAL_MAGIC  0x32515442   /* "BTQ2": the file holds the quality key in the order of enum BttQuality */
#define BTT_FILE_KEYS_MAX  32        /* a file claiming more keys than this is read as far as the ints, no name */

static const int g_defaults[BTT_KEY_COUNT] = {
	[BTT_KEY_ENABLE]  = 1,
	[BTT_KEY_LOWLAT]  = 0,
	[BTT_KEY_SOUND]   = BTT_SOUND_ORIGINAL,
	[BTT_KEY_BAND1]   = BTT_BAND_FLAT,
	[BTT_KEY_BAND2]   = BTT_BAND_FLAT,
	[BTT_KEY_BAND3]   = BTT_BAND_FLAT,
	[BTT_KEY_BAND4]   = BTT_BAND_FLAT,
	[BTT_KEY_BAND5]   = BTT_BAND_FLAT,
	[BTT_KEY_CHANNELS] = BTT_CH_STEREO,
	[BTT_KEY_BALANCE] = BTT_BAL_CENTRE,
	[BTT_KEY_VOLCURVE] = BTT_VC_STANDARD,
	[BTT_KEY_VOLMAX]  = 0,
	[BTT_KEY_CTRL]    = BTT_CTRL_STANDARD,
	[BTT_KEY_QUALITY] = BTT_Q_STANDARD,
	[BTT_KEY_DISCONNECT] = BTT_DISC_MUTE,
	[BTT_KEY_RECONNECT] = 0,
	[BTT_KEY_HFP]     = BTT_HFP_MODE_ON,
	[BTT_KEY_LOWBATT] = 1,
	[BTT_KEY_READBATT] = 1,
	[BTT_KEY_CODEC]   = BTT_CODEC_SBC,
	[BTT_KEY_AAC_ACCEL] = 1,
	[BTT_KEY_AAC_BITRATE] = BTT_AACBR_256,
	[BTT_KEY_LDAC_RATE] = BTT_LDACBR_330,
	[BTT_KEY_APTX_HD] = 1,
	[BTT_KEY_SOURCE_ONLY] = 1,
};

static const int g_max[BTT_KEY_COUNT] = {
	[BTT_KEY_ENABLE] = 1, [BTT_KEY_LOWLAT] = BTT_LAT_COUNT - 1, [BTT_KEY_SOUND] = BTT_SOUND_COUNT - 1,
	[BTT_KEY_BAND1] = BTT_BAND_STEPS - 1, [BTT_KEY_BAND2] = BTT_BAND_STEPS - 1,
	[BTT_KEY_BAND3] = BTT_BAND_STEPS - 1, [BTT_KEY_BAND4] = BTT_BAND_STEPS - 1,
	[BTT_KEY_BAND5] = BTT_BAND_STEPS - 1,
	[BTT_KEY_CHANNELS] = BTT_CH_COUNT - 1, [BTT_KEY_BALANCE] = BTT_BAL_STEPS - 1,
	[BTT_KEY_VOLCURVE] = BTT_VC_COUNT - 1, [BTT_KEY_VOLMAX] = BTT_VOLMAX_COUNT - 1,
	[BTT_KEY_CTRL] = BTT_CTRL_COUNT - 1, [BTT_KEY_QUALITY] = BTT_Q_COUNT - 1,
	[BTT_KEY_DISCONNECT] = BTT_DISC_COUNT - 1, [BTT_KEY_RECONNECT] = 1,
	[BTT_KEY_HFP] = BTT_HFP_MODE_MAX, [BTT_KEY_LOWBATT] = 1, [BTT_KEY_READBATT] = 1,
	[BTT_KEY_CODEC] = BTT_CODEC_COUNT - 1, [BTT_KEY_AAC_ACCEL] = 1,
	[BTT_KEY_AAC_BITRATE] = BTT_AACBR_COUNT - 1,
	[BTT_KEY_LDAC_RATE] = BTT_LDACBR_COUNT - 1,
	[BTT_KEY_APTX_HD] = 1,
	[BTT_KEY_SOURCE_ONLY] = 1,
};

static int    g_set[BTT_KEY_COUNT];
static char   g_name[BTT_NAME_MAX + 1];     /* the Bluetooth name set ("" = the console's own); the worker copies it */
static volatile int g_dirty;
static volatile unsigned g_refresh_early, g_refresh_busy;   /* Refresh Codecs presses refused in the start-up period / while a request was in hand: the worker logs them */
static volatile int g_startup_over;      /* the worker's passes have reached BTT_MIN_UPTIME: changes may go in (bttSet's Refresh Codecs waits for it) */
static volatile int g_settings_ver;     /* BTT_VER_NUM the Settings module reported, 0 = none yet */
static volatile unsigned g_page_skip;   /* bit n set: the Settings module reported BttPageSkip reason n */
static SceUID g_ev = -1, g_thread = -1;

/* ---- settings file ------------------------------------------------------ */

static uint32_t file_u32(const unsigned char *p)
{
	uint32_t v;

	memcpy(&v, p, 4);
	return v;
}

static void settings_load(void)
{
	/* header, the most keys a file may hold, then the name */
	unsigned char buf[8 + 4 * BTT_FILE_KEYS_MAX + 4 + BTT_NAME_MAX + 4];
	memcpy(g_set, g_defaults, sizeof(g_set));
	memset(g_name, 0, sizeof(g_name));
	SceUID fd = ksceIoOpen(BTT_SETTINGS, SCE_O_RDONLY, 0);
	if (fd < 0) {
		BTT_KLOG("settings: none saved, defaults");
		return;
	}
	int n = ksceIoRead(fd, buf, sizeof(buf));
	ksceIoClose(fd);
	if (n < 8 || file_u32(buf) != BTT_MAGIC) {
		BTT_KLOG("settings: bad file (%d bytes), defaults", n);
		return;
	}
	uint32_t file_keys = file_u32(buf + 4);
	int count = file_keys < BTT_KEY_COUNT ? (int)file_keys : BTT_KEY_COUNT;
	int name_off = 8 + 4 * (int)file_keys;
	int new_order = file_keys <= BTT_FILE_KEYS_MAX && n >= name_off + 4 + BTT_NAME_MAX + 4 &&
	                file_u32(buf + name_off + 4 + BTT_NAME_MAX) == BTT_QUAL_MAGIC;
	for (int i = 0; i < count && 8 + 4 * (i + 1) <= n; i++) {
		int v = (int)file_u32(buf + 8 + 4 * i);
		if (i == BTT_KEY_QUALITY && !new_order) {
			static const int k_old_quality[3] = { BTT_Q_STANDARD, BTT_Q_BALANCED, BTT_Q_STABLE };

			v = v >= 0 && v < 3 ? k_old_quality[v] : -1;
		}
		if (v >= 0 && v <= g_max[i])
			g_set[i] = v;
	}
	/* The name follows the ints the file itself holds. */
	if (file_keys <= BTT_FILE_KEYS_MAX) {
		int off = 8 + 4 * (int)file_keys;
		if (n >= off + 4 + BTT_NAME_MAX && file_u32(buf + off) == BTT_NAME_MAGIC) {
			memcpy(g_name, buf + off + 4, BTT_NAME_MAX);
			g_name[BTT_NAME_MAX] = 0;
			if (!btt_name_valid(g_name)) {
				BTT_KLOG("settings: the saved Bluetooth name is not valid - the console's own");
				memset(g_name, 0, sizeof(g_name));
			}
		}
	}
	/* the internal mode lives in memory only: every start takes it from Read Headset Battery (a file without that key has it on) */
	g_set[BTT_KEY_HFP] = g_set[BTT_KEY_READBATT] ? BTT_HFP_MODE_ON : 0;
	BTT_KLOG("settings: loaded %d keys%s", count, g_name[0] ? " and a Bluetooth name" : "");
}

static void name_copy(char *out)
{
	/* the name as the syscall last stored it; read byte by byte through volatile twice, so a half-written one is
	 * not used (the compiler may not merge the reads) */
	const volatile char *src = g_name;
	char again[BTT_NAME_MAX + 1];

	for (int i = 0; i < 3; i++) {
		for (int k = 0; k <= BTT_NAME_MAX; k++)
			out[k] = src[k];
		for (int k = 0; k <= BTT_NAME_MAX; k++)
			again[k] = src[k];
		if (!memcmp(out, again, sizeof(again)))
			break;
	}
	out[BTT_NAME_MAX] = 0;
}

static void settings_save(void)
{
	unsigned char buf[8 + 4 * BTT_KEY_COUNT + 4 + BTT_NAME_MAX + 4];
	uint32_t w;
	char name[BTT_NAME_MAX + 1];

	w = BTT_MAGIC;
	memcpy(buf, &w, 4);
	w = BTT_KEY_COUNT;
	memcpy(buf + 4, &w, 4);
	memcpy(buf + 8, g_set, sizeof(g_set));
	w = BTT_NAME_MAGIC;
	memcpy(buf + 8 + sizeof(g_set), &w, 4);
	name_copy(name);
	memset(buf + 12 + sizeof(g_set), 0, BTT_NAME_MAX);
	memcpy(buf + 12 + sizeof(g_set), name, strlen(name));
	w = BTT_QUAL_MAGIC;
	memcpy(buf + 12 + sizeof(g_set) + BTT_NAME_MAX, &w, 4);
	ksceIoMkdir(BTT_DIR, 0777);
	SceUID fd = ksceIoOpen(BTT_SETTINGS, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0) {
		BTT_KLOG("settings: save failed 0x%08x", (unsigned)fd);
		return;
	}
	ksceIoWrite(fd, buf, sizeof(buf));
	ksceIoClose(fd);
	BTT_KLOG("settings: saved");
}

/* ---- boot guard --------------------------------------------------------- */

static char g_guard_txt[96];            /* last content read, for the log          */
static volatile int g_guard_clear;      /* this start has reached the Home screen   */

/* First byte of the guard file, or 0 if there is none. */
static char guard_read(void)
{
	memset(g_guard_txt, 0, sizeof(g_guard_txt));
	SceUID fd = ksceIoOpen(BTT_GUARD_PATH, SCE_O_RDONLY, 0);
	if (fd >= 0) {
		ksceIoRead(fd, g_guard_txt, sizeof(g_guard_txt) - 1);
		ksceIoClose(fd);
	}
	for (int i = 0; g_guard_txt[i]; i++)
		if (g_guard_txt[i] < ' ' || g_guard_txt[i] > '~')
			g_guard_txt[i] = '.';
	for (int i = (int)strlen(g_guard_txt) - 1; i >= 0 &&
	     (g_guard_txt[i] == ' ' || g_guard_txt[i] == '.'); i--)
		g_guard_txt[i] = 0;   /* the padding and newline, for the log */
	return g_guard_txt[0];
}

static void guard_write(char c, const char *who)
{
	char buf[112];
	int n = 0;
	buf[n++] = c;
	n += snprintf(buf + n, sizeof(buf) - n, " up=%u by=%s\n",
	              (unsigned)(ksceKernelGetSystemTimeWide() / 1000000), who);
	/* Overwritten in place at a fixed length, never truncated: a crash between
	 * a truncate and the write would leave an empty file, which reads as "no
	 * guard". The first byte is the state. */
	if (n > (int)sizeof(buf) - 1)
		n = (int)sizeof(buf) - 1;
	while (n < (int)sizeof(buf) - 1)
		buf[n++] = ' ';
	buf[n++] = '\n';
	ksceIoMkdir(BTT_DIR, 0777);
	SceUID fd = ksceIoOpen(BTT_GUARD_PATH, SCE_O_WRONLY | SCE_O_CREAT, 0666);
	if (fd >= 0) {
		ksceIoWrite(fd, buf, n);
		ksceIoClose(fd);
	}
}

/* ---- syscalls (SceBluetoothTurbo) --------------------------------------- */

int bttGet(int key)
{
	if (key == BTT_KEY_LDAC_AVAIL)
		return btt_ldac_avail();   /* read only: what the Settings page may offer of LDAC (0 without the LDAC module) */
	if (key == BTT_KEY_APTX_AVAIL)
		return btt_aptx_avail();   /* read only: what the Settings page may offer of aptX (0 without the aptX module) */
	if (key == BTT_KEY_AAC_AVAIL)
		return btt_aac_avail();   /* read only: what the Settings page may offer of AAC (0 without the AAC module) */
	if (key >= 0 && key < BTT_KEY_COUNT)
		return g_set[key];
	return BTT_ERR_ARG;
}

int bttSet(int key, int value)
{
	if (key == BTT_KEY_SETTINGS_VER) {
		/* Only remembered here (syscall context, on the Settings app's
		 * thread): the worker logs it, so the Settings side does no file I/O. */
		if (value <= 0)
			return BTT_ERR_RANGE;
		g_settings_ver = value;
		if (g_ev >= 0)
			ksceKernelSetEventFlag(g_ev, EV_WAKE);
		return BTT_OK;
	}
	if (key == BTT_KEY_REFRESH_CODECS) {
		/* Write only, not stored: the Refresh Codecs button. Only a flag for the worker's next pass (btapply.c), which makes the guards that need the
		 * link (the list known, a call, the master switch) and logs why it declines; here the two that need no reading. */
		int r;

		if (value != 1)
			return BTT_ERR_RANGE;
		if (!g_startup_over) {
			g_refresh_early++;
			r = BTT_ERR_BUSY;
		} else {
			r = btt_btapply_request_refresh();
			if (r != BTT_OK)
				g_refresh_busy++;
		}
		if (g_ev >= 0)
			ksceKernelSetEventFlag(g_ev, EV_WAKE);   /* an accepted request is taken by the next pass; a refused one is logged by it */
		return r;
	}
	if (key == BTT_KEY_PAGE_SKIP) {
		/* Remembered only, like the version: the worker logs it. */
		if (value < BTT_SKIP_ANCHOR || value >= BTT_SKIP_COUNT)
			return BTT_ERR_RANGE;
		g_page_skip |= 1u << value;
		if (g_ev >= 0)
			ksceKernelSetEventFlag(g_ev, EV_WAKE);
		return BTT_OK;
	}
	if (key < 0 || key >= BTT_KEY_COUNT)
		return BTT_ERR_ARG;
	if (value < 0 || value > g_max[key])
		return BTT_ERR_RANGE;
	g_set[key] = value;
	if (key == BTT_KEY_READBATT)
		g_set[BTT_KEY_HFP] = value ? BTT_HFP_MODE_ON : 0;   /* the worker's next pass puts the mode in force; the master switch still gates it */
	g_dirty = 1;
	if (g_ev >= 0)
		ksceKernelSetEventFlag(g_ev, EV_WAKE);
	return BTT_OK;
}

/* The Bluetooth name. The Settings page passes its own memory: copied in and out with the system's checked
 * copies, never touched through the pointer. */
int bttGetName(char *out, int size)
{
	char k[BTT_NAME_MAX + 1];
	int n;

	if (!out || size < 1)
		return BTT_ERR_ARG;
	name_copy(k);
	if (!k[0])
		memcpy(k, BTT_NAME_DEFAULT, sizeof(BTT_NAME_DEFAULT));
	n = (int)strlen(k);
	if (n > size - 1)
		n = size - 1;
	k[n] = 0;
	if (ksceKernelMemcpyKernelToUser(out, k, (size_t)n + 1) < 0)
		return BTT_ERR_ARG;
	return n;
}

int bttSetName(const char *name)
{
	/* one byte more than the longest name, so a longer one arrives without a NUL and is refused */
	char k[BTT_NAME_MAX + 2];

	if (!name)
		return BTT_ERR_ARG;
	memset(k, 0, sizeof(k));
	if (ksceKernelStrncpyUserToKernel(k, name, BTT_NAME_MAX + 1) < 0)
		return BTT_ERR_ARG;
	k[BTT_NAME_MAX + 1] = 0;
	if (!btt_name_valid(k))
		return BTT_ERR_RANGE;
	/* an empty name or the console's own name is "none": nothing to inject, nothing different to show */
	if (!strcmp(k, BTT_NAME_DEFAULT))
		k[0] = 0;
	memcpy(g_name, k, sizeof(g_name));
	g_dirty = 1;
	if (g_ev >= 0)
		ksceKernelSetEventFlag(g_ev, EV_WAKE);
	return BTT_OK;
}

/* ---- worker ---------------------------------------------------------------- */

/* Once a second: look for SceShell, and clear the boot guard when this start has
 * reached the Home screen (SceShell up for BTT_GUARD_SHELL_SECS) or has simply
 * run for BTT_GUARD_SECS. */
static void boot_tick(unsigned up)
{
	static unsigned shell_at;   /* uptime second SceShell was first seen, +1 */

	if (btt_shell_up() && !shell_at)
		shell_at = up + 1;
	if (g_guard_clear)
		return;
	if (shell_at && up >= shell_at - 1 + BTT_GUARD_SHELL_SECS) {
		guard_write('0', "shell");
		g_guard_clear = 1;
		BTT_KLOG("SceShell up for %us - boot_guard set to 0", (unsigned)BTT_GUARD_SHELL_SECS);
	} else if (up >= BTT_GUARD_SECS) {
		guard_write('0', "worker");
		g_guard_clear = 1;
		BTT_KLOG("stable for %us - boot_guard set to 0", (unsigned)BTT_GUARD_SECS);
	}
}

/* Logs the version the Settings module reported (the app starts a new copy each
 * time it opens: only a different one is logged again). */
static void settings_ver_log(void)
{
	static int logged;
	int v = g_settings_ver;
	if (!v || v == logged)
		return;
	logged = v;
	if (v == BTT_VER_NUM)
		BTT_KLOG("settings module %d.%d.%d", v >> 16, (v >> 8) & 0xFF, v & 0xFF);
	else
		BTT_KLOG("settings module %d.%d.%d does not match the kernel module " BTT_VERSION,
		         v >> 16, (v >> 8) & 0xFF, v & 0xFF);
}

/* Logs, once per reason, that the Settings module saw the Devices page (or a device's page) and did not add its entry or rows (the page's
 * wording changed, it was too big, or no memory was left for the copy), so a missing entry has a line in the log. */
static void page_skip_log(void)
{
	static unsigned logged;
	static const char *const why[BTT_SKIP_COUNT] = { 0, "anchor missing", "page too large", "no memory", "device rows: closing tag missing", "device rows: no room" };
	unsigned now = g_page_skip, fresh = now & ~logged;

	for (int r = BTT_SKIP_ANCHOR; fresh && r < BTT_SKIP_COUNT; r++)
		if (fresh & (1u << r))
			BTT_KLOG("settings page: entry not added (%s)", why[r]);
	logged |= now;
}

/* Logs the Refresh Codecs presses that bttSet refused (it cannot write the log itself: it runs on the Settings app's thread), once per press. */
static void refresh_refused_log(void)
{
	static unsigned logged_early, logged_busy;
	unsigned early = g_refresh_early, busy = g_refresh_busy;

	if (early != logged_early) {
		BTT_KLOG("bluetooth apply: Refresh Codecs pressed in the start-up period: refused (%u)", early - logged_early);
		logged_early = early;
	}
	if (busy != logged_busy) {
		BTT_KLOG("bluetooth apply: Refresh Codecs pressed while the last request was in hand: refused (%u)", busy - logged_busy);
		logged_busy = busy;
	}
}

/* The add-ons' self-tests, from the early gate, one at a time in the order AAC, aptX, LDAC: the next is asked once the one before has its verdict (or
 * BTT_SELFTEST_WAIT seconds after it was asked). Only while Bluetooth Turbo is on (the add-ons ignore the call otherwise, and the pass asks again when it
 * comes on). Without an add-on its step is over at once. */
static void selftest_pass(int on, int codec, unsigned up)
{
	static void (*const sync[3])(int, int) = { btt_aac_link_sync, btt_aptx_link_sync, btt_ldac_link_sync };
	static int (*const done[3])(void) = { btt_aac_selftest_done, btt_aptx_selftest_done, btt_ldac_selftest_done };
	static const char *const name[3] = { "AAC", "aptX", "LDAC" };
	static int stage, asked;
	static unsigned asked_at;

	if (!on)
		return;
	while (stage < 3) {
		if (!asked) {
			asked = 1;
			asked_at = up;
			sync[stage](on, codec);
		}
		if (!done[stage]()) {
			if (up - asked_at < BTT_SELFTEST_WAIT)
				return;
			BTT_KLOG("bluetooth: the %s self-test has no verdict after %us - the next one is started", name[stage], (unsigned)BTT_SELFTEST_WAIT);
		}
		stage++;
		asked = 0;
	}
}

/* The Bluetooth changes, in two steps (the Home screen up, the boot guard clear, is needed for both).
 * From BTT_EARLY_UPTIME seconds: the timing fix whenever Bluetooth Turbo is on and the latency level, the
 * two code constants. From BTT_MIN_UPTIME seconds: the controller response (the same call, which is given
 * "standard" until then), the sound preset, channels and balance on the audio sent to the headphones
 * (bteq.c), the volume curve and maximum (btvol.c) and the connection quality (btqual.c), what happens when
 * headphones disconnect (btdisc.c), the reconnecting after standby (btreconn.c), the Bluetooth name
 * (btname.c): the start-up rule is about not touching the system early. The add-ons' self-tests (selftest_pass) touch nothing of the system and start
 * from the early gate.
 * Every setting is forced to its default when Bluetooth Turbo is off. */
static void bt_pass(void)
{
	static int logged_early, logged;
	unsigned up = (unsigned)(ksceKernelGetSystemTimeWide() / 1000000);

	if (!g_guard_clear || up < BTT_EARLY_UPTIME)
		return;
	int on = g_set[BTT_KEY_ENABLE];
	int late = up >= BTT_MIN_UPTIME;
	btt_aac_link_find();    /* once: is the optional AAC module loaded (every *KERNEL module has loaded by now) */
	btt_aptx_link_find();   /* and the aptX module */
	btt_ldac_link_find();   /* and the LDAC module */
	if (!logged_early) {
		logged_early = 1;
		BTT_KLOG("bluetooth: early period over (t=%u) - timing fix and latency may go in", up);
	}
	selftest_pass(on, g_set[BTT_KEY_CODEC], up);   /* the add-ons' self-tests: from the early gate, one at a time */
	btt_bt_set_aac(on && btt_aac_stream_is_aac());   /* the send queue's limit follows the codec of the stream (an AAC packet is twice the sound); nothing with the master switch off */
	btt_bt_sync(on, on ? g_set[BTT_KEY_LOWLAT] : BTT_LAT_STANDARD,
	            late && on && g_set[BTT_KEY_CTRL] == BTT_CTRL_FAST);
	if (!late)
		return;
	g_startup_over = 1;
	if (!logged) {
		logged = 1;
		BTT_KLOG("bluetooth: start-up period over (t=%u) - changes may go in", up);
	}
	btt_bteq_sync(g_set);
	btt_btvol_sync(g_set);
	btt_btqual_sync(on ? g_set[BTT_KEY_QUALITY] : BTT_Q_STANDARD);
	btt_btdisc_sync(btt_btapply_hold() ? BTT_DISC_SPEAKER : on ? g_set[BTT_KEY_DISCONNECT] : BTT_DISC_MUTE);   /* an apply holds the no-mute mode while the headphones are taken down and brought back */
	btt_btreconn_sync(on && g_set[BTT_KEY_RECONNECT]);
	{
		char name[BTT_NAME_MAX + 1];

		name_copy(name);
		btt_btname_sync(on ? name : "");
	}
	btt_bthq_set_ldac(on && btt_ldac_codec_effective(g_set[BTT_KEY_CODEC]) == BTT_CODEC_LDAC);   /* LDAC needs the 5-slot packet types */
	btt_bthq_sync(on ? g_set[BTT_KEY_QUALITY] : BTT_Q_STANDARD);
	btt_btflush_sync(on && g_set[BTT_KEY_LOWLAT] == BTT_LAT_LOWEST);
	btt_btevlog_pass(on);   /* the link log: why each link ended, who opened it, the profiles in order (it watches, it changes nothing) */
	btt_hfp_sync(on ? g_set[BTT_KEY_HFP] : 0);   /* the hands-free connection: Read Headset Battery sets the mode; the master switch gates it */
	btt_aac_stream_set_latency(on ? g_set[BTT_KEY_LOWLAT] : BTT_LAT_STANDARD);   /* the engine's output bound on an AAC stream */
	btt_aac_stream_set_kbps(BTT_AACBR_KBPS(g_set[BTT_KEY_AAC_BITRATE]));   /* the bit rate asked of the headphones; the next connection uses it */
	btt_aac_stream_set_ldac_mode((unsigned)g_set[BTT_KEY_LDAC_RATE]);   /* the LDAC bit rate: the next LDAC session, and a running one at once */
	btt_aac_stream_set_aptx_hd(g_set[BTT_KEY_APTX_HD]);   /* aptX HD When Available: whether HD is offered to headphones that list it */
	btt_aac_stream_set_source_only(g_set[BTT_KEY_SOURCE_ONLY]);   /* Music Source Only: the signalling hook leaves the Vita's speaker endpoint out of what it answers */
	btt_aac_stream_pass(on, g_set[BTT_KEY_CODEC], g_set[BTT_KEY_AAC_ACCEL]);   /* AAC streaming: the hooks (only when AAC is wanted), the session */
	btt_codecs_pass();   /* the codec list the signalling hook finished is kept (the file is the worker's) */
	btt_link_pass(on, on ? g_set[BTT_KEY_LOWLAT] : 0);   /* the audio link's codec and figures, for bttGetLinkInfo */
	btt_fall_pass(on, g_set[BTT_KEY_CODEC]);   /* a chosen codec that cannot be used for this connection (SBC is used): one notice for the Home screen module, once for each headset and codec */
	btt_btapply_pass(on, g_set[BTT_KEY_CODEC], (int)BTT_AACBR_KBPS(g_set[BTT_KEY_AAC_BITRATE]), g_set[BTT_KEY_AAC_ACCEL], g_set[BTT_KEY_QUALITY], g_set[BTT_KEY_APTX_HD]);   /* settings that need a new connection: the headphones reconnect at once */
}

static int worker(SceSize args, void *argp)
{
	int save_wait = 0;
	unsigned last_sec = 0;
	unsigned start_sec = (unsigned)(ksceKernelGetSystemTimeWide() / 1000000);

	for (;;) {
		unsigned bits = 0;
		unsigned wait_ms = btt_aac_stream_fast() || btt_btevlog_fast() ? BTT_FAST_MS : BTT_CHECK_MS;   /* an AAC stream is up: its session starts and ends within 0.1 s; a link is new: its profiles open within seconds */
		SceUInt timeout = wait_ms * 1000;
		ksceKernelWaitEventFlag(g_ev, EV_WAKE | EV_STOP,
		                        SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, &bits, &timeout);
		if (bits & EV_STOP)
			break;

		btt_fwlog_poll();   /* logs SceShell's build once it is running */
		settings_ver_log();
		page_skip_log();
		refresh_refused_log();
		if (g_dirty) {
			g_dirty = 0;
			save_wait = BTT_SAVE_DELAY_MS;
		}
		unsigned sec = (unsigned)(ksceKernelGetSystemTimeWide() / 1000000);
		if (sec != last_sec) {
			boot_tick(sec - start_sec);
			last_sec = sec;
		}
		bt_pass();

		/* Save once the user has stopped changing things for a second. */
		if (save_wait > 0) {
			save_wait -= (int)wait_ms;
			if (save_wait <= 0 && !g_dirty)
				settings_save();
		}
	}
	return 0;
}

static void wake(void) { if (g_ev >= 0) ksceKernelSetEventFlag(g_ev, EV_WAKE); }

/* ---- module entry ---------------------------------------------------------- */

#ifndef BTT_HOST_TEST   /* the host test (test/test_worker.c) builds on a platform without weak aliases */
void _start() __attribute__ ((weak, alias("module_start")));
#endif

int module_start(SceSize args, void *argp)
{
	btt_klog_init();
	BTT_KLOG("Bluetooth Turbo kernel " BTT_VERSION " starting");
	settings_load();

	/* '1' from here until the boot has reached the Home screen. */
	if (guard_read() == '1') {
		/* The last start never got that far: stand aside for this one (no
		 * worker, nothing touched) so the console boots, and clear the guard so
		 * the next start runs as usual. That start's log is kept as
		 * bluetoothturbo_kernel.prev.log. */
		BTT_KLOG("SAFE START: last start didn't finish [%s]; nothing runs this start", g_guard_txt);
		guard_write('0', "safe");
		return SCE_KERNEL_START_SUCCESS;
	}
	guard_write('1', "start");

	g_ev = ksceKernelCreateEventFlag("BluetoothTurbo", 0, 0, NULL);
	g_thread = ksceKernelCreateThread("BluetoothTurbo", worker, 0x70, 0x2000, 0, 0, NULL);
	if (g_ev < 0 || g_thread < 0) {
		BTT_KLOG("worker create failed ev=0x%08x th=0x%08x", (unsigned)g_ev, (unsigned)g_thread);
		return SCE_KERNEL_START_SUCCESS;
	}
	ksceKernelStartThread(g_thread, 0, NULL);

	BTT_KLOG("module_start done");
	wake();
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp)
{
	if (g_thread >= 0) {
		ksceKernelSetEventFlag(g_ev, EV_STOP);
		ksceKernelWaitThreadEnd(g_thread, NULL, NULL);
		ksceKernelDeleteThread(g_thread);
	}
	if (g_ev >= 0)
		ksceKernelDeleteEventFlag(g_ev);
	/* Every Bluetooth change back to stock, and the hooks out. The AAC stream first: its session ends and its hooks go before the shared one. */
	btt_aac_stream_restore();
	btt_btevlog_restore();
	btt_btflush_restore();
	btt_bthq_restore();
	btt_hfp_restore();
	btt_btname_restore();
	btt_btreconn_restore();
	btt_btdisc_restore();
	btt_btqual_restore();
	btt_btvol_restore();
	btt_bt_restore();
	btt_bteq_restore();
	return SCE_KERNEL_STOP_SUCCESS;
}
