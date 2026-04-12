#pragma once

#include "oak_log.h"
#include "oak_test_status.h"

struct oak_test_t
{
  enum oak_test_status_t (*fn)(void);
  const char* name;
};

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
#define OAK_TEST_DECL(fn_name) enum oak_test_status_t fn_name(void)

#define OAK_TEST_ENTRY(label)                                                  \
  {                                                                            \
    .fn = (label),                                                             \
    .name = (#label),                                                          \
  }

#define OAK_CHECK(expr)                                                        \
  do                                                                           \
  {                                                                            \
    if (!(expr))                                                               \
    {                                                                          \
      oak_log(OAK_LOG_ERR,                                                     \
              "check failed: %s (%s:%d)",                                      \
              #expr,                                                           \
              oak_filename(__FILE__),                                          \
              __LINE__);                                                       \
      return OAK_TEST_FAIL;                                                    \
    }                                                                          \
  } while (0)
