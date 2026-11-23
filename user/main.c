/*
 * main.c - bluetoothturbo.suprx, listed under *main (SceShell, the Home screen).
 *
 * Its job: tell the user, with the system's own notification, when the battery of the connected Bluetooth headset has run low
 * (20 %, 10 % and 5 %, each once per drain; user/lowbatt.c), if the Settings switch Low Battery Notifications is on, and when the codec chosen in
 * Bluetooth Codec cannot be used for the headphones and SBC is used instead. The kernel module owns the level and the switch (bttGetHeadsetBattery,
 * bttGet) and decides the codec notice and holds its text in the system language (bttGetNotice, kernel/btfall_core.h: the Home screen has no memory for
 * twenty more sentences); this module only asks, the battery every 30 s and the codec notice every 2 s while headphones are connected (every 30 s otherwise), and shows what it is given.
 *
 * It is as small as it can be (the Home screen is short of memory): no log, no Settings text beyond the one notice in twenty
 * languages, one thread with a small stack, and one 0x470-byte block for the notification. module_start only makes the thread; the
 * thread touches nothing until 60 s have passed (the Home screen is up long before). Every library is a weak import, checked before
 * its first call, so with the kernel module missing, older or stopped the module does nothing and says nothing.
 *
 * Why a Home screen module at all: the notification library can only be called from a user process, and the Home screen is the
 * one that is always there; the kernel module cannot send a notice, and no other plugin's host is used, so that this product never depends
 * on another. The thread exists whenever the module is loaded (the Settings page lives in
 * another process and cannot start one), and ends itself when the kernel module turns out not to be there.
 *
 * The send must not come from the Home screen's own UI thread (the call waits for a job that thread would have to run), so it comes
 * from this module's thread. Before each send a guard byte in ur0:data/bluetoothturbo/notice.guard says 'S' (sending) and after it 'K':
 * if a start finds 'S' the last send never came back, and this start sends nothing (the byte is put back to 'K' for the next one).
 */
#include <psp2/io/fcntl.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/notificationutil.h>

#include "../include/bluetoothturbo.h"
#include "imports.h"
#include "lowbatt.h"
#include "notice.h"
#include "bluetoothturbo_notice_table.h"

#define START_DELAY_US   (60u * 1000u * 1000u)
#define POLL_US          (2u * 1000u * 1000u)     /* the codec notice is asked for this often while headphones are connected    */
#define SAMPLE_POLLS     15u                       /* ... and the battery every 15th time: 30 s                                    */
#define TEXT_BYTES       192                       /* the longest codec notice (the kernel module's BTFALL_TEXT)                   */
#define STACK_BYTES      0x2000
#define GUARD_PATH       BTT_DIR "/notice.guard"
#ifndef SCE_SYSMODULE_NOTIFICATION_UTIL
#define SCE_SYSMODULE_NOTIFICATION_UTIL 0x33
#endif
#define LANG_COUNT       20

extern int sceRegMgrGetKeyInt(const char *category, const char *name, int *value);

static SceUID g_thread = -1;
static volatile int g_run;
static LowBatt g_state;
static int g_sysmodule_loaded;
static int g_lang = 1;                                       /* the system language, read every 30 s (not on every 2 s poll)   */
static int g_connected;                                      /* headphones were connected when the battery was last looked at (sample)     */
static int g_guard_off;                                      /* the last send never came back: no sends this start */
static uint8_t g_block[NOTICE_BLOCK_SIZE] __attribute__((aligned(4)));

static int system_language(void)
{
	int lang = -1;

	if (btt_import_ready((const void *)sceRegMgrGetKeyInt) && sceRegMgrGetKeyInt("/CONFIG/SYSTEM", "language", &lang) >= 0 &&
	    lang >= 0 && lang < LANG_COUNT)
		return lang;
	return 1;                                                /* English (US) */
}

/* The guard byte, in place (never truncated: a crash between a truncate and the write would leave an empty file). */
static void guard_write(char c)
{
	SceUID fd;

	if (!btt_import_ready((const void *)sceIoOpen))
		return;
	fd = sceIoOpen(GUARD_PATH, SCE_O_WRONLY | SCE_O_CREAT, 0666);
	if (fd >= 0) {
		sceIoWrite(fd, &c, 1);
		sceIoClose(fd);
	}
}

