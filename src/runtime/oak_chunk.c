#include "oak_chunk_impl.h"

#include "oak_allocator.h"
#include "oak_count_of.h"
#include "oak_log.h"
#include "oak_vector.h"

#include <stdio.h>
#include <string.h>

/* Reserved up front rather than grown from the vector's default so that a
 * chunk of typical size never reallocates during compilation. */
#define CHUNK_INITIAL_CAPACITY 256
#define CONST_INITIAL_CAPACITY 16

const oak_op_info_t oak_op_info[] = {
  [OAK_OP_HALT] = { "OP_HALT", OAK_OP_FMT_NONE, 0 },
  [OAK_OP_CONSTANT] = { "OP_CONSTANT", OAK_OP_FMT_CONSTANT, 1 },
  [OAK_OP_PUSH_INT8] = { "OP_PUSH_INT8", OAK_OP_FMT_INT8, 1 },
  [OAK_OP_TRUE] = { "OP_TRUE", OAK_OP_FMT_NONE, 1 },
  [OAK_OP_FALSE] = { "OP_FALSE", OAK_OP_FMT_NONE, 1 },
  [OAK_OP_NONE] = { "OP_NONE", OAK_OP_FMT_NONE, 1 },
  [OAK_OP_BOOL] = { "OP_BOOL", OAK_OP_FMT_NONE, 0 },
  [OAK_OP_POP] = { "OP_POP", OAK_OP_FMT_NONE, -1 },
  /* Variadic stack effect: tracked explicitly at the emit site. */
  [OAK_OP_POP_N] = { "OP_POP_N", OAK_OP_FMT_ARGC, 0 },
  [OAK_OP_GET_LOCAL] = { "OP_GET_LOCAL", OAK_OP_FMT_SLOT, 1 },
  [OAK_OP_SET_LOCAL] = { "OP_SET_LOCAL", OAK_OP_FMT_SLOT, -1 },
  [OAK_OP_WEAKEN] = { "OP_WEAKEN", OAK_OP_FMT_NONE, 0 },
  [OAK_OP_INC_LOCAL] = { "OP_INC_LOCAL", OAK_OP_FMT_SLOT, 0 },
  [OAK_OP_DEC_LOCAL] = { "OP_DEC_LOCAL", OAK_OP_FMT_SLOT, 0 },
  [OAK_OP_ADD] = { "OP_ADD", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_SUBTRACT] = { "OP_SUBTRACT", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_MULTIPLY] = { "OP_MULTIPLY", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_DIVIDE] = { "OP_DIVIDE", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_INT_DIVIDE] = { "OP_INT_DIVIDE", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_MODULO] = { "OP_MODULO", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_EQUAL] = { "OP_EQUAL", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_NOT_EQUAL] = { "OP_NOT_EQUAL", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_LESS] = { "OP_LESS", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_LESS_EQUAL] = { "OP_LESS_EQUAL", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_GREATER] = { "OP_GREATER", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_GREATER_EQUAL] = { "OP_GREATER_EQUAL", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_NEGATE] = { "OP_NEGATE", OAK_OP_FMT_NONE, 0 },
  [OAK_OP_NOT] = { "OP_NOT", OAK_OP_FMT_NONE, 0 },
  [OAK_OP_JUMP] = { "OP_JUMP", OAK_OP_FMT_JUMP_FWD, 0 },
  [OAK_OP_JUMP_IF_FALSE] = { "OP_JUMP_IF_FALSE", OAK_OP_FMT_JUMP_FWD, -1 },
  [OAK_OP_JUMP_IF_TRUE] = { "OP_JUMP_IF_TRUE", OAK_OP_FMT_JUMP_FWD, -1 },
  [OAK_OP_LOOP] = { "OP_LOOP", OAK_OP_FMT_JUMP_BACK, 0 },
  [OAK_OP_CALL] = { "OP_CALL", OAK_OP_FMT_ARGC, 0 },
  [OAK_OP_RETURN] = { "OP_RETURN", OAK_OP_FMT_NONE, 0 },
  [OAK_OP_NEW_ARR] = { "OP_NEW_ARR", OAK_OP_FMT_ARGC, 1 },
  [OAK_OP_NEW_MAP] = { "OP_NEW_MAP", OAK_OP_FMT_ARGC, 1 },
  [OAK_OP_GET_INDEX] = { "OP_GET_INDEX", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_SET_INDEX] = { "OP_SET_INDEX", OAK_OP_FMT_NONE, -3 },
  [OAK_OP_MAP_KEY_AT] = { "OP_MAP_KEY_AT", OAK_OP_FMT_NONE, -1 },
  [OAK_OP_MAP_VAL_AT] = { "OP_MAP_VAL_AT", OAK_OP_FMT_NONE, -1 },
  /* Pops <count> field values plus one type-name string from the stack and
   * pushes a fresh record. Stack effect counts only the name (consumed) and
   * the produced record; <count> is variable and adjusted at compile time. */
  [OAK_OP_NEW_RECORD] = { "OP_NEW_RECORD", OAK_OP_FMT_U8_U16, 0 },
  [OAK_OP_GET_FIELD] = { "OP_GET_FIELD", OAK_OP_FMT_SLOT, 0 },
  /* Stack: [..., record, value] -> [...]; sets record.fields[idx] and discards.
   */
  [OAK_OP_SET_FIELD] = { "OP_SET_FIELD", OAK_OP_FMT_SLOT, -2 },
  [OAK_OP_GET_MODULE_FN] = { "OP_GET_MODULE_FN", OAK_OP_FMT_U16_U16, 1 },
  /* Pops concrete value, pushes interface object; stack effect = 0 (net). */
  [OAK_OP_MAKE_INTERFACE_OBJECT] = { "OP_MAKE_INTERFACE_OBJECT", OAK_OP_FMT_CONSTANT, 0 },
  /* Virtual call through interface object: variadic stack effect. */
  [OAK_OP_CALL_VIRTUAL] = { "OP_CALL_VIRTUAL", OAK_OP_FMT_U8_U8, 0 },
  /* Fused compare + branch. */
  [OAK_OP_LESS_JUMP_IF_FALSE] = { "OP_LESS_JUMP_IF_FALSE", OAK_OP_FMT_JUMP_FWD, -2 },
  [OAK_OP_LESS_EQUAL_JUMP_IF_FALSE] = { "OP_LESS_EQUAL_JUMP_IF_FALSE", OAK_OP_FMT_JUMP_FWD, -2 },
  [OAK_OP_GREATER_JUMP_IF_FALSE] = { "OP_GREATER_JUMP_IF_FALSE", OAK_OP_FMT_JUMP_FWD, -2 },
  [OAK_OP_GREATER_EQUAL_JUMP_IF_FALSE] = { "OP_GREATER_EQUAL_JUMP_IF_FALSE", OAK_OP_FMT_JUMP_FWD, -2 },
  /* Superinstructions. */
  [OAK_OP_GET_LOCAL_GET_LOCAL] = { "OP_GET_LOCAL_GET_LOCAL", OAK_OP_FMT_U8_U8, 2 },
  [OAK_OP_INC_LOCAL_LOOP] = { "OP_INC_LOCAL_LOOP", OAK_OP_FMT_U8_U16, 0 },
};

