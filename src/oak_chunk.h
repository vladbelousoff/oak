#pragma once

#include "oak_value.h"

enum oak_opcode_t
{
  OAK_OP_HALT,
  OAK_OP_CONSTANT,
  OAK_OP_TRUE,
  OAK_OP_FALSE,
  OAK_OP_POP,
  OAK_OP_GET_LOCAL,
  OAK_OP_SET_LOCAL,
  OAK_OP_INC_LOCAL,
  OAK_OP_DEC_LOCAL,
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
  OAK_OP_CALL,
  OAK_OP_RETURN,
  OAK_OP_NEW_ARRAY,
  OAK_OP_NEW_ARRAY_FROM_STACK,
  OAK_OP_NEW_MAP,
  OAK_OP_GET_INDEX,
  OAK_OP_SET_INDEX,
  OAK_OP_MAP_KEY_AT,
  OAK_OP_MAP_VALUE_AT,
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

const struct oak_op_info_t* oak_op_get_info(u8 op);

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
  usize offset;
  char* name;
};

struct oak_chunk_t
{
  usize count;
  usize capacity;
  u8* bytecode;
  struct oak_code_loc_t* locations;
  usize const_count;
  usize const_capacity;
  struct oak_value_t* constants;
  usize debug_count;
  usize debug_capacity;
  struct oak_debug_local_t* debug_locals;
};

void oak_chunk_init(struct oak_chunk_t* chunk);
void oak_chunk_free(struct oak_chunk_t* chunk);

void oak_chunk_write(struct oak_chunk_t* chunk,
                     u8 byte,
                     struct oak_code_loc_t loc);

usize oak_chunk_add_constant(struct oak_chunk_t* chunk,
                              struct oak_value_t value);
void oak_chunk_add_debug_local(struct oak_chunk_t* chunk,
                               int slot,
                               const char* name,
                               usize length);
void oak_chunk_disassemble(const struct oak_chunk_t* chunk);
