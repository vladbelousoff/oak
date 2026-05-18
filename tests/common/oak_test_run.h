#pragma once

#include "oak_allocator.h"
#include "oak_log.h"
#include "oak_mem.h"
#include "oak_test.h"

static int oak_test_run(const struct oak_test_t* tests, const int count)
{
  struct oak_allocator_t allocator;
  oak_tracking_allocator_init(&allocator);
  oak_mem_set_allocator(&allocator);

  enum oak_test_status_t result = OAK_TEST_OK;

  for (int i = 0; i < count; ++i)
  {
    const struct oak_test_t* t = &tests[i];
    oak_log(OAK_LOG_INFO, "running %s...", t->name);
    result = t->fn();
    if (result != OAK_TEST_OK)
    {
      oak_log(OAK_LOG_ERROR, "failed: %s", t->name);
      break;
    }
    oak_log(OAK_LOG_INFO, "passed: %s", t->name);
  }

  allocator.shutdown(&allocator);
  return result == OAK_TEST_OK ? 0 : (int)result;
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
