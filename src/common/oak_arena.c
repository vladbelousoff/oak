#include "oak_arena.h"

#include <string.h>

typedef struct oak_arena_block oak_arena_block_t;
struct oak_arena_block
{
  oak_arena_block_t* next;
  usize capacity;
  usize used;
  char data[];
};

/* Round n up to the next multiple of (2 * sizeof(usize)). */
static usize align_up(usize n)
{
  const usize align = sizeof(usize) * 2;
  const usize mask = align - 1;
  return (n + mask) & ~mask;
}

static oak_arena_block_t* arena_new_block(oak_allocator_t* a,
                                                 usize capacity)
{
  oak_arena_block_t* block =
      oak_alloc(a, sizeof(oak_arena_block_t) + capacity, OAK_HERE);
  if (!block)
    return OAK_NULL;
  block->next = OAK_NULL;
  block->capacity = capacity;
  block->used = 0;
  return block;
}

void oak_arena_init(oak_arena_t* arena,
                    usize block_size,
                    oak_allocator_t* allocator)
{
  arena->block_size = block_size ? block_size : OAK_ARENA_DEFAULT_BLOCK_SIZE;
  arena->current = OAK_NULL;
  arena->allocator = allocator;
}

void* oak_arena_alloc(oak_arena_t* arena, usize size)
{
  const usize aligned = align_up(size);
  oak_arena_block_t* cur = arena->current;

  if (!cur || cur->used + aligned > cur->capacity)
  {
    usize cap = arena->block_size;
    if (aligned > cap)
      cap = aligned;
    oak_arena_block_t* block = arena_new_block(arena->allocator, cap);
    if (!block)
      return OAK_NULL;
    block->next = arena->current;
    arena->current = block;
    cur = block;
  }

  void* ptr = cur->data + cur->used;
  cur->used += aligned;
  memset(ptr, 0, aligned);
  return ptr;
}

void oak_arena_free(oak_arena_t* arena)
{
  oak_arena_block_t* block = arena->current;
  while (block)
  {
    oak_arena_block_t* next = block->next;
    oak_free(arena->allocator, block, OAK_HERE);
    block = next;
  }
  arena->current = OAK_NULL;
}
