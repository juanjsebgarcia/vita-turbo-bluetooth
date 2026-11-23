/*
 * btaptx_leaf.c - the functions the audio sender's thread calls in the aptX add-on, and nothing else: this object file is checked whole by
 * tools/check_bteq_leaf.sh (no system call, no vector or floating-point instruction, and no call but the encoders' own routines in the vendored library).
 * It runs inline in the Bluetooth code's audio sender, once per 10.67 ms block, as the console's own SBC encoder does.
 *
 * The encoders take four stereo frames at a time, the left and the right samples in two arrays of 32-bit integers (a 16-bit sample as it is for aptX; for aptX HD the
 * same sample shifted up by 8 into its 24-bit range), and give back one codeword for each channel. The bytes are packed here, most significant first, as the packets
 * carry them (2 bytes a channel for aptX, 3 for aptX HD).
 */
#include "btaptx.h"
#include "../third_party/aptxenc/classic/include/aptXbtenc.h"
#include "../third_party/aptxenc/hd/include/aptXHDbtenc.h"

void *volatile btaptx_ctx;
void *volatile btaptx_ctx_hd;
volatile uint32_t btaptx_running;
volatile uint32_t btaptx_n_blocks, btaptx_n_errors;

uint32_t btaptx_encode(const int16_t *pcm, uint8_t *out)
{
	void *ctx = btaptx_ctx;
	int32_t l[4], r[4];
	uint16_t cw[2];   /* (the encoder writes two 16-bit words here) */
	uint32_t i, k;

	if (btaptx_running != 1u || !ctx || !pcm || !out)
		return 0;
	for (i = 0; i < BTAPTX_BLOCK_FRAMES / 4u; i++) {
		for (k = 0; k < 4u; k++) {   /* four frames: the stereo pairs split into the left and the right */
			l[k] = pcm[i * 8u + 2u * k];
			r[k] = pcm[i * 8u + 2u * k + 1u];
		}
		if (aptxbtenc_encodestereo(ctx, l, r, cw) != 0) {
			btaptx_n_errors++;
			return 0;
		}
		out[i * 4u + 0u] = (uint8_t)(cw[0] >> 8);
		out[i * 4u + 1u] = (uint8_t)cw[0];
		out[i * 4u + 2u] = (uint8_t)(cw[1] >> 8);
		out[i * 4u + 3u] = (uint8_t)cw[1];
	}
	btaptx_n_blocks++;
	return BTAPTX_BLOCK_BYTES;
}

/* aptX HD: the same call with the 24-bit samples; the encoder makes two 24-bit codewords for every four frames (6 bytes: 768 for the block). */
uint32_t btaptx_encode_hd(const int16_t *pcm, uint8_t *out)
{
	void *ctx = btaptx_ctx_hd;
	int32_t l[4], r[4];
	uint32_t cw[2];
	uint32_t i, k;

	if (btaptx_running != 2u || !ctx || !pcm || !out)
		return 0;
	for (i = 0; i < BTAPTX_HD_BLOCK_FRAMES; i++) {
		for (k = 0; k < 4u; k++) {
			l[k] = (int32_t)pcm[i * 8u + 2u * k] * 256;
			r[k] = (int32_t)pcm[i * 8u + 2u * k + 1u] * 256;
		}
		if (aptxhdbtenc_encodestereo(ctx, l, r, cw) != 0) {
			btaptx_n_errors++;
			return 0;
		}
		out[i * BTAPTX_HD_FRAME_BYTES + 0u] = (uint8_t)(cw[0] >> 16);
		out[i * BTAPTX_HD_FRAME_BYTES + 1u] = (uint8_t)(cw[0] >> 8);
		out[i * BTAPTX_HD_FRAME_BYTES + 2u] = (uint8_t)cw[0];
		out[i * BTAPTX_HD_FRAME_BYTES + 3u] = (uint8_t)(cw[1] >> 16);
		out[i * BTAPTX_HD_FRAME_BYTES + 4u] = (uint8_t)(cw[1] >> 8);
		out[i * BTAPTX_HD_FRAME_BYTES + 5u] = (uint8_t)cw[1];
	}
	btaptx_n_blocks++;
	return BTAPTX_HD_BLOCK_BYTES;
}
