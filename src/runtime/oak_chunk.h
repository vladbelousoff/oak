#pragma once

#include "oak_container.h"
#include "oak_export.h"
#include "oak_value.h"

typedef enum oak_opcode oak_opcode_t;
enum oak_opcode
{
  OAK_OP_HALT,
  /* Push constants[idx] (16-bit big-endian index). */
  OAK_OP_CONSTANT,
  /* Push a signed 8-bit integer literal directly (no constant pool). */
  OAK_OP_PUSH_INT8,
  OAK_OP_TRUE,
  OAK_OP_FALSE,
  OAK_OP_NONE,
  /* Coerce top-of-stack to a boolean: pop value, push bool(truthy(value)). */
  OAK_OP_BOOL,
  OAK_OP_POP,
  /* Pops `arg` values from the stack (decref'ing each). The stack-effect entry
   * in oak_op_info[] is set to 0 because the actual effect varies; callers
   * (oak_compiler_emit_pops) adjust scope.stack_depth manually. */
  OAK_OP_POP_N,
  OAK_OP_GET_LOCAL,
  OAK_OP_SET_LOCAL,
  /* Converts the top stack value to a non-owning weak reference. */
  OAK_OP_WEAKEN,
  OAK_OP_INC_LOCAL,
  OAK_OP_DEC_LOCAL,
  /* Binary arithmetic / comparison / equality — dedicated opcodes. */
  OAK_OP_ADD,
  OAK_OP_SUBTRACT,
  OAK_OP_MULTIPLY,
  OAK_OP_DIVIDE,
  OAK_OP_INT_DIVIDE,
  OAK_OP_MODULO,
  OAK_OP_EQUAL,
  OAK_OP_NOT_EQUAL,
  OAK_OP_LESS,
  OAK_OP_LESS_EQUAL,
  OAK_OP_GREATER,
  OAK_OP_GREATER_EQUAL,
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
  /* Wraps the top-of-stack concrete value in an interface-object fat pointer.
   * Operand: 16-bit (big-endian) vtable constant index (an OAK_OBJ_ARRAY of
   * function values in interface-method declaration order).
   * Stack: [..., value] -> [..., interface_object]. */
  OAK_OP_MAKE_INTERFACE_OBJECT,
  /* Virtual dispatch through an interface object.
   * Operands: vtable_slot (u8), total_arity (u8, including self).
   * Stack: [..., interface_obj, arg1..argN] -> [..., return_value]. */
  OAK_OP_CALL_VIRTUAL,

  /* Fused comparison + conditional branch (avoids intermediate bool push/pop).
   * Operands: 16-bit big-endian forward jump offset.
   * Stack: [..., a, b] -> [...]; jumps if comparison is false. */
  OAK_OP_LESS_JUMP_IF_FALSE,
  OAK_OP_LESS_EQUAL_JUMP_IF_FALSE,
  OAK_OP_GREATER_JUMP_IF_FALSE,
  OAK_OP_GREATER_EQUAL_JUMP_IF_FALSE,

  /* Superinstructions: fused sequences that avoid redundant dispatch. */
  /* GET_LOCAL slot1, GET_LOCAL slot2 — pushes two locals. */
  OAK_OP_GET_LOCAL_GET_LOCAL,
  /* INC_LOCAL slot, LOOP offset — increment then backward jump. */
  OAK_OP_INC_LOCAL_LOOP,
};

/* Binary operation selector (used internally by the compiler). */
typedef enum oak_binop oak_binop_t;
enum oak_binop
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

OAK_API const char* oak_binop_name(u8 binop);

typedef enum oak_op_format oak_op_format_t;
enum oak_op_format
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
  /* Two 8-bit values: slot + argc; used by OAK_OP_CALL_VIRTUAL. */
  OAK_OP_FMT_U8_U8,
};

#define OAK_CHUNK_MAX_RECORD_FIELDS 32

/* Deduplicated per-chunk: declaration-order field names for a user record
 * type, referenced when constructing record instances. */
typedef struct oak_chunk_field_layout oak_chunk_field_layout_t;
struct oak_chunk_field_layout
{
  int field_count;
  const char* name[OAK_CHUNK_MAX_RECORD_FIELDS];
  char* name_blob;
};

typedef struct oak_op_info oak_op_info_t;
struct oak_op_info
{
  const char* name;
  oak_op_format_t format;
  int stack_effect;
};

OAK_API extern const oak_op_info_t oak_op_info[];

