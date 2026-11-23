#ifndef BTT_BTHQ_H
#define BTT_BTHQ_H

/* Bluetooth audio quality, High and Highest (the channels sent separately): put the changes for `level` (BttQuality)
 * in place or take them out. Called from the worker on every pass once the start-up period is over, with Standard
 * when Bluetooth Turbo is off. Only High and Highest do anything; every other level leaves the console's own code.
 *   High     the packet types allow 2-DH5, the packet cap of headphones limited to the 2 Mb/s radio rate is raised to
 *            match (so they reach their own highest bitpool when dual channel is not offered), and dual channel is
 *            capped at bitpool 38 (fits one 2-DH5 packet; about 490 kb/s)
 *   Highest  dual channel at the headphones' highest bitpool (at most 59), 2-DH5 and 3-DH5 allowed (about 670 kb/s at the usual bitpool 53, 744 kb/s at 59: "about 670–740 kb/s, depending on the headphones");
 *            some headphones play no sound at all at this setting
 * Heard from the next connection of the headphones. Cheap when nothing changes. */
void btt_bthq_sync(int level);

/* LDAC: its packets are built for one 2-DH5 packet (677 bytes), which the 3-slot types stream start allows cannot carry (it would take two 3-DH3 packets each), so while an LDAC
 * stream is wanted the packet types allow 2-DH5 whatever the quality level is (the next sync puts it in; High and Highest already have it). Nothing else changes: no cap,
 * no hook. Like the levels, heard from the next connection of the headphones. */
void btt_bthq_set_ldac(int on);

/* Module stop: everything back to stock. */
void btt_bthq_restore(void);

#endif
