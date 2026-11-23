/*
 * btldac_api.h - what the LDAC module (bluetoothturbo_ldac.skprx) gives the main kernel module: one table behind the module's one export, btldacGetApi, found at run
 * time exactly as the AAC module's (kernel/btaac_api.h says why a table behind one export, pulled by the main module, and why the module stays loaded once found).
 */
#ifndef BTLDAC_API_H
#define BTLDAC_API_H

#include "btldac.h"

/* Any change to BtLdacInfo or to the table bumps this: the main module fills a BtLdacInfo of its own across the module boundary. */
#define BTLDAC_API_VERSION  1

/* The number of btldacGetApi in the module's export table: the one the build gives the name in bluetoothturbo_ldac.yml; tools/check_aac_nid.py fails the build's tests if
 * the two ever differ. */
#define BTLDAC_NID_GET_API  0x61469f2bu

typedef struct BtLdacApi {
	uint32_t size;                  /* sizeof(BtLdacApi) of the module that made it: the reader needs at least its own */
	uint32_t version;               /* BTLDAC_API_VERSION */
	int      (*start)(uint32_t mode, uint32_t mtu);
	void     (*stop)(void);
	void     (*sync)(int on);
	void     (*set_mode)(uint32_t mode);
	void     (*get_info)(BtLdacInfo *out);
	uint32_t (*push_pcm)(const int16_t *pcm, uint32_t frames);                       /* the leaves: see btldac_leaf.c */
	uint32_t (*pop_pkt)(uint8_t *out, uint32_t cap, uint32_t *nframes, uint32_t *seq);
	uint32_t (*pkt_trim)(uint32_t keep);
	uint32_t (*pkt_depth)(void);
} BtLdacApi;

#endif /* BTLDAC_API_H */
