/*
 * imports.h - is a weak import connected yet?
 *
 * Every library the module calls is a weak import, so the module loads even when a library is not loaded in the process (or the
 * kernel module is not there). The loader then leaves the stub as `sub r12, pc, #8 ; mov pc, #0` and patches it once the library
 * loads; calling it before that jumps to address 0 and takes SceShell down. Check before the first call into any such library.
 */
#ifndef BTT_USER_IMPORTS_H
#define BTT_USER_IMPORTS_H

#include <stdint.h>

static inline int btt_import_ready(const void *fn)
{
	if (fn == 0)
		return 0;
	const volatile uint32_t *w = (const volatile uint32_t *)((uintptr_t)fn & ~(uintptr_t)1);
	return !(w[0] == 0xE24FC008u && w[1] == 0xE3A0F000u);
}

#endif
