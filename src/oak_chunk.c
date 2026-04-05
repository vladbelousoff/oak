#include "oak_chunk.h"

#include "oak_mem.h"

#define CHUNK_INITIAL_CAPACITY 256
#define CONST_INITIAL_CAPACITY 16

void oak_chunk_init(oak_chunk_t* chunk)
{
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->bytecode = NULL;
  chunk->lines = NULL;
  chunk->const_count = 0;
  chunk->const_capacity = 0;
  chunk->constants = NULL;
}

static void ensure_code_capacity(oak_chunk_t* chunk)
{
  if (chunk->count < chunk->capacity)
    return;

  const size_t new_cap =
      chunk->capacity == 0 ? CHUNK_INITIAL_CAPACITY : chunk->capacity * 2;
  chunk->bytecode =
      oak_realloc(chunk->bytecode, new_cap * sizeof(uint8_t), OAK_SRC_LOC);
  chunk->lines = oak_realloc(chunk->lines, new_cap * sizeof(int), OAK_SRC_LOC);
  chunk->capacity = new_cap;
}

void oak_chunk_write(oak_chunk_t* chunk, const uint8_t byte, const int line)
{
  ensure_code_capacity(chunk);
  chunk->bytecode[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}

size_t oak_chunk_add_constant(oak_chunk_t* chunk, const oak_value_t value)
{
  if (chunk->const_count >= chunk->const_capacity)
  {
    const size_t new_cap = chunk->const_capacity == 0
                               ? CONST_INITIAL_CAPACITY
                               : chunk->const_capacity * 2;
    chunk->constants = oak_realloc(
        chunk->constants, new_cap * sizeof(oak_value_t), OAK_SRC_LOC);
    chunk->const_capacity = new_cap;
  }

  chunk->constants[chunk->const_count] = value;
  return chunk->const_count++;
}

void oak_chunk_free(oak_chunk_t* chunk)
{
  if (chunk->constants && chunk->const_count > 0)
  {
    for (size_t i = 0; i < chunk->const_count; ++i)
      oak_value_decref(chunk->constants[i]);
  }

  if (chunk->bytecode)
    oak_free(chunk->bytecode, OAK_SRC_LOC);
  if (chunk->lines)
    oak_free(chunk->lines, OAK_SRC_LOC);
  if (chunk->constants)
    oak_free(chunk->constants, OAK_SRC_LOC);

  oak_free(chunk, OAK_SRC_LOC);
}
