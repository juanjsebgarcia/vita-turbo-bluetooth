/*
 * btldac_mem.h - the add-on's static block for libldac's allocations (btldac_mem.c).
 */
#ifndef BTLDAC_MEM_H
#define BTLDAC_MEM_H

#include <stddef.h>
#include <stdint.h>

#define BTLDAC_HEAP_BYTES  (32u * 1024u)   /* libldac asks for about 23.5 KB (measured on the host; the ARM build a little less) */

void  btldac_heap_reset(void);             /* clears the block: all handles must have been freed or abandoned */
void *btldac_malloc(size_t n);
void *btldac_calloc(size_t nmemb, size_t size);
void  btldac_free(void *p);                /* nothing: see btldac_mem.c */
void  btldac_heap_stats(uint32_t *used, uint32_t *peak, uint32_t *allocs, uint32_t *refused);

#endif
