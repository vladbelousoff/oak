#pragma once

#include "oak_state.h"
#include "oak_chunk_impl.h"


void oak_compiler_emit_byte(const oak_compiler_t* c,
                            u8 byte,
                            oak_code_loc_t loc);

typedef enum
{
  OAK_EMIT_U8,
  OAK_EMIT_U16
} oak_emit_arg_type_t;
typedef struct
{
  oak_emit_arg_type_t type;
  u16 value;
} oak_emit_arg_t;

#define OAK_ARG_U8(v)  ((oak_emit_arg_t){ OAK_EMIT_U8, (u16)(v) })
#define OAK_ARG_U16(v) ((oak_emit_arg_t){ OAK_EMIT_U16, (v) })

void oak_compiler_emit_op_impl(oak_compiler_t* c,
                               u8 op,
                               oak_code_loc_t loc,
                               const oak_emit_arg_t* args,
                               int n_args);

/* Variadic emit: oak_compiler_emit_op(c, op, loc [, OAK_ARG_U8/U16, ...]) */
#define oak_compiler_emit_op(c, op, loc, ...)                                  \
  oak_compiler_emit_op_impl(                                                   \
      c,                                                                       \
      op,                                                                      \
      loc,                                                                     \
      (const oak_emit_arg_t[]){ { 0 }, ##__VA_ARGS__ } + 1,                    \
      (int)(sizeof((const oak_emit_arg_t[]){ { 0 }, ##__VA_ARGS__ }) /         \
            sizeof(oak_emit_arg_t)) -                                          \
          1)

u16 oak_compiler_intern_constant(oak_compiler_t* c,
                                 oak_value_t value);

void oak_compiler_emit_constant(oak_compiler_t* c,
                                u16 idx,
                                oak_code_loc_t loc);

usize oak_compiler_emit_jump(oak_compiler_t* c,
                             u8 op,
                             oak_code_loc_t loc);

void oak_compiler_patch_jump(oak_compiler_t* c, usize offset);

/* Patches every jump offset held in `jumps` (a vector of usize). */
void oak_compiler_patch_jumps(oak_compiler_t* c,
                              const oak_container_t* jumps);

void oak_compiler_emit_loop(oak_compiler_t* c,
                            usize loop_start,
                            oak_code_loc_t loc);

void oak_compiler_emit_pops(oak_compiler_t* c,
                            int count,
                            oak_code_loc_t loc);

void oak_emit_loop_jump(oak_compiler_t* c,
                         oak_container_t* jumps,
                         int target_depth);
