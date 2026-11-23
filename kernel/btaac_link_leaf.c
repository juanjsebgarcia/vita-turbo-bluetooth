/*
 * btaac_link_leaf.c - the two leaves of the AAC module as the audio sender's hooks will call them, and nothing else: this object file is
 * checked whole by tools/check_bteq_leaf.sh. Each is one load of the table's pointer and one indirect call (which carries no relocation, so
 * the check sees no call), or nothing when the AAC module is not there. The functions behind the pointers are leaves too (btaac_leaf.c).
 */
#include "btaac_api.h"
#include "btaac_link.h"

extern const BtAacApi *volatile btaac_link_api;

uint32_t btt_aac_push_pcm(const int16_t *pcm, uint32_t frames)
{
	const BtAacApi *a = btaac_link_api;

	return a ? a->push_pcm(pcm, frames) : 0u;
}

uint32_t btt_aac_pop_au(uint8_t *out, uint32_t cap, uint32_t *seq)
{
	const BtAacApi *a = btaac_link_api;

	return a ? a->pop_au(out, cap, seq) : 0u;
}

uint32_t btt_aac_au_trim(uint32_t keep)
{
	const BtAacApi *a = btaac_link_api;

	return a ? a->au_trim(keep) : 0u;
}

uint32_t btt_aac_au_depth(void)
{
	const BtAacApi *a = btaac_link_api;

	return a ? a->au_depth() : 0u;
}
