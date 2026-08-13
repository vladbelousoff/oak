#pragma once

#include "oak_allocator.h"
#include "oak_export.h"
#include "oak_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OAK_ARENA_DEFAULT_BLOCK_SIZE 4096

typedef struct oak_arena_block oak_arena_block_t;

typedef struct oak_arena oak_arena_t;
struct oak_arena
{
  oak_arena_block_t* current;
  usize block_size;
  oak_allocator_t* allocator;
};

/* block_size 0 selects OAK_ARENA_DEFAULT_BLOCK_SIZE. Safe to call again after
 * free. */
OAK_API void oak_arena_init(oak_arena_t* arena,
                            usize block_size,
                            oak_allocator_t* allocator);

/* Returns zero-filled storage, aligned to 2 * sizeof(usize). Null on allocation
 * failure. */
OAK_API void* oak_arena_alloc(oak_arena_t* arena, usize size);

OAK_API void oak_arena_free(oak_arena_t* arena);

#ifdef __cplusplus
}
#endif
