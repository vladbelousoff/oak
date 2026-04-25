#pragma once

#include "oak_types.h"

#define OAK_ARENA_DEFAULT_BLOCK_SIZE 4096

struct oak_arena_block_t;

struct oak_arena_t
{
  struct oak_arena_block_t* current;
  usize block_size;
};

/* block_size 0 selects OAK_ARENA_DEFAULT_BLOCK_SIZE. Safe to call again after
 * free. */
void oak_arena_init(struct oak_arena_t* arena, usize block_size);

/* Returns zero-filled storage, aligned to 2 * sizeof(usize). Null on allocation
 * failure. */
void* oak_arena_alloc(struct oak_arena_t* arena, usize size);

void oak_arena_free(struct oak_arena_t* arena);
