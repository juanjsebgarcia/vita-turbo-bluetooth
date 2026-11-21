/*
 * main.c - bluetoothturbo_settings.suprx, listed under *NPXS10015 (the Settings app).
 *
 * Installs the Settings page hooks (settings_app/bluetoothturbo_settings.c). If the
 * module is ever loaded into another process by mistake, it does nothing.
 */
#include <psp2/kernel/modulemgr.h>
#include <string.h>
#include <taihen.h>

void btt_settings_install(void);
void btt_settings_release(void);

static int g_active;

void _start() __attribute__ ((weak, alias("module_start")));

int module_start(SceSize args, void *argp)
{
	tai_module_info_t info;
	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	if (taiGetModuleInfo("SceSettings", &info) >= 0) {
		btt_settings_install();
		g_active = 1;
	}
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp)
{
	if (g_active)
		btt_settings_release();
	return SCE_KERNEL_STOP_SUCCESS;
}
