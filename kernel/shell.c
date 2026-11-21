/*
 * shell.c - see shell.h. SceShell's process id comes from a system routine resolved at run time
 * (the routine's library number is the same on every firmware 3.60-3.74).
 */
#include <psp2kern/kernel/modulemgr.h>
#include <taihen.h>

#include "klog.h"
#include "shell.h"

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid,
                           uint32_t funcnid, uintptr_t *func);

#define NID_SHELL_PID   0x05093E7B   /* ksceKernelSysrootGetShellPid */

static int (*g_shell_pid)(void);
static int g_tried, g_up;

int btt_shell_up(void)
{
	if (g_up)
		return 1;
	if (!g_shell_pid && !g_tried) {
		uintptr_t f = 0;
		g_tried = 1;
		if (module_get_export_func(KERNEL_PID, "SceSysmem", TAI_ANY_LIBRARY,
		                           NID_SHELL_PID, &f) >= 0 && f)
			g_shell_pid = (int (*)(void))f;
		else
			BTT_KLOG("shell: process lookup not found - the boot guard clears by time only");
	}
	if (g_shell_pid && g_shell_pid() > 0) {
		g_up = 1;
		BTT_KLOG("shell: the Home screen is running");
	}
	return g_up;
}