#define OAK_OP_INFO_COUNT oak_count_of(oak_op_info)

const oak_op_info_t* oak_op_get_info(const u8 op)
{
  if (op < OAK_OP_INFO_COUNT && oak_op_info[op].name)
    return &oak_op_info[op];
  return null;
}

static const char* const oak_binop_names[] = {
  [OAK_BINOP_ADD] = "ADD",
  [OAK_BINOP_SUBTRACT] = "SUBTRACT",
  [OAK_BINOP_MULTIPLY] = "MULTIPLY",
  [OAK_BINOP_DIVIDE] = "DIVIDE",
  [OAK_BINOP_INT_DIVIDE] = "INT_DIVIDE",
  [OAK_BINOP_MODULO] = "MODULO",
  [OAK_BINOP_EQUAL] = "EQUAL",
  [OAK_BINOP_NOT_EQUAL] = "NOT_EQUAL",
  [OAK_BINOP_LESS] = "LESS",
  [OAK_BINOP_LESS_EQUAL] = "LESS_EQUAL",
  [OAK_BINOP_GREATER] = "GREATER",
  [OAK_BINOP_GREATER_EQUAL] = "GREATER_EQUAL",
};

const char* oak_binop_name(const u8 binop)
{
  if (binop < oak_count_of(oak_binop_names) && oak_binop_names[binop])
    return oak_binop_names[binop];
  return "?";
}