static void guard_start(void)
{
	char c = 0;
	SceUID fd;

	if (!btt_import_ready((const void *)sceIoOpen))
		return;
	fd = sceIoOpen(GUARD_PATH, SCE_O_RDONLY, 0);
	if (fd < 0)
		return;
	sceIoRead(fd, &c, 1);
	sceIoClose(fd);
	if (c == 'S') {
		g_guard_off = 1;
		guard_write('K');
	}
}

/* Shows `tmpl` (UTF-8; {n} becomes `level`) as a system notification. */
static void send_text(const char *tmpl, int level)
{
	if (g_guard_off || notice_build(g_block, tmpl, level) < 0)
		return;
	if (!g_sysmodule_loaded) {
		if (!btt_import_ready((const void *)sceSysmoduleLoadModule) || sceSysmoduleLoadModule(SCE_SYSMODULE_NOTIFICATION_UTIL) < 0)
			return;
		g_sysmodule_loaded = 1;
	}
	if (!btt_import_ready((const void *)sceNotificationUtilSendNotification))
		return;
	guard_write('S');
	sceNotificationUtilSendNotification((const SceWChar16 *)g_block);
	guard_write('K');
}

static void send_notice(int level)
{
	send_text(g_notice[system_language()], level);
}

/* A codec notice, if the kernel module has one waiting: its text already in the system language, no placeholder but the codec's name, which it has put in. */
static void codec_poll(void)
{
	char t[TEXT_BYTES];

	if (g_guard_off || !btt_import_ready((const void *)bttGetNotice))   /* with the guard on nothing is sent, so nothing is taken from the kernel module either */
		return;
	if (bttGetNotice(g_lang, t, (int)sizeof(t)) > 0)
		send_text(t, 0);
}

/* 0 when it looked, -1 when the kernel module is not there at all (it is loaded at boot, so it will not appear later). */
static int sample(void)
{
	BttHeadsetBattery b;
	int on, r;

	if (!btt_import_ready((const void *)bttGet) || !btt_import_ready((const void *)bttGetHeadsetBattery))
		return -1;
	on = bttGet(BTT_KEY_LOWBATT);
	if (on < 0) {
		g_connected = 0;
		return 0;
	}
	b.size = sizeof(b);
	b.level = BTT_BATT_UNKNOWN;
	b.connected = 0;
	if (bttGetHeadsetBattery(&b) != BTT_OK) {
		b.level = BTT_BATT_UNKNOWN;
		b.connected = 0;
	}
	g_connected = b.connected ? 1 : 0;
	r = lowbatt_step(&g_state, b.level, g_connected, on != 0);
	if (r >= 0)
		send_notice(r);
	return 0;
}

/* Sleeps `us` in one-second steps, so a module stop is heard within a second. */
static void nap(unsigned us)
{
	for (unsigned t = 0; t < us && g_run; t += 1000000u)
		sceKernelDelayThread(1000000u);
}

static int worker(SceSize args, void *argp)
{
	nap(START_DELAY_US);
	if (!g_run)
		return 0;
	guard_start();
	for (unsigned tick = 0; g_run; tick++) {
		if (tick % SAMPLE_POLLS == 0)
			g_lang = system_language();
		if (tick % SAMPLE_POLLS == 0 && sample() < 0) {
			/* no kernel module (or an older one): nothing to ask for ever, so give the stack back */
			g_thread = -1;
			sceKernelExitDeleteThread(0);
		}
		if (lowbatt_notice_due(tick, SAMPLE_POLLS, g_connected))
			codec_poll();
		nap(POLL_US);
	}
	return 0;
}

void _start() __attribute__ ((weak, alias("module_start")));

int module_start(SceSize args, void *argp)
{
	g_run = 1;
	g_thread = sceKernelCreateThread("BluetoothTurboNotice", worker, 0x10000100, STACK_BYTES, 0, 0, NULL);
	if (g_thread >= 0)
		sceKernelStartThread(g_thread, 0, NULL);
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp)
{
	g_run = 0;
	if (g_thread >= 0) {
		sceKernelWaitThreadEnd(g_thread, NULL, NULL);       /* the thread looks at g_run every second */
		sceKernelDeleteThread(g_thread);
	}
	return SCE_KERNEL_STOP_SUCCESS;
}
