#pragma once

#include "oak_types.h"

#include <string.h>

/* Compare identifier-like strings. Tokens and runtime names are stored with a
 * trailing terminal byte, so length is only kept for callers that need it. */
static inline int
oak_name_eq(const char* a, const char* b)
{
  return strcmp(a, b) == 0;
}
