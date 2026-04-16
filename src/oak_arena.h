#pragma once

#include "oak_types.h"

#define OAK_ARENA_DEFAULT_BLOCK_SIZE 4096

struct oak_arena_block_t
{
  struct oak_arena_block_t* next;
  usize capacity;
  usize used;
  char data[];
};

struct oak_arena_t
{
  struct oak_arena_block_t* current;
  usize block_size;
};

void oak_arena_init(struct oak_arena_t* arena, usize block_size);
void* oak_arena_alloc(struct oak_arena_t* arena, usize size);
void oak_arena_destroy(struct oak_arena_t* arena);
