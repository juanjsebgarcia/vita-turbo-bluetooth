/*
 * btldac_link.c - see btldac_link.h. The add-on is looked up by its module name and the number of its one export; the table it returns is checked for size and version. A
 * table that is not as expected is refused and logged, never used.
 */
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "btldac_api.h"
#include "btldac_link.h"
#include "klog.h"

#define LDAC_MODULE_NAME  "bluetoothturbo_ldac"

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);

/* The table, once found (btldac_link_leaf.c reads it). Never cleared: the add-on's module_stop asks not to be unloaded (btldac_main.c). */
const BtLdacApi *volatile btldac_link_api;

static int g_looked, g_logged_missing;

void btt_ldac_link_find(void)
{
	uintptr_t f = 0;
	const BtLdacApi *a;

	if (g_looked)
		return;
	g_looked = 1;
	if (module_get_export_func(KERNEL_PID, LDAC_MODULE_NAME, TAI_ANY_LIBRARY, BTLDAC_NID_GET_API, &f) < 0 || !f) {
		BTT_KLOG("ldac: the LDAC module is not loaded: LDAC is not offered");
		return;
	}
	a = ((const BtLdacApi *(*)(void))f)();
	if (!a || a->size < sizeof(BtLdacApi) || a->version != BTLDAC_API_VERSION) {
		BTT_KLOG("ldac: the LDAC module does not match this build (table size %u version %u, need %u and %u): LDAC is not offered",
		         a ? (unsigned)a->size : 0u, a ? (unsigned)a->version : 0u, (unsigned)sizeof(BtLdacApi), (unsigned)BTLDAC_API_VERSION);
		return;
	}
	btldac_link_api = a;
	BTT_KLOG("ldac: the LDAC module is loaded (interface version %u)", (unsigned)a->version);
}

void btt_ldac_link_sync(int on, int codec)
{
	const BtLdacApi *a = btldac_link_api;

	if (!a) {
		if (codec == BTT_CODEC_LDAC && !g_logged_missing) {
			g_logged_missing = 1;
			BTT_KLOG("ldac: the Bluetooth Codec is LDAC but the LDAC module is not loaded: SBC is sent");
		}
		return;
	}
	a->sync(on);
}

int btt_ldac_selftest_done(void)
{
	const BtLdacApi *a = btldac_link_api;
	BtLdacInfo i;

	if (!a)
		return 1;
	a->get_info(&i);
	return i.selftest != BTLDAC_ST_NOTRUN;
}

int btt_ldac_avail(void)
{
	const BtLdacApi *a = btldac_link_api;
	BtLdacInfo i;
	int v = BTT_LDAC_AVAIL_MODULE;

	if (!a)
		return 0;
	a->get_info(&i);
	if (i.selftest == BTLDAC_ST_PASS)
		v |= BTT_LDAC_AVAIL_TESTED;
	if (i.hold)
		v |= BTT_LDAC_AVAIL_HELD;
	return v;
}

int btt_ldac_codec_effective(int codec)
{
	int v;

	if (codec != BTT_CODEC_LDAC)
		return BTT_CODEC_SBC;
	v = btt_ldac_avail();
	return (v & BTT_LDAC_AVAIL_MODULE) && (v & BTT_LDAC_AVAIL_TESTED) && !(v & BTT_LDAC_AVAIL_HELD) ? BTT_CODEC_LDAC : BTT_CODEC_SBC;
}

int btt_ldac_session_start(uint32_t mode, uint32_t mtu)
{
	const BtLdacApi *a = btldac_link_api;

	return a ? a->start(mode, mtu) : BTLDAC_ERR_THREAD;
}

void btt_ldac_session_stop(void)
{
	const BtLdacApi *a = btldac_link_api;

	if (a)
		a->stop();
}

int btt_ldac_state(void)
{
	const BtLdacApi *a = btldac_link_api;
	BtLdacInfo i;

	if (!a)
		return BTLDAC_S_IDLE;
	a->get_info(&i);
	return (int)i.state;
}

int btt_ldac_last_error(void)
{
	const BtLdacApi *a = btldac_link_api;
	BtLdacInfo i;

	if (!a)
		return 0;
	a->get_info(&i);
	return (int)i.last_error;
}

int btt_ldac_session_running(void)
{
	int s = btt_ldac_state();

	return s == BTLDAC_S_RUNNING || s == BTLDAC_S_STARTING;
}

void btt_ldac_set_mode(uint32_t mode)
{
	const BtLdacApi *a = btldac_link_api;

	if (a)
		a->set_mode(mode);
}
