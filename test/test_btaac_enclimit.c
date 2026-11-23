/*
 * test_btaac_enclimit.c - host test of kernel/btaac_enclimit.c, the function that holds the vendored encoder's bit reservoir to a size, compiled with the
 * vendored encoder (one build) and its internal headers, as the module builds it.
 *
 *   make test      (cc -Ikernel -I<encoder's headers> test/test_btaac_enclimit.c kernel/btaac_enclimit.c + the vendored encoder, then run)
 *
 * What is proved: the record the limit relies on is what the vendored encoder's own set-up gives (every rate the engine asks for, one channel and two); a
 * record that is not that is refused and left alone, and so is a second call; and with the limit set, a frame is never longer than the average frame plus
 * the reservoir, for every reservoir from 8 bytes up and every bit rate from 64 to 320 kb/s, on the signals that fill the reservoir and then spend it.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "voAAC.h"
#include "aacenc_core.h"
#include "../kernel/btaac_core.h"

volatile unsigned int btaac_assert_failed;     /* the encoder's consistency checks land here (kernel/shim/assert.h) */
int btaac_enc_limit(void *h, unsigned int res_bytes);

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

static VO_U32 m_alloc(VO_S32 id, VO_MEM_INFO *m) { (void)id; m->VBuffer = calloc(1, (size_t)m->Size + 64); return m->VBuffer ? 0 : 1; }
static VO_U32 m_free(VO_S32 id, VO_PTR p) { (void)id; free(p); return 0; }
static VO_U32 m_set(VO_S32 id, VO_PTR p, VO_U8 v, VO_U32 n) { (void)id; memset(p, v, n); return 0; }
static VO_U32 m_copy(VO_S32 id, VO_PTR d, VO_PTR s, VO_U32 n) { (void)id; memcpy(d, s, n); return 0; }
static VO_U32 m_check(VO_S32 id, VO_PTR p, VO_U32 n) { (void)id; (void)p; (void)n; return 0; }
static VO_S32 m_compare(VO_S32 id, VO_PTR a, VO_PTR b, VO_U32 n) { (void)id; return memcmp(a, b, n); }
static VO_U32 m_move(VO_S32 id, VO_PTR d, VO_PTR s, VO_U32 n) { (void)id; memmove(d, s, n); return 0; }

static VO_AUDIO_CODECAPI g_api;
static VO_MEM_OPERATOR g_mo = { m_alloc, m_free, m_set, m_copy, m_check, m_compare, m_move };

static AAC_ENCODER *open_enc(uint32_t rate, uint32_t bps, uint32_t channels, VO_HANDLE *h)
{
	VO_CODEC_INIT_USERDATA ud;
	AACENC_PARAM prm;

	ud.memflag = VO_IMF_USERMEMOPERATOR;
	ud.memData = &g_mo;
	prm.sampleRate = (int)rate;
	prm.bitRate = (int)bps;
	prm.nChannels = (short)channels;
	prm.adtsUsed = 0;
	voGetAACEncAPI(&g_api);
	CHECK(!g_api.Init(h, VO_AUDIO_CodingAAC, &ud) && !g_api.SetParam(*h, VO_PID_AAC_ENCPARAM, &prm), "an encoder at %u b/s", (unsigned)bps);
	return (AAC_ENCODER *)*h;
}

/* Frames of a signal that fills the reservoir (a quiet tone) and spends it (full-scale noise), a second of each. */
static void fill_frame(uint32_t n, uint32_t channels, int16_t *out)
{
	for (uint32_t i = 0; i < BTAAC_FRAME; i++) {
		uint32_t t = n * BTAAC_FRAME + i, p = t % (48000u + 12000u);

		for (uint32_t c = 0; c < channels; c++) {
			if (p < 48000u)
				out[i * channels + c] = (int16_t)(2500.0 * sin(2 * 3.14159265358979 * (440.0 + 220.0 * c) * (double)t / 48000.0));
			else
				out[i * channels + c] = (int16_t)(btaac_hash32(t * 2u + c) >> 16);
		}
	}
}

static void t_record(void)
{
	static const uint32_t rates[] = { 8000, 64000, 128000, 192000, 256000, 320000 };

	for (unsigned k = 0; k < sizeof(rates) / sizeof(rates[0]); k++)
		for (uint32_t ch = 1; ch <= 2; ch++) {
			VO_HANDLE h;
			uint32_t bps;
			AAC_ENCODER *e;
			ELEMENT_BITS *eb;
			int avg, natural;

			bps = btaac_clamp_bps(rates[k], 48000, ch, 0);
			e = open_enc(48000, bps, ch, &h);
			eb = &e->qcKernel.elementBits;
			avg = (int)(bps / 48000u * 1024u + (bps % 48000u) * 1024u / 48000u);
			natural = (int)(ch * 6144u) - avg;
			natural -= natural & 7;
			CHECK(e->qcKernel.averageBitsTot == avg && eb->maxBits == (int)(ch * 6144u) && eb->maxBitResBits == natural && eb->bitResLevel == natural,
			      "%u channel(s) at %u b/s: average %d (%d), reservoir %d (%d), level %d, scale %d", (unsigned)ch, (unsigned)bps, e->qcKernel.averageBitsTot, avg, eb->maxBitResBits, natural, eb->bitResLevel, eb->maxBits);
			g_api.Uninit(h);
		}
}

