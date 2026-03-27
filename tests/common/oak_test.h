#pragma once

#include "oak_common.h"

typedef struct
{
  oak_result_t (*fn)(void);
  const char* name;
} oak_test_t;

/**
 * Declare a test for use in the registry.
 * Each test file should expose exactly one symbol via this macro.
 *
 * Example (test_empty.c):
 *   OAK_TEST_DECL(EmptyString);
 *
 * Example (test_main.c registry):
 *   OAK_TEST_ENTRY(EmptyString),
 */
#define OAK_TEST_DECL(fn_name) oak_result_t fn_name(void)

#define OAK_TEST_ENTRY(label)                                                  \
  {                                                                            \
    .fn = (label),                                                             \
    .name = (#label),                                                          \
  }
