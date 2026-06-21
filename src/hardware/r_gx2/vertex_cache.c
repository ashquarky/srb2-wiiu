//
// Created by ash on 21/6/26.
//

#include "vertex_cache.h"
#include <gx2/mem.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void vcache_init(struct vertex_arena *arena, const size_t size, const unsigned int align,
                 const unsigned int invalidate_flag, const boolean swap) {
	arena->cache_size = size;
	arena->align_mask = align - 1;
	arena->offset = 0;
	arena->invalidate_flag = invalidate_flag;
	arena->cache = aligned_alloc(align, size);
	arena->swap = swap;
}

struct block vcache_add(struct vertex_arena *arena, const void *data, const size_t size) {
	if (arena->cache_size < arena->offset + size) {
		printf("Vertex cache full! %d / %d.\n", arena->offset, arena->cache_size);
		return (struct block){NULL, 0};
	}

	unsigned char *block = arena->cache + arena->offset;
	if (arena->swap) {
		const unsigned char *src = data;
		for (unsigned int i = 0; i < size; i += 4) {
			block [i + 0] = src[i + 3];
			block [i + 1] = src[i + 2];
			block [i + 2] = src[i + 1];
			block [i + 3] = src[i + 0];
		}
	} else {
		memcpy(block, data, size);
	}

	GX2Invalidate(arena->invalidate_flag, block, size);

	arena->offset += size;
	arena->offset = (arena->offset + arena->align_mask) & ~arena->align_mask;

	return (struct block){block, size};
}

void vcache_reset(struct vertex_arena *arena) {
	arena->offset = 0;
}

void vcache_destroy(struct vertex_arena *arena) {
	free(arena->cache);
	arena->cache = NULL;
	arena->cache_size = 0;
}