static void t_refusals(void)
{
	VO_HANDLE h;
	AAC_ENCODER *e = open_enc(48000, 256000, 2, &h);
	ELEMENT_BITS *eb = &e->qcKernel.elementBits;
	int natural = eb->maxBitResBits;

	CHECK(btaac_enc_limit(NULL, 100) < 0 && btaac_enc_limit(h, 0) < 0, "no handle, no size");
	eb->maxBitResBits = (Word16)(natural + 8);                 /* a record that is not the set-up's, each field in turn */
	CHECK(btaac_enc_limit(h, 100) < 0 && eb->maxBitResBits == natural + 8 && eb->maxBits == 12288 && eb->bitResLevel == natural, "a reservoir that is not the set-up's: refused, nothing changed");
	eb->maxBitResBits = (Word16)natural;
	eb->bitResLevel = (Word16)(natural - 8);
	CHECK(btaac_enc_limit(h, 100) < 0 && eb->maxBitResBits == natural && eb->bitResLevel == natural - 8, "a level that is not full: refused (a frame has been encoded)");
	eb->bitResLevel = (Word16)natural;
	eb->maxBits = 6144;
	CHECK(btaac_enc_limit(h, 100) < 0 && eb->maxBits == 6144, "another scale: refused");
	eb->maxBits = 12288;
	e->qcKernel.averageBitsTot = 0;
	CHECK(btaac_enc_limit(h, 100) < 0, "no average: refused");
	e->qcKernel.averageBitsTot = 5461;
	e->qcKernel.nChannels = 3;
	CHECK(btaac_enc_limit(h, 100) < 0, "three channels: refused");
	e->qcKernel.nChannels = 2;
	CHECK(btaac_enc_limit(h, 853) == 1 && btaac_enc_limit(h, 2000) == 1 && eb->maxBits == 12288 && eb->maxBitResBits == natural, "a reservoir already no larger than asked for: left as it is (%d bytes of it)", natural / 8);
	CHECK(btaac_enc_limit(h, 100) == 0 && eb->maxBitResBits == 800 && eb->bitResLevel == 800 && eb->maxBits == 800, "100 bytes: the reservoir, its level and the rate control's scale are 800 bits");
	CHECK(btaac_enc_limit(h, 50) < 0 && eb->maxBitResBits == 800, "a second call is refused: the record is no longer the set-up's");
	g_api.Uninit(h);
}

/* Every frame is at most the average frame (to a whole byte) plus the reservoir, whatever the reservoir and the rate. */
static void t_bound(void)
{
	static const uint32_t rates[] = { 64000, 128000, 192000, 256000, 320000 };
	static int16_t pcm[BTAAC_FRAME * 2];
	static uint8_t out[1536 + 64];
	unsigned runs = 0;

	for (unsigned k = 0; k < sizeof(rates) / sizeof(rates[0]); k++)
		for (uint32_t res = BTAAC_RES_MIN; res < 900; res += (res < 64 ? 8 : 97)) {
			VO_HANDLE h;
			AAC_ENCODER *e = open_enc(48000, rates[k], 2, &h);
			uint32_t max = 0, total = 0, bound = btaac_frame_bytes(rates[k], 48000) + res;
			int r = btaac_enc_limit(h, res);

			CHECK(r >= 0, "limit %u at %u b/s: %d", (unsigned)res, (unsigned)rates[k], r);
			if (r == 1)
				bound = btaac_frame_bytes(rates[k], 48000) + (uint32_t)e->qcKernel.elementBits.maxBitResBits / 8u;   /* the encoder's own, smaller */
			for (uint32_t f = 0; f < 200; f++) {
				VO_CODECBUFFER in, ob;
				VO_AUDIO_OUTPUTINFO oi;

				fill_frame(f, 2, pcm);
				in.Buffer = (VO_PBYTE)pcm; in.Length = sizeof(pcm); in.Time = 0;
				ob.Buffer = out; ob.Length = sizeof(out); ob.Time = 0;
				CHECK(!g_api.SetInputData(h, &in) && !g_api.GetOutputData(h, &ob, &oi), "encode");
				if (ob.Length > max)
					max = ob.Length;
				total += ob.Length;
			}
			CHECK(btaac_assert_failed == 0, "the encoder's own checks");
			CHECK(max <= bound, "%u b/s, reservoir %u: the longest frame %u bytes is over %u", (unsigned)rates[k], (unsigned)res, (unsigned)max, (unsigned)bound);
			CHECK(total * 8.0 > 0.95 * rates[k] * 200 * 1024.0 / 48000.0 && total * 8.0 < 1.05 * rates[k] * 200 * 1024.0 / 48000.0, "%u b/s, reservoir %u: %u bytes in 200 frames", (unsigned)rates[k],
			      (unsigned)res, (unsigned)total);
			g_api.Uninit(h);
			runs++;
		}
	printf("  %u rate and reservoir pairs: no frame over the average plus the reservoir\n", runs);
}

int main(void)
{
	t_record();
	printf("ok    the encoder's record is what the limit expects (every rate, one and two channels)\n");
	t_refusals();
	printf("ok    a record that is not the expected one is refused and left alone\n");
	t_bound();
	printf("ok    no frame is longer than the average frame plus the reservoir\n");
	printf("test_btaac_enclimit: all passed\n");
	return 0;
}