void oak_chunk_init(oak_chunk_t* chunk, oak_allocator_t* allocator)
{
  chunk->allocator = allocator;
  chunk->code = oak_vector_new(allocator, sizeof(u8));
  chunk->constants = oak_vector_new(allocator, sizeof(oak_value_t));
  chunk->field_layouts =
      oak_vector_new(allocator, sizeof(oak_chunk_field_layout_t));
  oak_assert(chunk->code && chunk->constants && chunk->field_layouts);
  oak_assert(oak_reserve(chunk->code, CHUNK_INITIAL_CAPACITY));
  oak_assert(oak_reserve(chunk->constants, CONST_INITIAL_CAPACITY));
  chunk->debug = null;
  chunk->module_id = 0xFFFFu; /* OAK_MODULE_ID_NONE; no oak_module.h dep */
}

void oak_chunk_enable_debug(oak_chunk_t* chunk, const char* source_name)
{
  if (chunk->debug)
  {
    chunk->debug->source_name = source_name;
    return;
  }
  oak_chunk_debug_t* dbg =
      oak_alloc(chunk->allocator, sizeof(oak_chunk_debug_t), OAK_HERE);
  dbg->source_name = source_name;
  dbg->locations =
      oak_vector_new(chunk->allocator, sizeof(oak_code_loc_t));
  dbg->debug_locals =
      oak_vector_new(chunk->allocator, sizeof(oak_debug_local_t));
  oak_assert(dbg->locations && dbg->debug_locals);
  /* Debug can be enabled after some code is already written; backfill so the
   * two vectors stay index-aligned. */
  oak_assert(oak_resize(dbg->locations, oak_size(chunk->code)));
  chunk->debug = dbg;
}

int oak_chunk_add_field_layout(oak_chunk_t* const c,
                               const int n,
                               const char* const* const names)
{
  if (n < 0 || n > OAK_CHUNK_MAX_RECORD_FIELDS)
    return -1;
  const usize count = oak_size(c->field_layouts);
  const oak_chunk_field_layout_t* const existing =
      OAK_CDATA(oak_chunk_field_layout_t, c->field_layouts);
  for (usize k = 0; k < count; ++k)
  {
    const oak_chunk_field_layout_t* e = &existing[k];
    if (e->field_count != n)
      continue;
    int j;
    for (j = 0; j < n; ++j)
    {
      if (strcmp(names[j], e->name[j]) != 0)
        break;
    }
    if (j == n)
      return (int)k;
  }

  /* Zeroed because only the first n name slots are filled in and the whole
   * struct gets copied into the vector. */
  oak_chunk_field_layout_t d = { 0 };
  d.field_count = n;
  usize tot = 0u;
  for (int i = 0; i < n; ++i)
  {
    const usize a = strlen(names[i]);
    tot += a + 1u;
  }
  char* const blob = oak_alloc(c->allocator, tot, OAK_HERE);
  d.name_blob = blob;
  {
    char* p = blob;
    for (int i = 0; i < n; ++i)
    {
      const usize a = strlen(names[i]);
      memcpy(p, names[i], a);
      p[a] = '\0';
      d.name[i] = p;
      p += a + 1u;
    }
  }
  /* The name pointers refer into the heap blob, not into the entry, so they
   * survive the vector reallocating. */
  if (!oak_push_back(c->field_layouts, &d))
  {
    oak_free(c->allocator, blob, OAK_HERE);
    return -1;
  }
  return (int)count;
}

void oak_chunk_write(oak_chunk_t* chunk,
                     const u8 byte,
                     const oak_code_loc_t loc)
{
  oak_assert(oak_push_back(chunk->code, &byte));
  if (chunk->debug)
    oak_assert(oak_push_back(chunk->debug->locations, &loc));
}

usize oak_chunk_add_constant(oak_chunk_t* chunk,
                             const oak_value_t value)
{
  const usize index = oak_size(chunk->constants);
  oak_assert(oak_push_back(chunk->constants, &value));
  return index;
}

