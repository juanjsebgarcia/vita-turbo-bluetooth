/*
 * btaptx_link_leaf.c - the add-on's encoder as the audio sender's hook calls it, and nothing else: this object file is checked whole by
 * tools/check_bteq_leaf.sh. One load of the table's pointer and one indirect call (which carries no relocation, so the check sees no call), or nothing when the
 * add-on is not there. The function behind the pointer is a leaf too (btaptx_leaf.c).
 */
#include "btaptx_api.h"
#include "btaptx_link.h"

extern const BtAptxApi *volatile btaptx_link_api;

uint32_t btt_aptx_encode(const int16_t *pcm, uint8_t *out)
{
	const BtAptxApi *a = btaptx_link_api;

	return a ? a->encode(pcm, out) : 0u;
}

uint32_t btt_aptx_encode_hd(const int16_t *pcm, uint8_t *out)
{
	const BtAptxApi *a = btaptx_link_api;

	return a ? a->encode_hd(pcm, out) : 0u;
}
