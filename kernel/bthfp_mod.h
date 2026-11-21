#ifndef BTT_BTHFP_MOD_H
#define BTT_BTHFP_MOD_H

#include <stdint.h>

/* The headset battery code (bthfp_mod.c), driven by main.c's worker. */

/* The default of the internal switch (key BTT_KEY_HFP) and its highest value: 0 off, 4 the whole of it. Modes 1 and 3 are
 * subsets (see bthfp_mod.c). */
#define BTT_HFP_MODE_ON   4
#define BTT_HFP_MODE_MAX  4

/* Once per worker pass, after the start-up period: bring the mode in force to `mode` (0 off .. BTT_HFP_MODE_MAX). Nothing is
 * installed until the mode is first on. */
void btt_hfp_sync(int mode);

/* Module stop: the answers off, the service table back, the hook out, the level forgotten. */
void btt_hfp_restore(void);

/* The audio policy's data segment, once, and only if both ends of it resolve; 0 otherwise. */
uintptr_t btt_hfp_av_seg1(void);

#endif
