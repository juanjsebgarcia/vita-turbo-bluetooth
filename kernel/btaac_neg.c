/*
 * btaac_neg.c - the negotiation state of AAC streaming that the hooks, the worker and the audio sender's hook share (kernel/btaac_hook.h), and
 * nothing else. Zero at start: no connection known, AAC not offered.
 */
#include "btaac_hook.h"

volatile BtAacNeg btaac_neg;
volatile BtCdcRec btaac_cdc;
int16_t btaac_apt_pcm[1024] __attribute__((aligned(4)));
