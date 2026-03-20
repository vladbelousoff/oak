#include "oak_arena.h"

#include <string.h>

#include "oak_mem.h"

#define OAK_ARENA_ALIGN 16

static size_t align_up(const size_t n, const size_t align)
{
  return (n + align - 1) & ~(align - 1);
}

static oak_arena_block_t* arena_new_block(const size_t capacity)
{
  oak_arena_block_t* block =
      oak_mem_acquire(OAK_SRC_LOC, sizeof(oak_arena_block_t) + capacity);
  if (!block)
    return NULL;
  block->next = NULL;
  block->capacity = capacity;
  block->used = 0;
  return block;
}

void oak_arena_init(oak_arena_t* arena, const size_t block_size)
{
  arena->block_size = block_size ? block_size : OAK_ARENA_DEFAULT_BLOCK_SIZE;
  arena->current = NULL;
}

void* oak_arena_alloc(oak_arena_t* arena, size_t size)
{
  size = align_up(size, OAK_ARENA_ALIGN);

  if (!arena->current || arena->current->used + size > arena->current->capacity)
  {
    size_t cap = arena->block_size;
    if (size > cap)
      cap = size;
    oak_arena_block_t* block = arena_new_block(cap);
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

void oak_arena_destroy(oak_arena_t* arena)
{
  oak_arena_block_t* block = arena->current;
  while (block)
  {
    oak_arena_block_t* next = block->next;
    oak_mem_release(OAK_SRC_LOC, block);
    block = next;
  }
  arena->current = NULL;
}
