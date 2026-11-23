/*
 * btaptx_api.h - what the aptX add-on (bluetoothturbo_aptx.skprx) gives the main kernel module: one table of functions behind the module's one export,
 * btaptxGetApi, found at run time exactly as the AAC module's (kernel/btaac_api.h says why a table behind one export, pulled by the main module).
 */
#ifndef BTAPTX_API_H
#define BTAPTX_API_H

#include "btaptx.h"

/* Any change to BtAptxInfo or to the table bumps this: the main module fills a BtAptxInfo of its own across the module boundary. */
#define BTAPTX_API_VERSION  2   /* 2: aptX HD (start takes the mode, encode_hd, the HD fields of the information) */

/* The number of btaptxGetApi in the module's export table: the one the build gives the name in bluetoothturbo_aptx.yml; tools/check_aac_nid.py fails the
 * build's tests if the two ever differ. */
#define BTAPTX_NID_GET_API  0x144737D5u

typedef struct BtAptxApi {
	uint32_t size;                  /* sizeof(BtAptxApi) of the module that made it: the reader needs at least its own */
	uint32_t version;               /* BTAPTX_API_VERSION */
	int      (*start)(int hd);      /* a session: aptX HD when hd */
	void     (*stop)(void);
	void     (*sync)(int on);
	void     (*get_info)(BtAptxInfo *out);
	uint32_t (*encode)(const int16_t *pcm, uint8_t *out);   /* the leaf */
	uint32_t (*encode_hd)(const int16_t *pcm, uint8_t *out);   /* the HD leaf: 768 bytes */
} BtAptxApi;

#endif /* BTAPTX_API_H */
