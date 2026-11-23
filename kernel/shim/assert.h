/*
 * assert.h - the AAC encoder's consistency checks, without the panic. The vendored encoder includes this by name in one file; the
 * build puts this directory first on the include path. A failed check is counted, and the engine's thread reads the count
 * after every encode call and ends the session with it: a stopped session is better than a kernel panic.
 */
#ifndef BTAAC_SHIM_ASSERT_H
#define BTAAC_SHIM_ASSERT_H

extern volatile unsigned int btaac_assert_failed;

#undef assert   /* the encoder's own headers define a version that does nothing, and may come first */
#define assert(e) ((e) ? (void)0 : (void)(btaac_assert_failed++))

#endif