OAK_API const oak_op_info_t* oak_op_get_info(u8 op);

/* Source coordinates (from lexer tokens at compile time; stored per bytecode
 * byte). */
typedef struct oak_code_loc oak_code_loc_t;
struct oak_code_loc
{
  int line;
  int column;
};

typedef struct oak_debug_local oak_debug_local_t;
struct oak_debug_local
{
  int slot;
  usize offset;
  usize end_offset;
  char* name;
};

/* Debug-only state of a chunk. Optional: when null, the VM still executes
 * normally but produces error messages without source line/column and the
 * disassembler prints a sparse listing. */
typedef struct oak_chunk_debug oak_chunk_debug_t;
struct oak_chunk_debug
{
  /* Optional: path/label for the Oak source (borrowed). Not owned by chunk. */
  const char* source_name;
  /* oak_code_loc_t — one entry per byte in chunk->code. Kept in step
   * with the code vector by oak_chunk_write, which appends to both. */
  oak_container_t* locations;
  /* oak_debug_local_t */
  oak_container_t* debug_locals;
};

typedef struct oak_allocator oak_allocator_t;

typedef struct oak_chunk oak_chunk_t;
struct oak_chunk
{
  oak_allocator_t* allocator;
  /* u8 — the bytecode. */
  oak_container_t* code;
  /* oak_value_t */
  oak_container_t* constants;
  /* oak_chunk_field_layout_t */
  oak_container_t* field_layouts;
  /* Optional debug section; null when stripped. */
  oak_chunk_debug_t* debug;
  /* The module that owns this chunk.  OAK_MODULE_ID_NONE (0xFFFF) when the
   * chunk is part of a single-file (no-import) compile. */
  u16 module_id;
};

/* Sugar for the two things every reader of a chunk needs. The code vector is
 * only appended to during compilation, so a pointer hoisted before execution
 * stays valid for the life of the run. */
static inline usize oak_chunk_size(const oak_chunk_t* chunk)
{
  return oak_size(chunk->code);
}

static inline const u8* oak_chunk_code(const oak_chunk_t* chunk)
{
  return OAK_CDATA(u8, chunk->code);
}

/* Source location of the byte at `offset`, or {0,0} when the chunk carries no
 * debug section or the offset is past the end. */
static inline oak_code_loc_t oak_chunk_loc(const oak_chunk_t* chunk,
                                           const usize offset)
{
  /* Written without compound literals and with explicit casts off the void*
   * returns: this header is also compiled as C++ via oak.hpp. */
  oak_code_loc_t none;
  none.line = 0;
  none.column = 0;
  if (!chunk->debug)
    return none;
  const oak_code_loc_t* const loc =
      (const oak_code_loc_t*)oak_cget(chunk->debug->locations, offset);
  return loc ? *loc : none;
}

/* Bounds-checked single-constant read. Callers that touch the pool in a loop
 * while it can still grow must use this rather than hoisting a data pointer:
 * compiling a function body appends constants and may reallocate. */
static inline oak_value_t oak_chunk_constant(const oak_chunk_t* chunk,
                                             const usize index)
{
  const oak_value_t* const value =
      (const oak_value_t*)oak_cget(chunk->constants, index);
  return value ? *value : oak_value_none();
}

OAK_API void oak_chunk_init(oak_chunk_t* chunk,
                            oak_allocator_t* allocator);
OAK_API void oak_chunk_free(oak_chunk_t* chunk);

/* Allocate and attach an empty debug section to the chunk. Idempotent. */
OAK_API void oak_chunk_enable_debug(oak_chunk_t* chunk,
                                    const char* source_name);

/* Intern a field-name layout. Returns a stable id >= 0, or -1 on failure. */
OAK_API int oak_chunk_add_field_layout(oak_chunk_t* chunk,
                                       int field_count,
                                       const char* const* names);

OAK_API void oak_chunk_write(oak_chunk_t* chunk,
                             u8 byte,
                             oak_code_loc_t loc);

OAK_API usize oak_chunk_add_constant(oak_chunk_t* chunk,
                                     oak_value_t value);
OAK_API void oak_chunk_add_debug_local(oak_chunk_t* chunk,
                                       int slot,
                                       const char* name);
OAK_API void oak_chunk_end_debug_local(oak_chunk_t* chunk, int slot);
OAK_API void oak_chunk_disassemble(const oak_chunk_t* chunk);
OAK_API usize oak_chunk_disassemble_instruction(const oak_chunk_t* chunk,
                                                usize offset);
