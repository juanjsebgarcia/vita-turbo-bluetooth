/*
 * notice.h - the system notification's request block. Pure code: no system calls (tested on a computer).
 *
 * The block the system's send call reads is NOTICE_BLOCK_SIZE (0x470) bytes. Layout: UTF-16 text at +0 (1 to 63 units, then a NUL; no control characters), an ASCII launch word at +0x80 (empty here),
 * two zero words at +0x468 and +0x46C.
 */
#ifndef BTT_NOTICE_H
#define BTT_NOTICE_H

#include <stdint.h>

#define NOTICE_BLOCK_SIZE  0x470u
#define NOTICE_UNITS_MAX   63

/* Zeroes `block` (NOTICE_BLOCK_SIZE bytes) and writes `tmpl` (UTF-8) into it as UTF-16, with {n} replaced by `level` in decimal.
 * Returns the number of units written (1..63), or -1 if the text is empty, longer than 63 units or not valid UTF-8 (a text with a
 * control character is refused too). */
int notice_build(uint8_t *block, const char *tmpl, int level);

#endif
