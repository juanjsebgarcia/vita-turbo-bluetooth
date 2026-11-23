/*
 * btaac_link.c - see btaac_link.h. The AAC module is looked up by its module name and the number of its one export; the table it
 * returns is checked for size and version. A table that is not as expected is refused and logged, never used.
 */
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "btaac_api.h"
#include "btaac_link.h"
#include "btaac_stream.h"
#include "klog.h"

#define AAC_MODULE_NAME  "bluetoothturbo_aac"

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);

/* The table, once found (btaac_link_leaf.c reads it). Never cleared: see btaac_api.h on why a table taken once stays valid. */
const BtAacApi *volatile btaac_link_api;

static int g_looked, g_logged_missing;

void btt_aac_link_find(void)
{
	uintptr_t f = 0;
	const BtAacApi *a;

	if (g_looked)
		return;
	g_looked = 1;
	if (module_get_export_func(KERNEL_PID, AAC_MODULE_NAME, TAI_ANY_LIBRARY, BTAAC_NID_GET_API, &f) < 0 || !f) {
		BTT_KLOG("aac: the AAC module is not loaded: AAC is not offered");
		return;
	}
	a = ((const BtAacApi *(*)(void))f)();
	if (!a || a->size < sizeof(BtAacApi) || a->version != BTAAC_API_VERSION) {
		BTT_KLOG("aac: the AAC module does not match this build (table size %u version %u, need %u and %u): AAC is not offered",
		         a ? (unsigned)a->size : 0u, a ? (unsigned)a->version : 0u, (unsigned)sizeof(BtAacApi), (unsigned)BTAAC_API_VERSION);
		return;
	}
	btaac_link_api = a;
	BTT_KLOG("aac: the AAC module is loaded (interface version %u)", (unsigned)a->version);
}

void btt_aac_link_sync(int on, int codec)
{
	const BtAacApi *a = btaac_link_api;

	if (!a) {
		if (codec == BTT_CODEC_AAC && !g_logged_missing) {
			g_logged_missing = 1;
			BTT_KLOG("aac: the Bluetooth Codec is AAC but the AAC module is not loaded: SBC is sent");
		}
		return;
	}
	a->sync(on);
}

int btt_aac_selftest_done(void)
{
	const BtAacApi *a = btaac_link_api;
	BtAacInfo i;

	if (!a)
		return 1;
	a->get_info(&i);
	return i.selftest != BTAAC_ST_NOT_RUN;
}

int btt_aac_avail(void)
{
	const BtAacApi *a = btaac_link_api;
	BtAacInfo i;
	int v = BTT_AAC_AVAIL_MODULE;

	if (!a)
		return 0;
	a->get_info(&i);
	if (btaac_c_allowed((int)i.selftest))
		v |= BTT_AAC_AVAIL_TESTED;
	if (i.neon_allowed)
		v |= BTT_AAC_AVAIL_NEON;
	if (i.hold)
		v |= BTT_AAC_AVAIL_HELD;
	return v;
}

int btt_aac_codec_effective(int codec)
{
	int v;

	if (codec != BTT_CODEC_AAC)
		return BTT_CODEC_SBC;
	v = btt_aac_avail();
	return (v & BTT_AAC_AVAIL_MODULE) && (v & BTT_AAC_AVAIL_TESTED) && !(v & BTT_AAC_AVAIL_HELD) ? BTT_CODEC_AAC : BTT_CODEC_SBC;
}
