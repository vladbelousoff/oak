#include "oak_chunk.h"

#include "oak_mem.h"

#include <stdio.h>
#include <string.h>

#define CHUNK_INITIAL_CAPACITY 256
#define CONST_INITIAL_CAPACITY 16
#define DEBUG_INITIAL_CAPACITY 8

void oak_chunk_init(oak_chunk_t* chunk)
{
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->bytecode = NULL;
  chunk->lines = NULL;
  chunk->const_count = 0;
  chunk->const_capacity = 0;
  chunk->constants = NULL;
  chunk->debug_count = 0;
  chunk->debug_capacity = 0;
  chunk->debug_locals = NULL;
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

void oak_chunk_add_debug_local(oak_chunk_t* chunk,
                               const int slot,
                               const char* name,
                               const size_t length)
{
  if (length == 0)
    return;

  if (chunk->debug_count >= chunk->debug_capacity)
  {
    const size_t new_cap = chunk->debug_capacity == 0
                               ? DEBUG_INITIAL_CAPACITY
                               : chunk->debug_capacity * 2;
    chunk->debug_locals = oak_realloc(
        chunk->debug_locals, new_cap * sizeof(oak_debug_local_t), OAK_SRC_LOC);
    chunk->debug_capacity = new_cap;
  }

  char* buf = oak_alloc(length + 1, OAK_SRC_LOC);
  memcpy(buf, name, length);
  buf[length] = 0;

  oak_debug_local_t* d = &chunk->debug_locals[chunk->debug_count++];
  d->slot = slot;
  d->offset = chunk->count;
  d->name = buf;
}

void oak_chunk_free(oak_chunk_t* chunk)
{
  if (chunk->constants && chunk->const_count > 0)
  {
    for (size_t i = 0; i < chunk->const_count; ++i)
      oak_value_decref(chunk->constants[i]);
  }

  if (chunk->debug_locals)
  {
    for (size_t i = 0; i < chunk->debug_count; ++i)
      oak_free(chunk->debug_locals[i].name, OAK_SRC_LOC);
    oak_free(chunk->debug_locals, OAK_SRC_LOC);
  }

  if (chunk->bytecode)
    oak_free(chunk->bytecode, OAK_SRC_LOC);
  if (chunk->lines)
    oak_free(chunk->lines, OAK_SRC_LOC);
  if (chunk->constants)
    oak_free(chunk->constants, OAK_SRC_LOC);

  oak_free(chunk, OAK_SRC_LOC);
}

static const char* opcode_name(const uint8_t op)
{
  switch (op)
  {
    case OAK_OP_HALT:
      return "OP_HALT";
    case OAK_OP_CONSTANT:
      return "OP_CONSTANT";
    case OAK_OP_TRUE:
      return "OP_TRUE";
    case OAK_OP_FALSE:
      return "OP_FALSE";
    case OAK_OP_POP:
      return "OP_POP";
    case OAK_OP_GET_LOCAL:
      return "OP_GET_LOCAL";
    case OAK_OP_SET_LOCAL:
      return "OP_SET_LOCAL";
    case OAK_OP_ADD:
      return "OP_ADD";
    case OAK_OP_SUB:
      return "OP_SUB";
    case OAK_OP_MUL:
      return "OP_MUL";
    case OAK_OP_DIV:
      return "OP_DIV";
    case OAK_OP_MOD:
      return "OP_MOD";
    case OAK_OP_NEGATE:
      return "OP_NEGATE";
    case OAK_OP_NOT:
      return "OP_NOT";
    case OAK_OP_EQ:
      return "OP_EQ";
    case OAK_OP_NEQ:
      return "OP_NEQ";
    case OAK_OP_LT:
      return "OP_LT";
    case OAK_OP_LE:
      return "OP_LE";
    case OAK_OP_GT:
      return "OP_GT";
    case OAK_OP_GE:
      return "OP_GE";
    case OAK_OP_JUMP:
      return "OP_JUMP";
    case OAK_OP_JUMP_IF_FALSE:
      return "OP_JUMP_IF_FALSE";
    case OAK_OP_LOOP:
      return "OP_LOOP";
    case OAK_OP_PRINT:
      return "OP_PRINT";
    default:
      return "OP_UNKNOWN";
  }
}

static int snprint_value(char* buf, const size_t size, const oak_value_t value)
{
  switch (value.type)
  {
    case OAK_VAL_BOOL:
      return snprintf(buf, size, "%s", oak_as_bool(value) ? "true" : "false");
    case OAK_VAL_NUMBER:
      if (oak_is_f32(value))
        return snprintf(buf, size, "%g", (double)oak_as_f32(value));
      return snprintf(buf, size, "%d", oak_as_i32(value));
    case OAK_VAL_OBJ:
      if (oak_is_string(value))
        return snprintf(buf, size, "\"%s\"", oak_as_cstring(value));
      return snprintf(buf, size, "%p", (void*)oak_as_obj(value));
    default:
      buf[0] = 0;
      return 0;
  }
}

static const char*
debug_local_name(const oak_chunk_t* chunk, const int slot, const size_t offset)
{
  for (size_t i = chunk->debug_count; i > 0; --i)
  {
    const oak_debug_local_t* d = &chunk->debug_locals[i - 1];
    if (d->slot == slot && d->offset <= offset)
      return d->name;
  }
  return NULL;
}

static size_t disassemble_instruction(const oak_chunk_t* chunk,
                                      const size_t offset)
{
  char line[16];
  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1])
    snprintf(line, sizeof(line), "   |");
  else
    snprintf(line, sizeof(line), "%4d", chunk->lines[offset]);

  const uint8_t op = chunk->bytecode[offset];

  switch (op)
  {
    case OAK_OP_CONSTANT:
    {
      const uint8_t idx = chunk->bytecode[offset + 1];
      char val[64];
      snprint_value(val, sizeof(val), chunk->constants[idx]);
      oak_log(OAK_LOG_INF,
              "%04zu %s  %-16s %4d ; %s",
              offset,
              line,
              opcode_name(op),
              idx,
              val);
      return offset + 2;
    }
    case OAK_OP_GET_LOCAL:
    case OAK_OP_SET_LOCAL:
    {
      const uint8_t slot = chunk->bytecode[offset + 1];
      const char* name = debug_local_name(chunk, slot, offset);
      if (name)
        oak_log(OAK_LOG_INF,
                "%04zu %s  %-16s %4d ; %s",
                offset,
                line,
                opcode_name(op),
                slot,
                name);
      else
        oak_log(OAK_LOG_INF,
                "%04zu %s  %-16s %4d",
                offset,
                line,
                opcode_name(op),
                slot);
      return offset + 2;
    }
    case OAK_OP_JUMP:
    case OAK_OP_JUMP_IF_FALSE:
    {
      const uint16_t jump = (uint16_t)(chunk->bytecode[offset + 1] << 8) |
                            chunk->bytecode[offset + 2];
      oak_log(OAK_LOG_INF,
              "%04zu %s  %-16s %4d -> %04zu",
              offset,
              line,
              opcode_name(op),
              jump,
              offset + 3 + jump);
      return offset + 3;
    }
    case OAK_OP_LOOP:
    {
      const uint16_t jump = (uint16_t)(chunk->bytecode[offset + 1] << 8) |
                            chunk->bytecode[offset + 2];
      oak_log(OAK_LOG_INF,
              "%04zu %s  %-16s %4d -> %04zu",
              offset,
              line,
              opcode_name(op),
              jump,
              offset + 3 - jump);
      return offset + 3;
    }
    default:
      oak_log(OAK_LOG_INF, "%04zu %s  %s", offset, line, opcode_name(op));
      return offset + 1;
  }
}

void oak_chunk_disassemble(const oak_chunk_t* chunk)
{
  oak_log(OAK_LOG_INF,
          "---- chunk [%zu bytes, %zu constants] ----",
          chunk->count,
          chunk->const_count);
  size_t offset = 0;
  while (offset < chunk->count)
    offset = disassemble_instruction(chunk, offset);
}
