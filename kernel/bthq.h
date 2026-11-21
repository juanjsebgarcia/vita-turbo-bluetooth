#ifndef BTT_BTHQ_H
#define BTT_BTHQ_H

/* Bluetooth audio quality, High and Highest (the channels sent separately): put the changes for `level` (BttQuality)
 * in place or take them out. Called from the worker on every pass once the start-up period is over, with Standard
 * when Bluetooth Turbo is off. Only High and Highest do anything; every other level leaves the console's own code.
 *   High     the packet types allow 2-DH5, the packet cap of headphones limited to the 2 Mb/s radio rate is raised to
 *            match (so they reach their own highest bitpool when dual channel is not offered), and dual channel is
 *            capped at bitpool 38 (fits one 2-DH5 packet; about 490 kb/s)
 *   Highest  dual channel at the headphones' highest bitpool (at most 59), 2-DH5 and 3-DH5 allowed (up to about 670 kb/s);
 *            some headphones play no sound at all at this setting
 * Heard from the next connection of the headphones. Cheap when nothing changes. */
void btt_bthq_sync(int level);

/* Module stop: everything back to stock. */
void btt_bthq_restore(void);

#endif
