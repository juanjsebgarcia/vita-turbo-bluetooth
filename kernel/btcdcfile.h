#ifndef BTT_BTCDCFILE_H
#define BTT_BTCDCFILE_H

#include <stdint.h>

/* The codec lists of the headphones the console has read (btcodecs.h), kept in ur0:data/bluetoothturbo/codecs.bin. The worker is the only caller of both:
 * the file is read and written in its thread, never in a hook (the hook only publishes a finished list in btaac_cdc). */

/* The worker's pass: reads the file the first time, then takes a list the signalling hook finished and keeps it (writing the file only when what is kept changed). */
void btt_codecs_pass(void);

/* The BTT_HC_* bits kept for the device at this address (low word, high 16 bits); 0 when the console has never read its list. */
uint32_t btt_codecs_lookup(uint32_t lo, uint32_t hi);

#endif
