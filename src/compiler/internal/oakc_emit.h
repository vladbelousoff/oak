#pragma once

#include "oakc_state.h"
#include "oak_chunk.h"

/* ---------- oak_compiler_emit.c ---------- */

void oak_compiler_emit_byte(const struct oak_compiler_t* c,
                            u8 byte,
                            struct oak_code_loc_t loc);

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

void oak_compiler_emit_op_impl(struct oak_compiler_t* c,
                               u8 op,
                               struct oak_code_loc_t loc,
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

u16 oak_compiler_intern_constant(struct oak_compiler_t* c,
                                 struct oak_value_t value);

void oak_compiler_emit_constant(struct oak_compiler_t* c,
                                u16 idx,
                                struct oak_code_loc_t loc);

usize oak_compiler_emit_jump(struct oak_compiler_t* c,
                             u8 op,
                             struct oak_code_loc_t loc);

void oak_compiler_patch_jump(struct oak_compiler_t* c, usize offset);

void oak_compiler_patch_jumps(struct oak_compiler_t* c,
                              const usize* jumps,
                              int count);

void oak_compiler_emit_loop(struct oak_compiler_t* c,
                            usize loop_start,
                            struct oak_code_loc_t loc);

void oak_compiler_emit_pops(struct oak_compiler_t* c,
                            int count,
                            struct oak_code_loc_t loc);

void oakc_emit_loop_jump(struct oak_compiler_t* c,
                                         usize* jumps,
                                         int* count,
                                         int target_depth,
                                         const char* keyword);
