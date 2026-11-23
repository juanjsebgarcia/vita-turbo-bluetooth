/*
 * btaac_enclimit.c - holds the vendored encoder's bit reservoir to a size, so no access unit it writes is longer than a packet of the link takes. It is built
 * with the encoder (twice, like it: CMakeLists.txt gives each build its own symbol prefix) because it reads the encoder's own record; the vendored files stay as
 * they are, and this is the whole of the change this product makes to how the encoder behaves.
 *
 * What it sets, in the encoder's element record (aacenc/inc/qc_data.h), once the encoder has been given its rate and before the first frame:
 *   maxBitResBits  the most the reservoir may hold; a frame is at most the average frame plus the reservoir's level (aacenc/src/qc_main.c: QCMain keeps a channel's
 *                  bits under the average plus the level, and raises the quantiser's gain until it is), so the level's ceiling bounds every frame;
 *   bitResLevel    the level the reservoir starts at: full, as the encoder starts it;
 *   maxBits        the number the rate control divides the level by to find how full the reservoir is (aacenc/src/adj_thr.c, bitresCalcBitFac): set to the new size, so
 *                  "full" still means full and the controller keeps spending and saving as it was tuned to, in a smaller reservoir. Left at its own number the reservoir
 *                  would always look nearly empty, and the controller would save bits it has no room to keep (they would come out as padding).
 * The average bit rate is the encoder's own and does not change: what the reservoir cannot take is written as padding.
 *
 * It checks what it relies on first (the record's layout at the vendored commit: the sizes the encoder's own set-up gives) and does nothing, and says so, when any of it
 * is not as expected, so a different encoder version is caught here and never poked blind.
 */
#include "aacenc_core.h"
#include "config.h"

#define BTAAC_ENCLIMIT_OK        0
#define BTAAC_ENCLIMIT_UNCHANGED 1    /* the encoder's own reservoir is already no larger than asked for */
#define BTAAC_ENCLIMIT_LAYOUT   (-1)  /* the encoder's record is not what this was written against: nothing was changed */

/* `h` is the handle the encoder's Init returned, after SetParam gave it its rate and before the first frame; `res_bytes` the reservoir to allow (at least 1). */
int btaac_enc_limit(void *h, unsigned int res_bytes)
{
	AAC_ENCODER *e = (AAC_ENCODER *)h;
	ELEMENT_BITS *eb;
	int n, natural, bits;

	if (!e || !res_bytes)
		return BTAAC_ENCLIMIT_LAYOUT;
	eb = &e->qcKernel.elementBits;
	n = e->elInfo.nChannelsInEl;
	if ((n != 1 && n != 2) || e->qcKernel.nChannels != n)
		return BTAAC_ENCLIMIT_LAYOUT;
	/* the set-up's own arithmetic (channel_map.c, InitElementBits): the element's most bits, and the reservoir as that less the average frame, to a whole byte */
	natural = n * MAXBITS_COEF - e->qcKernel.averageBitsTot;
	natural -= natural & 7;
	if (e->qcKernel.averageBitsTot <= 0 || eb->maxBits != n * MAXBITS_COEF || eb->maxBitResBits != natural || eb->bitResLevel != natural)
		return BTAAC_ENCLIMIT_LAYOUT;
	if (res_bytes >= (unsigned int)natural / 8u)
		return BTAAC_ENCLIMIT_UNCHANGED;
	bits = (int)(res_bytes * 8u);
	eb->maxBitResBits = (Word16)bits;
	eb->bitResLevel = (Word16)bits;
	eb->maxBits = (Word16)bits;
	return BTAAC_ENCLIMIT_OK;
}