void oak_chunk_add_debug_local(oak_chunk_t* chunk,
                               const int slot,
                               const char* name)
{
  const usize length = strlen(name);
  if (length == 0)
    return;
  if (!chunk->debug)
    return;

  char* buf = oak_alloc(chunk->allocator, length + 1, OAK_HERE);
  memcpy(buf, name, length);
  buf[length] = 0;

  const oak_debug_local_t d = {
    .slot = slot,
    .offset = oak_size(chunk->code),
    .end_offset = (usize)-1,
    .name = buf,
  };
  if (!oak_push_back(chunk->debug->debug_locals, &d))
    oak_free(chunk->allocator, buf, OAK_HERE);
}

void oak_chunk_end_debug_local(oak_chunk_t* chunk, const int slot)
{
  if (!chunk->debug)
    return;
  oak_debug_local_t* locals =
      OAK_DATA(oak_debug_local_t, chunk->debug->debug_locals);
  /* Counts down from the count rather than from count-1: with usize indices a
   * loop ending at `i >= 0` would never terminate. */
  for (usize i = oak_size(chunk->debug->debug_locals); i > 0; --i)
  {
    oak_debug_local_t* d = &locals[i - 1];
    if (d->slot == slot && d->end_offset == (usize)-1)
    {
      d->end_offset = oak_size(chunk->code);
      return;
    }
  }
}

void oak_chunk_free(oak_chunk_t* chunk)
{
  oak_allocator_t* a = chunk->allocator;

  {
    const oak_value_t* constants =
        OAK_CDATA(oak_value_t, chunk->constants);
    for (usize i = 0; i < oak_size(chunk->constants); ++i)
      oak_value_decref(constants[i]);
    oak_destroy(chunk->constants);
  }

  if (chunk->debug)
  {
    oak_chunk_debug_t* dbg = chunk->debug;
    const oak_debug_local_t* locals =
        OAK_CDATA(oak_debug_local_t, dbg->debug_locals);
    for (usize i = 0; i < oak_size(dbg->debug_locals); ++i)
      oak_free(a, locals[i].name, OAK_HERE);
    oak_destroy(dbg->debug_locals);
    oak_destroy(dbg->locations);
    oak_free(a, dbg, OAK_HERE);
  }

  oak_destroy(chunk->code);

  {
    const oak_chunk_field_layout_t* layouts =
        OAK_CDATA(oak_chunk_field_layout_t, chunk->field_layouts);
    for (usize i = 0; i < oak_size(chunk->field_layouts); ++i)
      oak_free(a, layouts[i].name_blob, OAK_HERE);
    oak_destroy(chunk->field_layouts);
  }

  oak_free(a, chunk, OAK_HERE);
}

static const char* opcode_name(const u8 op)
{
  const oak_op_info_t* info = oak_op_get_info(op);
  return info ? info->name : "OP_UNKNOWN";
}

static int
snprint_value(char* buf, const usize size, const oak_value_t value)
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
      const oak_obj_record_t* s = oak_as_record(value);
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

static const char* debug_local_name(const oak_chunk_t* chunk,
                                    const int slot,
                                    const usize offset)
{
  if (!chunk->debug)
    return null;
  const oak_chunk_debug_t* dbg = chunk->debug;
  const oak_debug_local_t* locals =
      OAK_CDATA(oak_debug_local_t, dbg->debug_locals);
  for (usize i = oak_size(dbg->debug_locals); i > 0; --i)
  {
    const oak_debug_local_t* d = &locals[i - 1];
    if (d->slot == slot && d->offset <= offset &&
        (d->end_offset == (usize)-1 || offset < d->end_offset))
      return d->name;
  }
  return null;
}

