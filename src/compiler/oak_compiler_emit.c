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

u8 oak_compiler_intern_constant(struct oak_compiler_t* c,
                                const struct oak_value_t value)
{
  if (c->chunk->const_count >= 256)
  {
    oak_compiler_error_at(c, null, "too many constants in one chunk (max 256)");
    return 0;
  }
  const usize idx = oak_chunk_add_constant(c->chunk, value);
  oak_assert(idx <= 255);
  return (u8)idx;
}

usize oak_compiler_emit_jump(struct oak_compiler_t* c,
                             const u8 op,
                             const struct oak_code_loc_t loc)
{
  oak_compiler_emit_op(c, op, loc);
  oak_compiler_emit_byte(c, 0xff, loc);
  oak_compiler_emit_byte(c, 0xff, loc);
  return c->chunk->count - 2;
}

/* On range error, leaves placeholder operands; do not execute bytecode if
 * has_error. */
void oak_compiler_patch_jump(struct oak_compiler_t* c, const usize offset)
{
  const usize jump = c->chunk->count - offset - 2;
  if (jump > 0xffff)
  {
    oak_compiler_error_at(c, null, "jump offset too large (max 65535 bytes)");
    return;
  }

  c->chunk->bytecode[offset]     = (u8)(jump >> 8);
  c->chunk->bytecode[offset + 1] = (u8)(jump);
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
  const usize jump = c->chunk->count - loop_start + 2;
  if (jump > 0xffff)
  {
    oak_compiler_error_at(c, null, "loop body too large (max 65535 bytes)");
    return;
  }

  oak_compiler_emit_byte(c, (u8)(jump >> 8), loc);
  oak_compiler_emit_byte(c, (u8)(jump),      loc);
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
