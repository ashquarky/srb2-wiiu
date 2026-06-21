//
// Created by ash on 21/6/26.
//

#ifndef SRB2_VERTEX_CACHE_H
#define SRB2_VERTEX_CACHE_H

#include <stddef.h>
#include "../../doomtype.h"

// in the spirit of doom engine. an arena allocator!
struct vertex_arena {
	size_t cache_size;
	unsigned char *cache;
	size_t offset;
	unsigned int align_mask;
	unsigned int invalidate_flag;
	boolean swap;
};

struct block {
	void *data;
	size_t size;
};

void vcache_init(struct vertex_arena *arena, size_t size, unsigned int align, unsigned int invalidate_flag,
                 boolean swap);

struct block vcache_add(struct vertex_arena *arena, const void *data, size_t size);

void vcache_reset(struct vertex_arena *arena);

void vcache_destroy(struct vertex_arena *arena);

#endif //SRB2_VERTEX_CACHE_H
