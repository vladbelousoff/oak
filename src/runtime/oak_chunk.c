#include "oak_chunk.h"

#include "oak_count_of.h"
#include "oak_mem.h"

#include <stdio.h>
#include <string.h>

#define CHUNK_INITIAL_CAPACITY 256
#define CONST_INITIAL_CAPACITY 16
#define DEBUG_INITIAL_CAPACITY 8

const struct oak_op_info_t oak_op_info[] = {
  [OAK_OP_HALT] = { "OP_HALT", OAK_OP_FMT_NONE, 0 },
  [OAK_OP_CONSTANT] = { "OP_CONSTANT", OAK_OP_FMT_CONSTANT, 1 },
  [OAK_OP_CONSTANT_LONG] = { "OP_CONSTANT_LONG", OAK_OP_FMT_CONSTANT_LONG, 1 },
  [OAK_OP_TRUE] = { "OP_TRUE", OAK_OP_FMT_NONE, 1 },
  [OAK_OP_FALSE] = { "OP_FALSE", OAK_OP_FMT_NONE, 1 },
  [OAK_OP_POP] = { "OP_POP", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_GET_LOCAL] = { "OP_GET_LOCAL", OAK_OP_FMT_SLOT, 1 },
  [OAK_OP_SET_LOCAL] = { "OP_SET_LOCAL", OAK_OP_FMT_SLOT, 0 },
  [OAK_OP_INC_LOCAL] = { "OP_INC_LOCAL", OAK_OP_FMT_SLOT, 0 },
  [OAK_OP_DEC_LOCAL] = { "OP_DEC_LOCAL", OAK_OP_FMT_SLOT, 0 },
  [OAK_OP_ADD] = { "OP_ADD", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_SUB] = { "OP_SUB", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_MUL] = { "OP_MUL", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_DIV] = { "OP_DIV", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_MOD] = { "OP_MOD", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_NEGATE] = { "OP_NEGATE", OAK_OP_FMT_NONE, 0 },
  [OAK_OP_NOT] = { "OP_NOT", OAK_OP_FMT_NONE, 0 },
  [OAK_OP_EQ] = { "OP_EQ", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_NEQ] = { "OP_NEQ", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_LT] = { "OP_LT", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_LE] = { "OP_LE", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_GT] = { "OP_GT", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_GE] = { "OP_GE", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_JUMP] = { "OP_JUMP", OAK_OP_FMT_JUMP_FWD, 0 },
  [OAK_OP_JUMP_IF_FALSE] = { "OP_JUMP_IF_FALSE", OAK_OP_FMT_JUMP_FWD, -1 },
  [OAK_OP_LOOP] = { "OP_LOOP", OAK_OP_FMT_JUMP_BACK, 0 },
  [OAK_OP_CALL] = { "OP_CALL", OAK_OP_FMT_ARGC, 0 },
  [OAK_OP_RETURN] = { "OP_RETURN", OAK_OP_FMT_NONE, 0 },
  [OAK_OP_NEW_ARRAY] = { "OP_NEW_ARRAY", OAK_OP_FMT_NONE, 1 },
  [OAK_OP_NEW_ARRAY_FROM_STACK] = { "OP_NEW_ARRAY_FROM_STACK",
                                    OAK_OP_FMT_ARGC,
                                    1 },
  [OAK_OP_NEW_MAP] = { "OP_NEW_MAP", OAK_OP_FMT_NONE, 1 },
  [OAK_OP_NEW_MAP_FROM_STACK] = { "OP_NEW_MAP_FROM_STACK", OAK_OP_FMT_ARGC, 1 },
  [OAK_OP_GET_INDEX] = { "OP_GET_INDEX", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_SET_INDEX] = { "OP_SET_INDEX", OAK_OP_FMT_NONE, -2 },
  [OAK_OP_MAP_KEY_AT] = { "OP_MAP_KEY_AT", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_MAP_VALUE_AT] = { "OP_MAP_VALUE_AT", OAK_OP_FMT_NONE, -1 },
  /* Pops <count> field values plus one type-name string from the stack and
   * pushes a fresh record. Stack effect counts only the name (consumed) and
   * the produced record; <count> is variable and adjusted at compile time. */
  [OAK_OP_NEW_RECORD_FROM_STACK] = { "OP_NEW_RECORD_FROM_STACK",
                                     OAK_OP_FMT_ARGC,
                                     0 },
  [OAK_OP_GET_FIELD] = { "OP_GET_FIELD", OAK_OP_FMT_SLOT, 0 },
  /* Stack: [..., record, value] -> [..., value]; sets record.fields[idx]. */
  [OAK_OP_SET_FIELD] = { "OP_SET_FIELD", OAK_OP_FMT_SLOT, -1 },
};

#define OAK_OP_INFO_COUNT oak_count_of(oak_op_info)

