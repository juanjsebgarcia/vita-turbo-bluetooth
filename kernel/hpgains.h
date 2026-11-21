#ifndef BTT_HPGAINS_H
#define BTT_HPGAINS_H

#include <stdint.h>

#include "../include/bluetoothturbo.h"

/* The five band gains (dB) of a sound preset, or NULL for Original, Custom and
 * anything out of range. */
const signed char *btt_hp_preset_row(int preset);

/* The band gains the settings ask for: all zero when Bluetooth Turbo is off.
 * Custom reads the five custom bands. Table lookups only. Returns 1 if any band
 * is not flat. */
int btt_hp_gains(const int *settings, int gain[BTT_BAND_COUNT]);

/* The channel stage the settings ask for: the mode (BttChannels) and the balance step, forced to
 * Stereo and the centre when Bluetooth Turbo is off or a value is out of range. Returns 1 if the
 * block would be changed (not Stereo, or the balance off centre). */
int btt_hp_mix(const int *settings, int *mode, int *balance);

/* The volume table the settings ask for (curve * BTT_VOLMAX_COUNT + maximum, see btvol_map.h): 0 when
 * Bluetooth Turbo is off, a value is out of range, or both are Standard / 100 %. */
uint32_t btt_hp_volsel(const int *settings);

#endif
