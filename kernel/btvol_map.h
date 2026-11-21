/*
 * btvol_map.h - the Bluetooth volume mapping (btvol_map.c): plain C with no kernel headers, so the
 * host test (test/test_btvol.c) compiles it as it is. The hooks (btvol_hook.c) are leaves and call
 * only these two functions.
 *
 * "sel" picks a table: curve * BV_MAXES + maximum (BttVolCurve, then the BttVolMax index). Table 0 is
 * Standard with 100 %: the console's own numbers, which both functions then return unchanged
 * whatever they are given.
 */
#ifndef BTT_BTVOL_MAP_H
#define BTT_BTVOL_MAP_H

#include <stdint.h>

#include "btvol_table.h"

/* The number to send to the headphones, for the volume number the console asked to send (0..127,
 * the console's own number for one of its 31 steps; any other value is taken to the nearest step). */
uint32_t bv_send_map(uint32_t sel, uint32_t vol);

/* The volume number to hand the console, for the one the headphones reported (0..127): the exact
 * inverse of bv_send_map on every number it can produce, and the nearest step for any other. */
uint32_t bv_read_map(uint32_t sel, uint32_t vol);

#endif