const struct oak_op_info_t* oak_op_get_info(const u8 op)
{
  if (op < OAK_OP_INFO_COUNT && oak_op_info[op].name)
    return &oak_op_info[op];
  return null;
}

void oak_chunk_init(struct oak_chunk_t* chunk)
{
  chunk->source_name = null;
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->bytecode = null;
  chunk->locations = null;
  chunk->const_count = 0;
  chunk->const_capacity = 0;
  chunk->constants = null;
  chunk->debug_count = 0;
  chunk->debug_capacity = 0;
  chunk->debug_locals = null;
}

static void ensure_code_capacity(struct oak_chunk_t* chunk)
{
  if (chunk->count < chunk->capacity)
    return;

  const usize new_cap =
      chunk->capacity == 0 ? CHUNK_INITIAL_CAPACITY : chunk->capacity * 2;
  chunk->bytecode =
      oak_realloc(chunk->bytecode, new_cap * sizeof(u8), OAK_SRC_LOC);
  chunk->locations = oak_realloc(
      chunk->locations, new_cap * sizeof(struct oak_code_loc_t), OAK_SRC_LOC);
  chunk->capacity = new_cap;
}

void oak_chunk_write(struct oak_chunk_t* chunk,
                     const u8 byte,
                     const struct oak_code_loc_t loc)
{
  ensure_code_capacity(chunk);
  chunk->bytecode[chunk->count] = byte;
  chunk->locations[chunk->count] = loc;
  chunk->count++;
}

usize oak_chunk_add_constant(struct oak_chunk_t* chunk,
                             const struct oak_value_t value)
{
  if (chunk->const_count >= chunk->const_capacity)
  {
    const usize new_cap = chunk->const_capacity == 0
                              ? CONST_INITIAL_CAPACITY
                              : chunk->const_capacity * 2;
    chunk->constants = oak_realloc(
        chunk->constants, new_cap * sizeof(struct oak_value_t), OAK_SRC_LOC);
    chunk->const_capacity = new_cap;
  }

  chunk->constants[chunk->const_count] = value;
  return chunk->const_count++;
}

void oak_chunk_add_debug_local(struct oak_chunk_t* chunk,
                               const int slot,
                               const char* name,
                               const usize length)
{
  if (length == 0)
    return;

  if (chunk->debug_count >= chunk->debug_capacity)
  {
    const usize new_cap = chunk->debug_capacity == 0
                              ? DEBUG_INITIAL_CAPACITY
                              : chunk->debug_capacity * 2;
    chunk->debug_locals =
        oak_realloc(chunk->debug_locals,
                    new_cap * sizeof(struct oak_debug_local_t),
                    OAK_SRC_LOC);
    chunk->debug_capacity = new_cap;
  }

  char* buf = oak_alloc(length + 1, OAK_SRC_LOC);
  memcpy(buf, name, length);
  buf[length] = 0;

  struct oak_debug_local_t* d = &chunk->debug_locals[chunk->debug_count++];
  d->slot = slot;
  d->offset = chunk->count;
  d->name = buf;
}

void oak_chunk_free(struct oak_chunk_t* chunk)
{
  if (chunk->constants && chunk->const_count > 0)
  {
    for (usize i = 0; i < chunk->const_count; ++i)
      oak_value_decref(chunk->constants[i]);
  }

  if (chunk->debug_locals)
  {
    for (usize i = 0; i < chunk->debug_count; ++i)
      oak_free(chunk->debug_locals[i].name, OAK_SRC_LOC);
    oak_free(chunk->debug_locals, OAK_SRC_LOC);
  }

  if (chunk->bytecode)
    oak_free(chunk->bytecode, OAK_SRC_LOC);
  if (chunk->locations)
    oak_free(chunk->locations, OAK_SRC_LOC);
  if (chunk->constants)
    oak_free(chunk->constants, OAK_SRC_LOC);

  oak_free(chunk, OAK_SRC_LOC);
}

static const char* opcode_name(const u8 op)
{
  const struct oak_op_info_t* info = oak_op_get_info(op);
  return info ? info->name : "OP_UNKNOWN";
}

static int
snprint_value(char* buf, const usize size, const struct oak_value_t value)
{
  if (oak_is_bool(value))
    return snprintf(buf, size, "%s", oak_as_bool(value) ? "true" : "false");
  if (oak_is_number(value))
  {
    if (oak_is_f32(value))
      return snprintf(buf, size, "%g", (double)oak_as_f32(value));
    return snprintf(buf, size, "%d", oak_as_i32(value));
  }
  if (oak_is_obj(value))
  {
    if (oak_is_string(value))
      return snprintf(buf, size, "'%s'", oak_as_cstring(value));
    if (oak_is_fn(value))
      return snprintf(buf, size, "<fn @%zu>", oak_as_fn(value)->code_offset);
    if (oak_is_native_fn(value))
      return oak_native_fn_format(buf, size, oak_as_native_fn(value));
    if (oak_is_array(value))
      return snprintf(
          buf, size, "<array len=%zu>", oak_as_array(value)->length);
    if (oak_is_map(value))
      return snprintf(buf, size, "<map len=%zu>", oak_as_map(value)->length);
    if (oak_is_record(value))
    {
      const struct oak_obj_record_t* s = oak_as_record(value);
      return snprintf(buf,
                      size,
                      "<%s fields=%d>",
                      s->type_name ? s->type_name : "record",
                      s->field_count);
    }
    return snprintf(buf, size, "%p", (void*)oak_as_obj(value));
  }
  buf[0] = '\0';
  return 0;
}

