#pragma once

#include "oak_token.h"
#include "oak_types.h"
#include "oak_value.h"

struct oak_compile_options_t;

/* Filled by oak_stdlib_oak_token_inspect for a native OakToken value. */
struct oak_stdlib_token_view_t
{
  enum oak_token_kind_t kind;
  int line;
  int column;
  int offset;
  int i32;
  float f32;
  const char* text_ptr;
  usize text_len;
};

void oak_stdlib_register_lexer(struct oak_compile_options_t* opts);

/* Returns 0 if `v` is a registered OakToken native record; fills `out`.
 * Returns -1 if lexer types are not registered, `out` is null, or `v` is not
 * an OakToken. */
int oak_stdlib_oak_token_inspect(const struct oak_value_t v,
                                 struct oak_stdlib_token_view_t* out);
