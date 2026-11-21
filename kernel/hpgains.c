/*
 * hpgains.c - the sound presets as band gains. Plain C with no kernel calls, so the host
 * test compiles this file as it is.
 */
#include "hpgains.h"

/* dB per band: bass, low mids, mids, presence, treble. */
static const signed char g_hp[BTT_SOUND_COUNT][BTT_BAND_COUNT] = {
	[BTT_SOUND_ORIGINAL]  = {  0,  0,  0,  0,  0 },
	[BTT_SOUND_WARM]      = {  3,  1,  0, -1, -1 },
	[BTT_SOUND_BRIGHT]    = {  0,  0,  0,  2,  3 },
	[BTT_SOUND_BASS]      = {  6,  3,  0,  0,  0 },
	[BTT_SOUND_LIVELY]    = {  4,  1, -2,  1,  4 },
};

const signed char *btt_hp_preset_row(int preset)
{
	if (preset > BTT_SOUND_ORIGINAL && preset < BTT_SOUND_COUNT && preset != BTT_SOUND_CUSTOM)
		return g_hp[preset];
	return 0;
}

static int hp_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int btt_hp_gains(const int *st, int gain[BTT_BAND_COUNT])
{
	int i, any = 0;
	int preset = st[BTT_KEY_SOUND];
	const signed char *g = btt_hp_preset_row(preset);

	for (i = 0; i < BTT_BAND_COUNT; i++) {
		int v = 0;
		if (st[BTT_KEY_ENABLE])
			v = preset == BTT_SOUND_CUSTOM ? BTT_BAND_DB(st[BTT_KEY_BAND1 + i]) : g ? g[i] : 0;
		gain[i] = hp_clamp(v, -12, 12);
		any |= gain[i] != 0;
	}
	return any;
}

int btt_hp_mix(const int *st, int *mode, int *balance)
{
	int ch = st[BTT_KEY_CHANNELS], bal = st[BTT_KEY_BALANCE];

	*mode = 0;
	*balance = BTT_BAL_CENTRE;
	if (!st[BTT_KEY_ENABLE])
		return 0;
	*mode = ch == BTT_CH_MONO || ch == BTT_CH_SWAP ? ch : BTT_CH_STEREO;
	*balance = bal >= 0 && bal < BTT_BAL_STEPS ? bal : BTT_BAL_CENTRE;
	return *mode != BTT_CH_STEREO || *balance != BTT_BAL_CENTRE;
}

uint32_t btt_hp_volsel(const int *st)
{
	int curve = st[BTT_KEY_VOLCURVE], max = st[BTT_KEY_VOLMAX];

	if (!st[BTT_KEY_ENABLE] || curve < 0 || curve >= BTT_VC_COUNT || max < 0 || max >= BTT_VOLMAX_COUNT)
		return 0;
	return (uint32_t)(curve * BTT_VOLMAX_COUNT + max);
}
