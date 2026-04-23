#include "oak_compiler_internal.h"

void oak_compiler_emit_byte(const struct oak_compiler_t* c,
                            const u8 byte,
                            const struct oak_code_loc_t loc)
{
  oak_chunk_write(c->chunk, byte, loc);
}

void oak_compiler_emit_op(struct oak_compiler_t* c,
                          const u8 op,
                          const struct oak_code_loc_t loc)
{
  oak_compiler_emit_byte(c, op, loc);
  const struct oak_op_info_t* info = oak_op_get_info(op);
  if (info)
    c->stack_depth += info->stack_effect;
}

void oak_compiler_emit_op_arg(struct oak_compiler_t* c,
                              const u8 op,
                              const u8 arg,
                              const struct oak_code_loc_t loc)
{
  oak_compiler_emit_byte(c, op, loc);
  oak_compiler_emit_byte(c, arg, loc);
  const struct oak_op_info_t* info = oak_op_get_info(op);
  if (info)
    c->stack_depth += info->stack_effect;
}

u16 oak_compiler_intern_constant(struct oak_compiler_t* c,
                                 const struct oak_value_t value)
{
  if (c->chunk->const_count >= 65536)
  {
    oak_compiler_error_at(c, null, "too many constants in one chunk (max 65536)");
    return 0;
  }
  /* Deduplicate integer and float constants to conserve pool slots. */
  if (oak_is_number(value))
  {
    for (usize i = 0; i < c->chunk->const_count; ++i)
    {
      const struct oak_value_t existing = c->chunk->constants[i];
      if (oak_value_equal(existing, value))
        return (u16)i;
    }
  }
  const usize idx = oak_chunk_add_constant(c->chunk, value);
  oak_assert(idx <= 65535);
  return (u16)idx;
}

/* Emit a constant load using OP_CONSTANT (1-byte index) for small pools or
 * OP_CONSTANT_LONG (2-byte index) for larger ones. */
void oak_compiler_emit_constant(struct oak_compiler_t* c,
                                const u16 idx,
                                const struct oak_code_loc_t loc)
{
  if (idx <= 255)
  {
    oak_compiler_emit_op_arg(c, OAK_OP_CONSTANT, (u8)idx, loc);
  }
  else
  {
    oak_compiler_emit_byte(c, OAK_OP_CONSTANT_LONG, loc);
    oak_compiler_emit_byte(c, (u8)(idx >> 8), loc);
    oak_compiler_emit_byte(c, (u8)(idx), loc);
    const struct oak_op_info_t* info = oak_op_get_info(OAK_OP_CONSTANT_LONG);
    if (info)
      c->stack_depth += info->stack_effect;
  }
}

usize oak_compiler_emit_jump(struct oak_compiler_t* c,
                             const u8 op,
                             const struct oak_code_loc_t loc)
{
  oak_compiler_emit_op(c, op, loc);
  /* Reserve 4 bytes for the 32-bit forward jump offset (big-endian). */
  oak_compiler_emit_byte(c, 0xff, loc);
  oak_compiler_emit_byte(c, 0xff, loc);
  oak_compiler_emit_byte(c, 0xff, loc);
  oak_compiler_emit_byte(c, 0xff, loc);
  return c->chunk->count - 4;
}

void oak_compiler_patch_jump(struct oak_compiler_t* c, const usize offset)
{
  /* Distance from end of the 4-byte operand to the current position. */
  const usize jump = c->chunk->count - offset - 4;
  c->chunk->bytecode[offset]     = (u8)(jump >> 24);
  c->chunk->bytecode[offset + 1] = (u8)(jump >> 16);
  c->chunk->bytecode[offset + 2] = (u8)(jump >> 8);
  c->chunk->bytecode[offset + 3] = (u8)(jump);
}

void oak_compiler_patch_jumps(struct oak_compiler_t* c,
                              const usize* jumps,
                              const int count)
{
  for (int i = 0; i < count; ++i)
    oak_compiler_patch_jump(c, jumps[i]);
}

void oak_compiler_emit_loop(struct oak_compiler_t* c,
                            const usize loop_start,
                            const struct oak_code_loc_t loc)
{
  oak_compiler_emit_op(c, OAK_OP_LOOP, loc);
  /* The 4-byte operand itself is included in the backward distance. */
  const usize jump = c->chunk->count - loop_start + 4;
  oak_compiler_emit_byte(c, (u8)(jump >> 24), loc);
  oak_compiler_emit_byte(c, (u8)(jump >> 16), loc);
  oak_compiler_emit_byte(c, (u8)(jump >> 8),  loc);
  oak_compiler_emit_byte(c, (u8)(jump),        loc);
}

void oak_compiler_emit_pops(struct oak_compiler_t* c,
                            int count,
                            const struct oak_code_loc_t loc)
{
  while (count-- > 0)
    oak_compiler_emit_op(c, OAK_OP_POP, loc);
}

void oak_compiler_emit_loop_control_jump(struct oak_compiler_t* c,
                                         usize* jumps,
                                         int* count,
                                         const int target_depth,
                                         const char* keyword)
{
  const int saved_depth = c->stack_depth;
  oak_compiler_emit_pops(c, c->stack_depth - target_depth, OAK_LOC_SYNTHETIC);

  if (*count >= OAK_MAX_LOOP_BRANCHES)
  {
    oak_compiler_error_at(c,
                          null,
                          "too many '%s' statements in loop (max %d)",
                          keyword,
                          OAK_MAX_LOOP_BRANCHES);
    c->stack_depth = saved_depth;
    return;
  }
  jumps[(*count)++] =
      oak_compiler_emit_jump(c, OAK_OP_JUMP, OAK_LOC_SYNTHETIC);
  c->stack_depth = saved_depth;
}
