/*
 * btfall.c - the codec fallback notification, the kernel module's side. See btfall.h and btfall_core.h.
 *
 * The worker (never a hook) gathers what btfall_step needs: the link's snapshot (btlink.c) and what the stream code recorded about a refusal
 * (btt_aac_stream_take_failure), and queues a notice. The Home screen module takes the text through bttGetNotice, in the system's language, and shows it
 * with the notification call it uses for the low battery notice.
 */
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/sysclib.h>

#include "../include/bluetoothturbo.h"
#include "btaac_hook.h"
#include "btaac_stream.h"
#include "btaptx_link.h"
#include "btlink.h"
#include "btfall.h"
#include "btfall_core.h"
#include "klog.h"

static BtFall g_fall = { .setting = -1 };

void btt_fall_pass(int on, int codec)
{
	BttLinkInfo k;
	BtFallIn in;
	uint32_t before = g_fall.head;

	memset(&in, 0, sizeof(in));
	btt_link_read(&k);
	in.on = on;
	in.setting = codec;
	in.offered = on ? btt_codec_effective(codec) : BTT_CODEC_SBC;
	in.hd_pref = btaac_neg.want_aptx_hd ? 1 : 0;
	in.connected = k.connected ? 1 : 0;
	in.addr_lo = k.addr_lo;
	in.addr_hi = k.addr_hi;
	in.codec_mask = k.codec_mask;
	in.link_codec = (int)k.codec;
	in.banned = btaac_ban_bits(k.addr_lo, k.addr_hi);   /* the codecs the headphones were marked for after a refusal (btaac_ban) */
	in.fail_codec = btt_aac_stream_take_failure(&in.fail_lo, &in.fail_hi);   /* taken every pass, so an old one is not shown later */
	if (btfall_step(&g_fall, &in) > 0)
		BTT_KLOG("codec notice: %u queued for the Home screen module (%u waiting, %u lost)", (unsigned)(g_fall.head - before), (unsigned)(g_fall.head - g_fall.tail),
		         (unsigned)g_fall.dropped);
}

/* The Home screen module's call: the oldest waiting notice's text in language `lang` (0..19; English for any other) into its buffer. Returns the text's length in
 * bytes (UTF-8, without the NUL), 0 when nothing is waiting, BTT_ERR_ARG for no buffer, BTT_ERR_RANGE when the text does not fit. A notice that is taken is gone. */
int bttGetNotice(int lang, char *out, int size)
{
	BtFallNotice n;
	char t[BTFALL_TEXT];
	int len;

	if (!out || size < 2)
		return BTT_ERR_ARG;
	if (!btfall_pop(&g_fall, &n))
		return 0;
	len = btfall_text(lang, n.kind, n.codec, t, (int)sizeof(t));
	if (len < 0 || len >= size)
		return BTT_ERR_RANGE;
	if (ksceKernelMemcpyKernelToUser(out, t, (size_t)len + 1) < 0)
		return BTT_ERR_ARG;
	return len;
}
