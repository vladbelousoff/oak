#include "oak_arena.h"

#include <string.h>

#include "oak_mem.h"

struct oak_arena_block_t
{
  struct oak_arena_block_t* next;
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

static struct oak_arena_block_t* arena_new_block(usize capacity)
{
  struct oak_arena_block_t* block =
      oak_alloc(sizeof(struct oak_arena_block_t) + capacity, OAK_SRC_LOC);
  if (!block)
    return null;
  block->next = null;
  block->capacity = capacity;
  block->used = 0;
  return block;
}

void oak_arena_init(struct oak_arena_t* arena, usize block_size)
{
  arena->block_size = block_size ? block_size : OAK_ARENA_DEFAULT_BLOCK_SIZE;
  arena->current = null;
}

void* oak_arena_alloc(struct oak_arena_t* arena, usize size)
{
  const usize aligned = align_up(size);
  struct oak_arena_block_t* cur = arena->current;
  const int need_block =
      !cur || cur->used + aligned > cur->capacity;

  if (need_block)
  {
    usize cap = arena->block_size;
    if (aligned > cap)
      cap = aligned;
    struct oak_arena_block_t* block = arena_new_block(cap);
    if (!block)
      return null;
    block->next = arena->current;
    arena->current = block;
    cur = block;
  }

  void* ptr = cur->data + cur->used;
  cur->used += aligned;
  memset(ptr, 0, aligned);
  return ptr;
}

void oak_arena_free(struct oak_arena_t* arena)
{
  struct oak_arena_block_t* block = arena->current;
  while (block)
  {
    struct oak_arena_block_t* next = block->next;
    oak_free(block, OAK_SRC_LOC);
    block = next;
  }
  arena->current = null;
}
