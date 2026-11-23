/*
 * btaptx_link.c - see btaptx_link.h. The add-on is looked up by its module name and the number of its one export; the table it returns is checked for size and
 * version. A table that is not as expected is refused and logged, never used.
 */
#include <psp2kern/kernel/sysclib.h>
#include <taihen.h>

#include "../include/bluetoothturbo.h"
#include "btaac_link.h"
#include "btaptx_api.h"
#include "btaptx_link.h"
#include "btldac_link.h"
#include "klog.h"

#define APTX_MODULE_NAME  "bluetoothturbo_aptx"

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);

/* The table, once found (btaptx_link_leaf.c reads it). Never cleared: the add-on's module_stop asks not to be unloaded (btaptx_main.c). */
const BtAptxApi *volatile btaptx_link_api;

static int g_looked, g_logged_missing;

void btt_aptx_link_find(void)
{
	uintptr_t f = 0;
	const BtAptxApi *a;

	if (g_looked)
		return;
	g_looked = 1;
	if (module_get_export_func(KERNEL_PID, APTX_MODULE_NAME, TAI_ANY_LIBRARY, BTAPTX_NID_GET_API, &f) < 0 || !f) {
		BTT_KLOG("aptx: the aptX module is not loaded: aptX is not offered");
		return;
	}
	a = ((const BtAptxApi *(*)(void))f)();
	if (!a || a->size < sizeof(BtAptxApi) || a->version != BTAPTX_API_VERSION) {
		BTT_KLOG("aptx: the aptX module does not match this build (table size %u version %u, need %u and %u): aptX is not offered",
		         a ? (unsigned)a->size : 0u, a ? (unsigned)a->version : 0u, (unsigned)sizeof(BtAptxApi), (unsigned)BTAPTX_API_VERSION);
		return;
	}
	btaptx_link_api = a;
	BTT_KLOG("aptx: the aptX module is loaded (interface version %u)", (unsigned)a->version);
}

void btt_aptx_link_sync(int on, int codec)
{
	const BtAptxApi *a = btaptx_link_api;

	if (!a) {
		if (codec == BTT_CODEC_APTX && !g_logged_missing) {
			g_logged_missing = 1;
			BTT_KLOG("aptx: the Bluetooth Codec is aptX but the aptX module is not loaded: SBC is sent");
		}
		return;
	}
	a->sync(on);
}

int btt_aptx_selftest_done(void)
{
	const BtAptxApi *a = btaptx_link_api;
	BtAptxInfo i;

	if (!a)
		return 1;
	a->get_info(&i);
	return i.selftest != BTAPTX_ST_NOTRUN;
}

int btt_aptx_avail(void)
{
	const BtAptxApi *a = btaptx_link_api;
	BtAptxInfo i;
	int v = BTT_APTX_AVAIL_MODULE;

	if (!a)
		return 0;
	a->get_info(&i);
	if (i.selftest == BTAPTX_ST_PASS)
		v |= BTT_APTX_AVAIL_TESTED;
	if (i.hold)
		v |= BTT_APTX_AVAIL_HELD;
	if (i.selftest_hd == BTAPTX_ST_PASS && i.hd_ctx_ok)
		v |= BTT_APTX_AVAIL_HD;
	return v;
}

int btt_aptx_hd_usable(void)
{
	int v = btt_aptx_avail();

	return (v & BTT_APTX_AVAIL_MODULE) && (v & BTT_APTX_AVAIL_TESTED) && (v & BTT_APTX_AVAIL_HD) && !(v & BTT_APTX_AVAIL_HELD);
}

int btt_aptx_codec_effective(int codec)
{
	int v;

	if (codec != BTT_CODEC_APTX)
		return BTT_CODEC_SBC;
	v = btt_aptx_avail();
	return (v & BTT_APTX_AVAIL_MODULE) && (v & BTT_APTX_AVAIL_TESTED) && !(v & BTT_APTX_AVAIL_HELD) ? BTT_CODEC_APTX : BTT_CODEC_SBC;
}

int btt_codec_effective(int codec)
{
	return codec == BTT_CODEC_AAC ? btt_aac_codec_effective(codec) : codec == BTT_CODEC_LDAC ? btt_ldac_codec_effective(codec) : btt_aptx_codec_effective(codec);
}

int btt_aptx_session_start(int hd)
{
	const BtAptxApi *a = btaptx_link_api;

	return a ? a->start(hd) : BTAPTX_ERR_NOCTX;
}

void btt_aptx_session_stop(void)
{
	const BtAptxApi *a = btaptx_link_api;

	if (a)
		a->stop();
}
