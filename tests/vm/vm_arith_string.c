#include "oak_count_of.h"
#include "oak_test_pipeline.h"

/* '+' on strings concatenates. */
OAK_TEST_DECL(StringConcatRuns)
{
  return expect_ok("let s = 'hello' + ' ' + 'world';\n"
                   "print(s);\n");
}

/* int + float: arithmetic coerces to float. */
OAK_TEST_DECL(MixedIntFloatAddRuns)
{
  return expect_ok("let x = 1 + 2.5;\n"
                   "print(x);\n");
}

/* float / int: arithmetic coerces to float. */
OAK_TEST_DECL(MixedFloatIntDivRuns)
{
  return expect_ok("let x = 7.0 / 2;\n"
                   "print(x);\n");
}

/* Float division by zero is also rejected at runtime in Oak (not IEEE inf). */
OAK_TEST_DECL(FloatDivisionByZeroRuntime)
{
  return expect_runtime_error("let x = 1.0 / 0.0;\n"
                              "print(x);\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(StringConcatRuns),
    OAK_TEST_ENTRY(MixedIntFloatAddRuns),
    OAK_TEST_ENTRY(MixedFloatIntDivRuns),
    OAK_TEST_ENTRY(FloatDivisionByZeroRuntime),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
