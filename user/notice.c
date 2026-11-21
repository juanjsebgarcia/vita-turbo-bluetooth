/* notice.c - see notice.h. */
#include "notice.h"

static int put(uint16_t *out, int n, unsigned cp)
{
	if (n >= NOTICE_UNITS_MAX || cp < 0x20 || cp == 0x7F)
		return -1;
	out[n] = (uint16_t)cp;
	return n + 1;
}

int notice_build(uint8_t *block, const char *tmpl, int level)
{
	uint16_t *out = (uint16_t *)(void *)block;
	const unsigned char *s = (const unsigned char *)tmpl;
	int n = 0;

	for (unsigned i = 0; i < NOTICE_BLOCK_SIZE; i++)
		block[i] = 0;
	if (!s || level < 0 || level > 100)
		return -1;
	while (*s && n >= 0) {
		unsigned cp;

		if (s[0] == '{' && s[1] == 'n' && s[2] == '}') {
			char d[4];
			int k = 0;

			if (level >= 100)
				d[k++] = '1';
			if (level >= 10)
				d[k++] = (char)('0' + (level / 10) % 10);
			d[k++] = (char)('0' + level % 10);
			for (int j = 0; j < k && n >= 0; j++)
				n = put(out, n, (unsigned)d[j]);
			s += 3;
			continue;
		}
		if (s[0] < 0x80) {
			cp = s[0];
			s += 1;
		} else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
			cp = ((unsigned)(s[0] & 0x1F) << 6) | (s[1] & 0x3Fu);
			s += 2;
		} else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
			cp = ((unsigned)(s[0] & 0x0F) << 12) | ((unsigned)(s[1] & 0x3F) << 6) | (s[2] & 0x3Fu);
			s += 3;
		} else {
			n = -1;                      /* not UTF-8, or beyond the 16 bits the table uses */
			break;
		}
		n = put(out, n, cp);
	}
	if (n <= 0) {
		for (unsigned i = 0; i < NOTICE_BLOCK_SIZE; i++)
			block[i] = 0;
		return -1;
	}
	return n;
}
