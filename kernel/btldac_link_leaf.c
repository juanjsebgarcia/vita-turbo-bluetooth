/*
 * btldac_link_leaf.c - the LDAC add-on's ring routines as the audio sender's hooks call them, and nothing else: this object file is checked whole by
 * tools/check_bteq_leaf.sh. Each is one load of the table's pointer and one indirect call (which carries no relocation, so the check sees no call), or nothing when the
 * add-on is not there. The functions behind the pointers are leaves too (btldac_leaf.c).
 */
#include "btldac_api.h"
#include "btldac_link.h"

extern const BtLdacApi *volatile btldac_link_api;

uint32_t btt_ldac_push_pcm(const int16_t *pcm, uint32_t frames)
{
	const BtLdacApi *a = btldac_link_api;

	return a ? a->push_pcm(pcm, frames) : 0u;
}

uint32_t btt_ldac_pop_pkt(uint8_t *out, uint32_t cap, uint32_t *nframes, uint32_t *seq)
{
	const BtLdacApi *a = btldac_link_api;

	return a ? a->pop_pkt(out, cap, nframes, seq) : 0u;
}

uint32_t btt_ldac_pkt_trim(uint32_t keep)
{
	const BtLdacApi *a = btldac_link_api;

	return a ? a->pkt_trim(keep) : 0u;
}

uint32_t btt_ldac_pkt_depth(void)
{
	const BtLdacApi *a = btldac_link_api;

	return a ? a->pkt_depth() : 0u;
}
