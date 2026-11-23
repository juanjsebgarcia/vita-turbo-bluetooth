#ifndef BTT_BTEQ_H
#define BTT_BTEQ_H

/* Bluetooth sound: apply the sound preset (or its Custom bands), the channel mode and the balance
 * to the audio sent to Bluetooth headphones. Call from the worker on every pass once the start-up
 * period is over (the Home screen up and BTT_MIN_UPTIME seconds of uptime). `settings` is the
 * module's settings array (BttKey). Installs the hook while Bluetooth Turbo is on and the preset is
 * not flat or the channels are not Stereo with the balance centred, follows changes of the settings,
 * and makes the filter flat (passing every block through) when they are off; the hook stays in
 * until btt_bteq_restore at module stop. Cheap when nothing changes. */
void btt_bteq_sync(const int *settings);

/* 1 once the sender's hook is in (AAC streaming offers AAC only then: without it the engine would get no sound). */
int btt_bteq_hook_in(void);

/* Everything back to stock (module stop). */
void btt_bteq_restore(void);

#endif
