#pragma once

#include "oak_types.h"

#include <string.h>

/* Compare identifier-like strings. */
static inline int
oak_name_eq(const char* a, const char* b)
{
  return strcmp(a, b) == 0;
}
