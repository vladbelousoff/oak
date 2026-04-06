#pragma once

#include "oak_value.h"

#include <stdint.h>

typedef enum
{
  OAK_OP_HALT,
  OAK_OP_CONSTANT,
  OAK_OP_TRUE,
  OAK_OP_FALSE,
  OAK_OP_POP,
  OAK_OP_GET_LOCAL,
  OAK_OP_SET_LOCAL,
  OAK_OP_ADD,
  OAK_OP_SUB,
  OAK_OP_MUL,
  OAK_OP_DIV,
  OAK_OP_MOD,
  OAK_OP_NEGATE,
  OAK_OP_NOT,
  OAK_OP_EQ,
  OAK_OP_NEQ,
  OAK_OP_LT,
  OAK_OP_LE,
  OAK_OP_GT,
  OAK_OP_GE,
  OAK_OP_JUMP,
  OAK_OP_JUMP_IF_FALSE,
  OAK_OP_LOOP,
  OAK_OP_PRINT,
} oak_opcode_t;

typedef struct
{
  size_t count;
  size_t capacity;
  uint8_t* bytecode;
  int* lines;
  size_t const_count;
  size_t const_capacity;
  oak_value_t* constants;
} oak_chunk_t;

void oak_chunk_init(oak_chunk_t* chunk);
void oak_chunk_free(oak_chunk_t* chunk);

void oak_chunk_write(oak_chunk_t* chunk, uint8_t byte, int line);
size_t oak_chunk_add_constant(oak_chunk_t* chunk, oak_value_t value);
