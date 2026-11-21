/*
 * fwlog.c - see fwlog.h.
 *
 * Every firmware ships its own SceShell build and a version spoof leaves it
 * alone, so SceShell's module NID names the firmware the console really runs.
 * Everything here is resolved at run time and read-only; a lookup that fails
 * just leaves its line out.
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "fwlog.h"
#include "klog.h"

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid,
                           uint32_t funcnid, uintptr_t *func);

#define LIB_MODULEMGR       0xD4A60A52   /* SceModulemgrForDriver              */
#define NID_GET_SWVERSION   0x5182E212   /* ksceKernelGetSystemSwVersion       */
#define LIB_SYSROOT         0x2ED7F97A   /* SceSysrootForDriver                */
#define NID_SYSROOT_VERSION 0x67AAB627   /* the kernel's own firmware record   */
#define NID_SHELL_PID       0x05093E7B   /* ksceKernelSysrootGetShellPid       */

/* SceShell's module NID on each firmware 3.60-3.74. */
static const struct { uint32_t nid; const char *fw; } k_shell[] = {
	{ 0x0552F692, "3.60" }, { 0x532155E5, "3.61" }, { 0xBB4B0A3E, "3.63" },
	{ 0x5549BF1F, "3.65" }, { 0x34B4D82E, "3.67" }, { 0x12DAC0F3, "3.68" },
	{ 0x0703C828, "3.69" }, { 0x2053B5A5, "3.70" }, { 0xF476E785, "3.71" },
	{ 0x939FFBE9, "3.72" }, { 0x734D476A, "3.73" }, { 0x51CB6207, "3.74" },
};

static int g_started, g_done;

static uintptr_t fn(const char *mod, uint32_t lib, uint32_t nid)
{
	uintptr_t f = 0;
	if (module_get_export_func(KERNEL_PID, mod, lib, nid, &f) < 0)
		return 0;
	return f;
}

static void log_versions(void)
{
	int (*swver)(SceKernelFwInfo *) =
		(int (*)(SceKernelFwInfo *))fn("SceKernelModulemgr", LIB_MODULEMGR, NID_GET_SWVERSION);
	if (swver) {
		SceKernelFwInfo v;
		memset(&v, 0, sizeof(v));
		v.size = sizeof(v);
		if (swver(&v) >= 0) {
			char s[16];
			int n = 0;
			while (n < (int)sizeof(s) - 1 && v.versionString[n]) {
				s[n] = v.versionString[n];
				n++;
			}
			s[n] = 0;
			BTT_KLOG("firmware: reports %s (0x%08x)", s, (unsigned)v.version);
		}
	}
	unsigned (*real)(void) =
		(unsigned (*)(void))fn("SceSysmem", LIB_SYSROOT, NID_SYSROOT_VERSION);
	if (real)
		BTT_KLOG("firmware: kernel record 0x%08x", real());
}

static void log_shell(void)
{
	int (*shell_pid)(void) = (int (*)(void))fn("SceSysmem", TAI_ANY_LIBRARY, NID_SHELL_PID);
	if (!shell_pid) {
		g_done = 1;   /* nothing to retry */
		return;
	}
	SceUID pid = shell_pid();
	if (pid <= 0)
		return;       /* SceShell is not up yet */
	tai_module_info_t ti;
	memset(&ti, 0, sizeof(ti));
	ti.size = sizeof(ti);
	if (taiGetModuleInfoForKernel(pid, "SceShell", &ti) < 0)
		return;
	g_done = 1;
	for (unsigned i = 0; i < sizeof(k_shell) / sizeof(k_shell[0]); i++) {
		if (k_shell[i].nid == ti.module_nid) {
			BTT_KLOG("firmware: home screen is the %s build (0x%08x)",
			        k_shell[i].fw, (unsigned)ti.module_nid);
			return;
		}
	}
	BTT_KLOG("firmware: home screen build 0x%08x is not one of 3.60-3.74", (unsigned)ti.module_nid);
}

void btt_fwlog_poll(void)
{
	if (!g_started) {
		g_started = 1;
		log_versions();
	}
	if (!g_done)
		log_shell();
}
