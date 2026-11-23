/*
 * btldac_mem.c - the memory libldac asks for, served from one static block by a bump allocator. The library allocates about 23 KB when an encoder is opened (the
 * handle with its ring and work buffers, the core's state) and the adaptive code a few bytes, and never again while encoding. The library's allocation calls are
 * renamed to these at build time (CMakeLists.txt: -Dmalloc=btldac_malloc ...); the block is cleared when an encoder is made so that the bytes it makes do not depend on
 * what an earlier session left (the self-test's hash would otherwise be a matter of luck). Nothing is ever freed one by one: the whole block is released by
 * btldac_heap_reset after the handles have been freed.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "btldac_mem.h"

static uint8_t g_heap[BTLDAC_HEAP_BYTES] __attribute__((aligned(8)));
static uint32_t g_used, g_peak, g_allocs, g_refused;

void btldac_heap_reset(void)
{
	memset(g_heap, 0, sizeof(g_heap));
	g_used = 0;
	g_allocs = 0;
	g_refused = 0;
}

void *btldac_malloc(size_t n)
{
	uint32_t size = ((uint32_t)n + 7u) & ~7u;
	void *p;

	if (n > BTLDAC_HEAP_BYTES || g_used + size > BTLDAC_HEAP_BYTES) {
		g_refused++;
		return NULL;
	}
	p = g_heap + g_used;
	g_used += size;
	g_allocs++;
	if (g_used > g_peak)
		g_peak = g_used;
	return p;
}

void *btldac_calloc(size_t nmemb, size_t size)
{
	if (size && nmemb > BTLDAC_HEAP_BYTES / size) {
		g_refused++;
		return NULL;
	}
	return btldac_malloc(nmemb * size);   /* the block is clear already */
}

void btldac_free(void *p)
{
	(void)p;
}

void btldac_heap_stats(uint32_t *used, uint32_t *peak, uint32_t *allocs, uint32_t *refused)
{
	*used = g_used;
	*peak = g_peak;
	*allocs = g_allocs;
	*refused = g_refused;
}
