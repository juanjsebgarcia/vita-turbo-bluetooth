/*
 * btaac_api.h - what the AAC module (bluetoothturbo_aac.skprx) gives the main kernel module: one table of functions, handed over by the
 * module's one export, btaacGetApi. The AAC module is optional (installing it is the opt-in); the main module looks the export up at
 * run time and works exactly as before when it is not there.
 *
 * Why a table behind one export, and why the main module pulls it (rather than the AAC module registering itself):
 *   - one name to look up, one number to check (BTAAC_NID_GET_API, verified against the build by tools/check_aac_nid.py), a version and
 *     a size in the table so a pair of builds that disagree is refused, not run;
 *   - no load order: neither module needs the other to start, the AAC module needs nothing of the main one, and the main module looks
 *     once its own start-up period is over, long after every *KERNEL module has loaded, so a plugin list in either order works;
 *   - no state to unwind: a registration would have to be undone when either module stops; a table that is only read has nothing to undo.
 *     The one hazard of any such link, calling into a module that has been unloaded, is closed on the AAC side: its module_stop quiesces
 *     the engine and then asks not to be unloaded (btaac_main.c), so a pointer taken once stays valid.
 */
#ifndef BTAAC_API_H
#define BTAAC_API_H

#include "btaac.h"

/* Any change to BtAacInfo (btaac.h) or to the table bumps this: the main module fills a BtAacInfo of its own across the module boundary. */
#define BTAAC_API_VERSION   4

/* The number of btaacGetApi in the module's export table (the library's name is not used: the lookup takes any library). It is the one the
 * build gives the name in bluetoothturbo_aac.yml; tools/check_aac_nid.py fails the build's tests if the two ever differ. */
#define BTAAC_NID_GET_API   0x5CFD5B5Fu

typedef struct BtAacApi {
	uint32_t size;                  /* sizeof(BtAacApi) of the module that made it: the reader needs at least its own */
	uint32_t version;               /* BTAAC_API_VERSION */
	int      (*start)(uint32_t kbps, uint32_t rate, uint32_t channels, uint32_t max_kbps, int accel, uint32_t mtu);
	void     (*stop)(void);
	void     (*sync)(int on);       /* the self-test, once */
	void     (*get_info)(BtAacInfo *out);
	uint32_t (*push_pcm)(const int16_t *pcm, uint32_t frames);            /* the leaves: see btaac_leaf.c */
	uint32_t (*pop_au)(uint8_t *out, uint32_t cap, uint32_t *seq);
	uint32_t (*au_trim)(uint32_t keep);                                   /* drops the oldest waiting units down to `keep` */
	uint32_t (*au_depth)(void);                                           /* the units of the session waiting in the output ring */
} BtAacApi;

#endif /* BTAAC_API_H */
