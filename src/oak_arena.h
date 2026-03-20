#pragma once

#include <stddef.h>

#define OAK_ARENA_DEFAULT_BLOCK_SIZE 4096

typedef struct oak_arena_block_t
{
  struct oak_arena_block_t* next;
  size_t capacity;
  size_t used;
  char data[];
} oak_arena_block_t;

typedef struct
{
  oak_arena_block_t* current;
  size_t block_size;
} oak_arena_t;

void oak_arena_init(oak_arena_t* arena, size_t block_size);
void* oak_arena_alloc(oak_arena_t* arena, size_t size);
void oak_arena_destroy(oak_arena_t* arena);
