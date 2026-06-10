//
// Created by ash on 10/09/24.
//

#include "r_gx2mm.h"
#include <gx2/enum.h>
#include <malloc.h>
#include <whb/log.h>

#define GX2MM_CHUNK GX2_UNIFORM_BLOCK_ALIGNMENT
#define GX2MM_SMOL 1024
#define GX2MM_BIG 1024

static void *smol_heap = NULL;
static uint32_t smol_heap_size;
static uint32_t smol_heap_offs;

static void *big_allocs[GX2MM_BIG];
static int big_allocs_count;

void gx2mm_init(void) {
	gx2mm_reset();
	if (smol_heap) free(smol_heap);

	smol_heap_size = GX2MM_CHUNK * GX2MM_SMOL;
	smol_heap = memalign(GX2MM_CHUNK, smol_heap_size);
}

void gx2mm_reset(void) {
	smol_heap_offs = 0;

	for (int i = 0; i < big_allocs_count; i++) {
		free(big_allocs[i]);
	}
	big_allocs_count = 0;
}

void *gx2mm_alloc(size_t size, size_t align) {
	if (align <= GX2MM_CHUNK && size <= GX2MM_CHUNK) {
		void *mem = smol_heap + smol_heap_offs;
		smol_heap_offs += GX2MM_CHUNK;
		if (smol_heap_offs <= smol_heap_size) return mem;
	}

	if (big_allocs_count < GX2MM_BIG) {
//		WHBLogWritef("Skipped large allocation of %d/%d bytes\n", size, align);
		void *mem = memalign(align, size);
		big_allocs[big_allocs_count++] = mem;
		return mem;
	}

	return NULL; // :(
}
