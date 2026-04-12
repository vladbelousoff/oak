#include "oak_arena.h"

#include <string.h>

#include "oak_mem.h"

static size_t align_up(const size_t n)
{
  const size_t mask = sizeof(size_t) * 2 - 1;
  return n + mask & ~mask;
}

static struct oak_arena_block_t* arena_new_block(const size_t capacity)
{
  struct oak_arena_block_t* block =
      oak_alloc(sizeof(struct oak_arena_block_t) + capacity, OAK_SRC_LOC);
  if (!block)
    return NULL;
  block->next = NULL;
  block->capacity = capacity;
  block->used = 0;
  return block;
}

void oak_arena_init(struct oak_arena_t* arena, const size_t block_size)
{
  arena->block_size = block_size ? block_size : OAK_ARENA_DEFAULT_BLOCK_SIZE;
  arena->current = NULL;
}

void* oak_arena_alloc(struct oak_arena_t* arena, size_t size)
{
  size = align_up(size);

  if (!arena->current || arena->current->used + size > arena->current->capacity)
  {
    size_t cap = arena->block_size;
    if (size > cap)
      cap = size;
    struct oak_arena_block_t* block = arena_new_block(cap);
    if (!block)
      return NULL;
    block->next = arena->current;
    arena->current = block;
  }

  void* ptr = arena->current->data + arena->current->used;
  arena->current->used += size;
  memset(ptr, 0, size);
  return ptr;
}

void oak_arena_destroy(struct oak_arena_t* arena)
{
  struct oak_arena_block_t* block = arena->current;
  while (block)
  {
    struct oak_arena_block_t* next = block->next;
    oak_free(block, OAK_SRC_LOC);
    block = next;
  }
  arena->current = NULL;
}
