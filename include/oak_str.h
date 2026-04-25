#pragma once

#include "oak_types.h"

#include <string.h>

/* Compare two length-prefixed identifier-like byte sequences (lexer text is
 * semantically (ptr, len) but tokens are stored with a trailing '\\0'). */
static inline int
oak_name_eq(const char* a, usize a_len, const char* b, usize b_len)
{
  return a_len == b_len && memcmp(a, b, a_len) == 0;
}
