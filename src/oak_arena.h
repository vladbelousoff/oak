#pragma once

#include <stddef.h>

#define OAK_ARENA_DEFAULT_BLOCK_SIZE 4096

struct oak_arena_block_t
{
  struct oak_arena_block_t* next;
  size_t capacity;
  size_t used;
  char data[];
};

struct oak_arena_t
{
  struct oak_arena_block_t* current;
  size_t block_size;
};

void oak_arena_init(struct oak_arena_t* arena, size_t block_size);
void* oak_arena_alloc(struct oak_arena_t* arena, size_t size);
void oak_arena_destroy(struct oak_arena_t* arena);
