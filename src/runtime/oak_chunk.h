#pragma once

#include "oak_value.h"

enum oak_opcode_t
{
  OAK_OP_HALT,
  /* Push constants[idx] (16-bit big-endian index). */
  OAK_OP_CONSTANT,
  /* Push a signed 8-bit integer literal directly (no constant pool). */
  OAK_OP_PUSH_INT8,
  OAK_OP_TRUE,
  OAK_OP_FALSE,
  /* Coerce top-of-stack to a boolean: pop value, push bool(truthy(value)). */
  OAK_OP_BOOL,
  OAK_OP_POP,
  /* Pops `arg` values from the stack (decref'ing each). The stack-effect entry
   * in oak_op_info[] is set to 0 because the actual effect varies; callers
   * (oak_compiler_emit_pops) adjust scope.stack_depth manually. */
  OAK_OP_POP_N,
  OAK_OP_GET_LOCAL,
  OAK_OP_SET_LOCAL,
  OAK_OP_INC_LOCAL,
  OAK_OP_DEC_LOCAL,
  /* Binary arithmetic / comparison / equality. The operation is encoded in
   * an 8-bit operand byte (oak_binop_t). */
  OAK_OP_BINARY,
  OAK_OP_NEGATE,
  OAK_OP_NOT,
  OAK_OP_JUMP,
  OAK_OP_JUMP_IF_FALSE,
  OAK_OP_JUMP_IF_TRUE,
  OAK_OP_LOOP,
  OAK_OP_CALL,
  OAK_OP_RETURN,
  OAK_OP_NEW_ARR,
  OAK_OP_NEW_MAP,
  OAK_OP_GET_INDEX,
  OAK_OP_SET_INDEX,
  OAK_OP_MAP_KEY_AT,
  OAK_OP_MAP_VAL_AT,
  OAK_OP_NEW_RECORD,
  OAK_OP_GET_FIELD,
  OAK_OP_SET_FIELD,
  /* Cross-module: pushes the function value at constants[const_idx] of the
   * module identified by module_id. */
  OAK_OP_GET_MODULE_FN,
};

/* Operand byte for OAK_OP_BINARY. */
enum oak_binop_t
{
  OAK_BINOP_ADD,
  OAK_BINOP_SUBTRACT,
  OAK_BINOP_MULTIPLY,
  OAK_BINOP_DIVIDE,
  OAK_BINOP_INT_DIVIDE,
  OAK_BINOP_MODULO,
  OAK_BINOP_EQUAL,
  OAK_BINOP_NOT_EQUAL,
  OAK_BINOP_LESS,
  OAK_BINOP_LESS_EQUAL,
  OAK_BINOP_GREATER,
  OAK_BINOP_GREATER_EQUAL,
};

const char* oak_binop_name(u8 binop);

enum oak_op_format_t
{
  OAK_OP_FMT_NONE,
  OAK_OP_FMT_CONSTANT, /* 16-bit (big-endian) constant index */
  OAK_OP_FMT_INT8,     /* signed 8-bit immediate integer */
  OAK_OP_FMT_SLOT,
  OAK_OP_FMT_JUMP_FWD,
  OAK_OP_FMT_JUMP_BACK,
  OAK_OP_FMT_ARGC,
  OAK_OP_FMT_BINOP, /* 8-bit binary operation selector (oak_binop_t) */
  /* 8-bit count + 16-bit (big-endian) id; e.g. user record + field layout. */
  OAK_OP_FMT_U8_U16,
  /* 16-bit (big-endian) module_id + 16-bit (big-endian) const_idx; used by
   * cross-module references such as OP_GET_MODULE_FN. */
  OAK_OP_FMT_U16_U16,
};

#define OAK_CHUNK_MAX_RECORD_FIELDS 32

/* Deduplicated per-chunk: declaration-order field names for a user record
 * type, referenced when constructing record instances. */
struct oak_chunk_field_layout
{
  int field_count;
  const char* name[OAK_CHUNK_MAX_RECORD_FIELDS];
  char* name_blob;
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

/* Debug-only state of a chunk. Optional: when null, the VM still executes
 * normally but produces error messages without source line/column and the
 * disassembler prints a sparse listing. */
struct oak_chunk_debug_t
{
  /* Optional: path/label for the Oak source (borrowed). Not owned by chunk. */
  const char* source_name;
  /* One entry per byte in chunk->bytecode. */
  struct oak_code_loc_t* locations;
  usize debug_count;
  usize debug_capacity;
  struct oak_debug_local_t* debug_locals;
};

struct oak_chunk_t
{
  usize count;
  usize capacity;
  u8* bytecode;
  usize const_count;
  usize const_capacity;
  struct oak_value_t* constants;
  struct oak_chunk_field_layout* field_layouts;
  int field_layout_count;
  int field_layout_capacity;
  /* Optional debug section; null when stripped. */
  struct oak_chunk_debug_t* debug;
  /* The module that owns this chunk.  OAK_MODULE_ID_NONE (0xFFFF) when the
   * chunk is part of a single-file (no-import) compile. */
  u16 module_id;
};

void oak_chunk_init(struct oak_chunk_t* chunk);
void oak_chunk_free(struct oak_chunk_t* chunk);

/* Allocate and attach an empty debug section to the chunk. Idempotent. */
void oak_chunk_enable_debug(struct oak_chunk_t* chunk, const char* source_name);

/* Intern a field-name layout. Returns a stable id >= 0, or -1 on failure.
 * `names[i]` is `name_len[i]` bytes if `name_len` is non-NULL, else
 * `names[i]` is a C string. */
int oak_chunk_add_field_layout(struct oak_chunk_t* chunk,
                               int field_count,
                               const char* const* names,
                               const usize* name_len);

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
