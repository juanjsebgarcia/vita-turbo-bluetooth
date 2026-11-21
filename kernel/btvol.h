#ifndef BTT_BTVOL_H
#define BTT_BTVOL_H

/* Bluetooth volume: the volume curve and the maximum, applied to the volume numbers the console
 * sends to the headphones (AVRCP absolute volume) and, inverted, to the ones they report. Call
 * from the worker on every pass once the start-up period is over. `settings` is the module's
 * settings array (BttKey). Installs the two hooks while Bluetooth Turbo is on and the curve or the
 * maximum is not Standard / 100 %, follows changes, and makes both pass every value through when
 * they are off; the hooks stay in until btt_btvol_restore at module stop. The headphones hear the
 * new mapping from the next volume change. Cheap when nothing changes. */
void btt_btvol_sync(const int *settings);

/* Everything back to stock (module stop). */
void btt_btvol_restore(void);

#endif
