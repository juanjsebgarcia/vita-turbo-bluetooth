/*
 * bluetoothturbo_i18n.h - translations for the "Bluetooth Turbo" Settings page, in all
 * 20 system languages (English is authoritative, the rest machine-translated;
 * the strings live in tools/gen_i18n.py). Same scheme as Wi-Fi Turbo's.
 */
#ifndef BLUETOOTHTURBO_I18N_H
#define BLUETOOTHTURBO_I18N_H

/* Matches SCE_SYSTEM_PARAM_LANG_* and /CONFIG/SYSTEM/language. */
enum btt_lang_id {
	BTT_LANG_JAPANESE = 0, BTT_LANG_ENGLISH_US = 1, BTT_LANG_FRENCH, BTT_LANG_SPANISH,
	BTT_LANG_GERMAN, BTT_LANG_ITALIAN, BTT_LANG_DUTCH, BTT_LANG_PORTUGUESE_PT,
	BTT_LANG_RUSSIAN, BTT_LANG_KOREAN, BTT_LANG_CHINESE_T, BTT_LANG_CHINESE_S,
	BTT_LANG_FINNISH, BTT_LANG_SWEDISH, BTT_LANG_DANISH, BTT_LANG_NORWEGIAN,
	BTT_LANG_POLISH, BTT_LANG_PORTUGUESE_BR, BTT_LANG_ENGLISH_GB, BTT_LANG_TURKISH,
	BTT_LANG_COUNT
};

#include "bluetoothturbo_i18n_ids.h"

#define BTT_I18N_ENUM(name) BTT_I18N_##name,
enum btt_i18n_id { BTT_I18N_IDS(BTT_I18N_ENUM) BTT_I18N_COUNT };
#undef BTT_I18N_ENUM

int btt_i18n_lang(void);
const char *btt_i18n(int id);                    /* never NULL, English fallback */
int btt_i18n_to_u16(const char *utf8, unsigned short *out, int out_max);

#endif /* BLUETOOTHTURBO_I18N_H */