usize oak_chunk_disassemble_instruction(const oak_chunk_t* chunk,
                                       const usize offset)
{
  char line[16];
  const u8* const code = oak_chunk_code(chunk);
  const oak_value_t* const constants = OAK_CDATA(oak_value_t, chunk->constants);
  const oak_code_loc_t* locs =
      chunk->debug ? OAK_CDATA(oak_code_loc_t, chunk->debug->locations) : null;
  if (!locs)
    snprintf(line, sizeof(line), "   ?");
  else if (offset > 0 && locs[offset].line == locs[offset - 1].line)
    snprintf(line, sizeof(line), "   |");
  else
    snprintf(line, sizeof(line), "%4d", locs[offset].line);

  const u8 op = code[offset];
  const char* name = opcode_name(op);
  const oak_op_info_t* info = oak_op_get_info(op);
  const oak_op_format_t fmt = info ? info->format : OAK_OP_FMT_NONE;

  switch (fmt)
  {
    case OAK_OP_FMT_CONSTANT:
    {
      const u16 idx = (u16)((u16)code[offset + 1] << 8) |
                      code[offset + 2];
      char val[64];
      snprint_value(val, sizeof(val), constants[idx]);
      oak_log(OAK_LOG_INFO,
              "%04zu %s  %-20s %4u ; %s",
              offset,
              line,
              name,
              (unsigned)idx,
              val);
      return offset + 3;
    }
    case OAK_OP_FMT_INT8:
    {
      const signed char val = (signed char)code[offset + 1];
      oak_log(
          OAK_LOG_INFO, "%04zu %s  %-20s %4d", offset, line, name, (int)val);
      return offset + 2;
    }
    case OAK_OP_FMT_SLOT:
    {
      const u8 slot = code[offset + 1];
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
      const u16 jump = (u16)(((u16)code[offset + 1] << 8) |
                             code[offset + 2]);
      oak_log(OAK_LOG_INFO,
              "%04zu %s  %-20s %6u -> %04zu",
              offset,
              line,
              name,
              (unsigned)jump,
              offset + 3 + (usize)jump);
      return offset + 3;
    }
    case OAK_OP_FMT_JUMP_BACK:
    {
      const u16 jump = (u16)(((u16)code[offset + 1] << 8) |
                             code[offset + 2]);
      oak_log(OAK_LOG_INFO,
              "%04zu %s  %-20s %6u -> %04zu",
              offset,
              line,
              name,
              (unsigned)jump,
              offset + 3 - (usize)jump);
      return offset + 3;
    }
    case OAK_OP_FMT_ARGC:
    {
      const u8 argc = code[offset + 1];
      oak_log(OAK_LOG_INFO, "%04zu %s  %-20s %4d", offset, line, name, argc);
      return offset + 2;
    }
    case OAK_OP_FMT_BINOP:
    {
      const u8 binop = code[offset + 1];
      oak_log(OAK_LOG_INFO,
              "%04zu %s  %-20s %s",
              offset,
              line,
              name,
              oak_binop_name(binop));
      return offset + 2;
    }
    case OAK_OP_FMT_U8_U16:
    {
      const u8 a = code[offset + 1];
      const u16 b = (u16)((u16)code[offset + 2] << 8) |
                    code[offset + 3];
      oak_log(OAK_LOG_INFO,
              "%04zu %s  %-20s  %3u, layout %4u",
              offset,
              line,
              name,
              (unsigned)a,
              (unsigned)b);
      return offset + 4;
    }
    case OAK_OP_FMT_U16_U16:
    {
      const u16 a = (u16)((u16)code[offset + 1] << 8) |
                    code[offset + 2];
      const u16 b = (u16)((u16)code[offset + 3] << 8) |
                    code[offset + 4];
      oak_log(OAK_LOG_INFO,
              "%04zu %s  %-20s  mod %4u, idx %4u",
              offset,
              line,
              name,
              (unsigned)a,
              (unsigned)b);
      return offset + 5;
    }
    case OAK_OP_FMT_U8_U8:
    {
      const u8 a = code[offset + 1];
      const u8 b = code[offset + 2];
      oak_log(OAK_LOG_INFO,
              "%04zu %s  %-20s  slot %3u, argc %3u",
              offset,
              line,
              name,
              (unsigned)a,
              (unsigned)b);
      return offset + 3;
    }
    default:
      oak_log(OAK_LOG_INFO, "%04zu %s  %s", offset, line, name);
      return offset + 1;
  }
}

void oak_chunk_disassemble(const oak_chunk_t* chunk)
{
  oak_log(OAK_LOG_INFO,
          "---- chunk [%zu bytes, %zu constants]%s ----",
          oak_chunk_size(chunk),
          oak_size(chunk->constants),
          chunk->debug ? "" : " (no debug info)");
  usize offset = 0;
  while (offset < oak_chunk_size(chunk))
    offset = oak_chunk_disassemble_instruction(chunk, offset);
}