static const char* debug_local_name(const struct oak_chunk_t* chunk,
                                    const int slot,
                                    const usize offset)
{
  for (usize i = chunk->debug_count; i > 0; --i)
  {
    const struct oak_debug_local_t* d = &chunk->debug_locals[i - 1];
    if (d->slot == slot && d->offset <= offset)
      return d->name;
  }
  return null;
}

static usize disassemble_instruction(const struct oak_chunk_t* chunk,
                                     const usize offset)
{
  char line[16];
  if (offset > 0 &&
      chunk->locations[offset].line == chunk->locations[offset - 1].line)
    snprintf(line, sizeof(line), "   |");
  else
    snprintf(line, sizeof(line), "%4d", chunk->locations[offset].line);

  const u8 op = chunk->bytecode[offset];
  const char* name = opcode_name(op);
  const struct oak_op_info_t* info = oak_op_get_info(op);
  const enum oak_op_format_t fmt = info ? info->format : OAK_OP_FMT_NONE;

  switch (fmt)
  {
    case OAK_OP_FMT_CONSTANT:
    {
      const u8 idx = chunk->bytecode[offset + 1];
      char val[64];
      snprint_value(val, sizeof(val), chunk->constants[idx]);
      oak_log(OAK_LOG_INFO,
              "%04zu %s  %-20s %4d ; %s",
              offset,
              line,
              name,
              idx,
              val);
      return offset + 2;
    }
    case OAK_OP_FMT_CONSTANT_LONG:
    {
      const u16 idx = (u16)((u16)chunk->bytecode[offset + 1] << 8) |
                      chunk->bytecode[offset + 2];
      char val[64];
      snprint_value(val, sizeof(val), chunk->constants[idx]);
      oak_log(OAK_LOG_INFO,
              "%04zu %s  %-20s %4u ; %s",
              offset,
              line,
              name,
              (unsigned)idx,
              val);
      return offset + 3;
    }
    case OAK_OP_FMT_SLOT:
    {
      const u8 slot = chunk->bytecode[offset + 1];
      const char* local = debug_local_name(chunk, slot, offset);
      if (local)
        oak_log(OAK_LOG_INFO,
                "%04zu %s  %-20s %4d ; %s",
                offset,
                line,
                name,
                slot,
                local);
      else
        oak_log(OAK_LOG_INFO, "%04zu %s  %-20s %4d", offset, line, name, slot);
      return offset + 2;
    }
    case OAK_OP_FMT_JUMP_FWD:
    {
      const u32 jump = ((u32)chunk->bytecode[offset + 1] << 24) |
                       ((u32)chunk->bytecode[offset + 2] << 16) |
                       ((u32)chunk->bytecode[offset + 3] << 8) |
                       (u32)chunk->bytecode[offset + 4];
      oak_log(OAK_LOG_INFO,
              "%04zu %s  %-20s %6u -> %04zu",
              offset,
              line,
              name,
              jump,
              offset + 5 + (usize)jump);
      return offset + 5;
    }
    case OAK_OP_FMT_JUMP_BACK:
    {
      const u32 jump = ((u32)chunk->bytecode[offset + 1] << 24) |
                       ((u32)chunk->bytecode[offset + 2] << 16) |
                       ((u32)chunk->bytecode[offset + 3] << 8) |
                       (u32)chunk->bytecode[offset + 4];
      oak_log(OAK_LOG_INFO,
              "%04zu %s  %-20s %6u -> %04zu",
              offset,
              line,
              name,
              jump,
              offset + 5 - (usize)jump);
      return offset + 5;
    }
    case OAK_OP_FMT_ARGC:
    {
      const u8 argc = chunk->bytecode[offset + 1];
      oak_log(OAK_LOG_INFO, "%04zu %s  %-20s %4d", offset, line, name, argc);
      return offset + 2;
    }
    default:
      oak_log(OAK_LOG_INFO, "%04zu %s  %s", offset, line, name);
      return offset + 1;
  }
}

void oak_chunk_disassemble(const struct oak_chunk_t* chunk)
{
  oak_log(OAK_LOG_INFO,
          "---- chunk [%zu bytes, %zu constants] ----",
          chunk->count,
          chunk->const_count);
  usize offset = 0;
  while (offset < chunk->count)
    offset = disassemble_instruction(chunk, offset);
}
