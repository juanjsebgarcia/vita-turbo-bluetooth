#ifndef BTT_BTQUAL_H
#define BTT_BTQUAL_H

/* Bluetooth audio quality, the bitpool cap of Stable and Balanced: put the cap in place or take it out, to match
 * `level` (BttQuality: Stable 37, Balanced 45, any other level none). Called from the worker on every pass
 * once the start-up period is over, with Standard when Bluetooth Turbo is off. The hook on the routine that
 * applies an A2DP codec configuration (checked byte for byte first) goes in the first time Stable or Balanced is wanted
 * (or High or Highest, which share it: bthq.c); it stays in, writing nothing, until module stop. The cap takes effect
 * from the next connection of the headphones. Cheap when nothing changes. */
void btt_btqual_sync(int level);

/* Module stop: the hook out. */
void btt_btqual_restore(void);

/* High and Highest (bthq.c) share this hook: make sure it is in (0), or say it cannot be (-1), and set what the
 * hook does for them (0 = nothing, BTHQ_HIGH and BTHQ_HIGHEST as in btqual_hook.h). */
int btt_btqual_hq_hook(void);
void btt_btqual_hq_set(int mode);

#endif
