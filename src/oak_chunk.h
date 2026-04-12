#pragma once

#include "oak_value.h"

#include <stdint.h>

enum oak_opcode_t
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
  OAK_OP_CALL,
  OAK_OP_RETURN,
};

enum oak_op_format_t
{
  OAK_OP_FMT_NONE,
  OAK_OP_FMT_CONSTANT,
  OAK_OP_FMT_SLOT,
  OAK_OP_FMT_JUMP_FWD,
  OAK_OP_FMT_JUMP_BACK,
  OAK_OP_FMT_ARGC,
};

struct oak_op_info_t
{
  const char* name;
  enum oak_op_format_t format;
  int stack_effect;
};

extern const struct oak_op_info_t oak_op_info[];

const struct oak_op_info_t* oak_op_get_info(uint8_t op);

/* Source coordinates (from lexer tokens at compile time; stored per bytecode
 * byte). Named oak_code_loc_t to avoid clashing with oak_mem.h's oak_src_loc_t.
 */
struct oak_code_loc_t
{
  int line;
  int column;
};

struct oak_debug_local_t
{
  int slot;
  size_t offset;
  char* name;
};

struct oak_chunk_t
{
  size_t count;
  size_t capacity;
  uint8_t* bytecode;
  struct oak_code_loc_t* locations;
  size_t const_count;
  size_t const_capacity;
  struct oak_value_t* constants;
  size_t debug_count;
  size_t debug_capacity;
  struct oak_debug_local_t* debug_locals;
};

void oak_chunk_init(struct oak_chunk_t* chunk);
void oak_chunk_free(struct oak_chunk_t* chunk);

void oak_chunk_write(struct oak_chunk_t* chunk,
                     uint8_t byte,
                     struct oak_code_loc_t loc);

size_t oak_chunk_add_constant(struct oak_chunk_t* chunk,
                              struct oak_value_t value);
void oak_chunk_add_debug_local(struct oak_chunk_t* chunk,
                               int slot,
                               const char* name,
                               size_t length);
void oak_chunk_disassemble(const struct oak_chunk_t* chunk);
