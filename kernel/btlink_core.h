/*
 * btlink_core.h - what bttGetLinkInfo says, worked out from what the module saw (no system call: the host test compiles it as it is).
 */
#ifndef BTLINK_CORE_H
#define BTLINK_CORE_H

#include <stdint.h>

#include "../include/bluetoothturbo.h"

#define BTLINK_PROF_HFP  0x1u        /* in the link slot's connected-profile word: the hands-free profile is connected          */
#define BTLINK_PROF_HSP  0x10000u    /* ... the headset profile (the console's own) is connected; HFP wins when both are       */

/* What the worker read on one pass. */
typedef struct BtLinkIn {
	int on;                 /* Bluetooth Turbo's master switch                                                          */
	int recognised;         /* the Bluetooth code's data was readable (its code recognised)                              */
	int connected;          /* the A2DP link's slot is set                                                              */
	int streaming;          /* the stream is started                                                                    */
	int enc_valid;          /* the stream's SBC encoder has been set up by the first packet                              */
	uint32_t sbc_bitpool;   /* its highest bitpool                                                                      */
	uint32_t sbc_chmode;    /* its channel mode as the code numbers it: 0 mono, 1 dual, 2 stereo, 3 joint                */
	int aac_stream;         /* this link is configured as AAC (the AAC stream hooks' state is CONFIGURED for this slot)   */
	uint32_t aac_bps;       /* the AAC bit rate in force, b/s                                                           */
	int aac_neon;           /* the AAC session runs the NEON build                                                      */
	int aptx_stream;        /* this link is configured as aptX (the stream hooks' state is CONFIGURED for this slot, aptX negotiated)   */
	int aptx_hd;            /* ... and it is aptX HD (the codec stays aptX; the bit rate tells the two apart)                                   */
	int ldac_stream;        /* this link is configured as LDAC (the stream hooks' state is CONFIGURED for this slot, LDAC negotiated)             */
	uint32_t ldac_kbps;     /* the LDAC bit rate of the packet last made, kb/s (0 until one was)                                               */
	int latency;            /* the Bluetooth latency setting                                                            */
	uint32_t addr_lo;       /* the A2DP link's device address: low word, and the high 16 bits                            */
	uint32_t addr_hi;
	uint32_t codec_mask;    /* the codecs the headphones list (BTT_HC_*), 0 when never read                                 */
	unsigned sig_lq;        /* the smoothed radio readings and the word (btsignal_core.h); all 0 while measuring              */
	int sig_rssi;
	unsigned sig_level;     /* BTT_LINK_SIGNAL_*                                                                            */
	uint32_t conn_word;     /* the link slot's connected-profile word: 0x1 hands-free, 0x10000 headset                         */
} BtLinkIn;

static inline void btlink_compose(const BtLinkIn *in, BttLinkInfo *o)
{
	*o = (BttLinkInfo){ 0 };
	if (!in->on || !in->recognised)
		return;
	o->latency = (unsigned)in->latency;
	if (!in->connected)
		return;
	o->connected = 1;
	o->streaming = in->streaming ? 1u : 0u;
	o->sample_rate = 48000u;
	o->addr_lo = in->addr_lo;
	o->addr_hi = in->addr_hi & 0xFFFFu;
	o->codec_mask = in->codec_mask;
	o->link_quality = in->sig_lq;
	o->rssi = in->sig_rssi;
	o->signal_level = in->sig_level;
	o->voice_profile = (in->conn_word & BTLINK_PROF_HFP) ? BTT_LINK_VOICE_HFP : (in->conn_word & BTLINK_PROF_HSP) ? BTT_LINK_VOICE_HSP : BTT_LINK_VOICE_NONE;
	if (in->ldac_stream) {
		o->codec = BTT_LINK_CODEC_LDAC;
		o->bitrate_kbps = in->ldac_kbps ? in->ldac_kbps : 330u;   /* the setting's default until the first packet is made */
		return;
	}
	if (in->aptx_stream) {
		o->codec = BTT_LINK_CODEC_APTX;
		o->bitrate_kbps = in->aptx_hd ? 576u : 384u;   /* fixed: the codec has no rate setting (aptX HD 576) */
		return;
	}
	if (in->aac_stream) {
		o->codec = BTT_LINK_CODEC_AAC;
		o->bitrate_kbps = (in->aac_bps + 500u) / 1000u;
		o->accel = in->aac_neon ? 1u : 0u;
		return;
	}
	o->codec = BTT_LINK_CODEC_SBC;
	if (in->streaming && in->enc_valid && in->sbc_chmode <= 3u) {
		o->sbc_bitpool = in->sbc_bitpool;
		o->sbc_channel_mode = in->sbc_chmode + 1u;   /* 0 mono -> 1 .. 3 joint -> 4: 0 stays "not known" */
	}
}

#endif
