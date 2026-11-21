/*
 * btvol_map.c - see btvol_map.h. Table lookups and comparisons only: it runs inside the hooks on
 * the console's volume calls, so it makes no call and uses no division or floating point
 * (tools/check_bteq_leaf.sh checks the object file).
 */
#include "btvol_map.h"

/* The step whose entry in `tab` is nearest to `vol` (the lower step on a tie). `tab` increases. */
static uint32_t nearest(const uint8_t *tab, uint32_t vol)
{
	uint32_t i = 0;

	while (i + 1 < BV_STEPS && tab[i + 1] <= vol)
		i++;                       /* the last entry that is not above vol */
	if (i + 1 < BV_STEPS && (uint32_t)tab[i + 1] - vol < vol - tab[i])
		i++;                       /* the next one is strictly closer */
	return i;
}

uint32_t bv_send_map(uint32_t sel, uint32_t vol)
{
	if (!sel || sel >= BV_TABLES || vol > 127)
		return vol;
	return BV_TAB[sel][nearest(BV_STOCK, vol)];
}

uint32_t bv_read_map(uint32_t sel, uint32_t vol)
{
	if (!sel || sel >= BV_TABLES || vol > 127)
		return vol;
	return BV_STOCK[nearest(BV_TAB[sel], vol)];
}
