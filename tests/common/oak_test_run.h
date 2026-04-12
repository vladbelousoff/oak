#pragma once

#include "oak_log.h"
#include "oak_mem.h"
#include "oak_test.h"

/**
 * Run an array of tests and return an exit code.
 * Call this from each test executable's main().
 */
static int oak_test_run(const struct oak_test_t* tests, const int count)
{
  enum oak_result_t result = OAK_SUCCESS;
  oak_mem_init();

  for (int i = 0; i < count; ++i)
  {
    const struct oak_test_t* t = &tests[i];
    oak_log(OAK_LOG_INF, "running %s...", t->name);
    result = t->fn();
    if (result != OAK_SUCCESS)
    {
      oak_log(OAK_LOG_ERR, "failed: %s", t->name);
      break;
    }
    oak_log(OAK_LOG_INF, "passed: %s", t->name);
  }

  oak_mem_shutdown();
  return result == OAK_SUCCESS ? 0 : 1;
}

/** Convenience macro for the common single-test-per-exe case. */
#define OAK_TEST_MAIN(label)                                                   \
  int main(const int argc, char* argv[])                                       \
  {                                                                            \
    (void)argc;                                                                \
    (void)argv;                                                                \
    static struct oak_test_t t[] = {                                           \
      OAK_TEST_ENTRY(label),                                                   \
    };                                                                         \
    return oak_test_run(t, 1);                                                 \
  }
