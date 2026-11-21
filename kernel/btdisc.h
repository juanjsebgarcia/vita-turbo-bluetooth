#ifndef BTT_BTDISC_H
#define BTT_BTDISC_H

/* What the console does when Bluetooth headphones disconnect: put the change in place or take it out, to
 * match `mode` (BttDisconnect: 0 Mute, the console's own; 1 Pause, Keep Volume; 2 Play on Speaker). Called
 * from the worker on every pass once the start-up period is over, with 0 when Bluetooth Turbo is off. Mute
 * touches nothing (not even a look at the system). Each of the two changes is a few bytes of the audio
 * policy's code, checked byte for byte first, and is heard from the next time headphones disconnect. Cheap
 * when nothing changes. */
void btt_btdisc_sync(int mode);

/* Module stop: the original bytes back. */
void btt_btdisc_restore(void);

#endif
