#include "internal/oak_compiler.h"

void oak_compiler_emit_byte(const oak_compiler_t* c,
                            const u8 byte,
                            const oak_code_loc_t loc)
{
  oak_chunk_write(c->chunk, byte, loc);
}

void oak_compiler_emit_op_impl(oak_compiler_t* c,
                               const u8 op,
                               const oak_code_loc_t loc,
                               const oak_emit_arg_t* args,
                               const int n_args)
{
  oak_compiler_emit_byte(c, op, loc);
  for (int i = 0; i < n_args; i++)
  {
    if (args[i].type == OAK_EMIT_U8)
    {
      oak_compiler_emit_byte(c, (u8)args[i].value, loc);
    }
    else
    {
      oak_compiler_emit_byte(c, (u8)(args[i].value >> 8u), loc);
      oak_compiler_emit_byte(c, (u8)(args[i].value & 0xffu), loc);
    }
  }
  c->scope.stack_depth += oak_op_info[op].stack_effect;
}

u16 oak_compiler_intern_constant(oak_compiler_t* c,
                                 const oak_value_t value)
{
  if (oak_size(c->chunk->constants) >= 65536)
  {
    oak_compiler_error_at(
        c, null, "too many constants in one chunk (max 65536)");
    return 0;
  }
  /* Deduplicate integer and float constants to conserve pool slots. */
  if (oak_is_number(value))
  {
    const oak_value_t* const constants =
        OAK_CDATA(oak_value_t, c->chunk->constants);
    for (usize i = 0; i < oak_size(c->chunk->constants); ++i)
    {
      if (oak_value_equal(constants[i], value))
        return (u16)i;
    }
  }
  const usize idx = oak_chunk_add_constant(c->chunk, value);
  oak_assert(idx <= 65535);
  return (u16)idx;
}

/* Emit a constant load using OP_CONSTANT (1-byte index) for small pools or
 * OP_CONSTANT_LONG (2-byte index) for larger ones. */
void oak_compiler_emit_constant(oak_compiler_t* c,
                                const u16 idx,
                                const oak_code_loc_t loc)
{
  oak_compiler_emit_op(c, OAK_OP_CONSTANT, loc, OAK_ARG_U16(idx));
}

usize oak_compiler_emit_jump(oak_compiler_t* c,
                             const u8 op,
                             const oak_code_loc_t loc)
{
  oak_compiler_emit_op(c, op, loc);
  /* Reserve 2 bytes for the 16-bit forward jump offset (big-endian). */
  oak_compiler_emit_byte(c, 0xff, loc);
  oak_compiler_emit_byte(c, 0xff, loc);
  return oak_chunk_size(c->chunk) - 2;
}

void oak_compiler_patch_jump(oak_compiler_t* c, const usize offset)
{
  /* Distance from end of the 2-byte operand to the current position. */
  const usize jump = oak_chunk_size(c->chunk) - offset - 2;
  if (jump > 0xFFFFu)
  {
    oak_compiler_error_at(
        c, null, "jump distance %zu exceeds 16-bit limit (max 65535)", jump);
    return;
  }
  u8* const code = OAK_DATA(u8, c->chunk->code);
  code[offset] = (u8)(jump >> 8);
  code[offset + 1] = (u8)(jump);
}

void oak_compiler_patch_jumps(oak_compiler_t* c,
                              const oak_container_t* jumps)
{
  const usize* offsets = OAK_CDATA(usize, jumps);
  for (usize i = 0; i < oak_size(jumps); ++i)
    oak_compiler_patch_jump(c, offsets[i]);
}

void oak_compiler_emit_loop(oak_compiler_t* c,
                            const usize loop_start,
                            const oak_code_loc_t loc)
{
  oak_compiler_emit_op(c, OAK_OP_LOOP, loc);
  /* The 2-byte operand itself is included in the backward distance. */
  const usize jump = oak_chunk_size(c->chunk) - loop_start + 2;
  if (jump > 0xFFFFu)
  {
    oak_compiler_error_at(
        c, null, "loop distance %zu exceeds 16-bit limit (max 65535)", jump);
    oak_compiler_emit_byte(c, 0, loc);
    oak_compiler_emit_byte(c, 0, loc);
    return;
  }
  oak_compiler_emit_byte(c, (u8)(jump >> 8), loc);
  oak_compiler_emit_byte(c, (u8)(jump), loc);
}

void oak_compiler_emit_pops(oak_compiler_t* c,
                            int count,
                            const oak_code_loc_t loc)
{
  if (count <= 0)
    return;
  if (count == 1)
  {
    oak_compiler_emit_op(c, OAK_OP_POP, loc);
    return;
  }
  /* OP_POP_N has variadic stack effect; track it manually here. */
  while (count > 0)
  {
    const int chunk = count > 255 ? 255 : count;
    oak_compiler_emit_byte(c, OAK_OP_POP_N, loc);
    oak_compiler_emit_byte(c, (u8)chunk, loc);
    c->scope.stack_depth -= chunk;
    count -= chunk;
  }
}

void oak_emit_loop_jump(oak_compiler_t* c,
                         oak_container_t* jumps,
                         const int target_depth)
{
  const int saved_depth = c->scope.stack_depth;
  oak_compiler_emit_pops(
      c, c->scope.stack_depth - target_depth, OAK_LOC_SYNTHETIC);

  const usize jump = oak_compiler_emit_jump(c, OAK_OP_JUMP, OAK_LOC_SYNTHETIC);
  oak_assert(oak_push_back(jumps, &jump));
  c->scope.stack_depth = saved_depth;
}
