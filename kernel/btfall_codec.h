/* btfall_codec.h - the names the codec fallback notification (btfall_core.h) shares with the stream code (btaac_stream.c): no code, so the stream code does not carry the texts. */
#ifndef BTFALL_CODEC_H
#define BTFALL_CODEC_H

/* The codec as a notice names it. aptX HD is its own name: when the HD stream was refused the connection is SBC, and the next one is plain aptX. */
enum { BTFALL_C_NONE = 0, BTFALL_C_AAC, BTFALL_C_APTX, BTFALL_C_APTX_HD, BTFALL_C_LDAC, BTFALL_C_COUNT };
/* Which text (the row in the table): 0 the headphones do not support it, 1 they could not use it. */
enum { BTFALL_K_UNSUPPORTED = 0, BTFALL_K_REFUSED, BTFALL_K_COUNT };

#endif
