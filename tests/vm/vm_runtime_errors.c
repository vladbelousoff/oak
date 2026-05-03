#include "oak_count_of.h"
#include "oak_test_pipeline.h"

/* Out-of-bounds array index trips the bounds check at runtime. */
OAK_TEST_DECL(ArrayIndexOutOfBoundsRuntime)
{
  return expect_runtime_error("let arr = [1, 2, 3];\n"
                              "print(arr[100]);\n");
}

/* Negative array indices are also rejected at runtime. */
OAK_TEST_DECL(ArrayNegativeIndexRuntime)
{
  return expect_runtime_error("let arr = [1, 2, 3];\n"
                              "print(arr[-1]);\n");
}

/* Reading a missing map key is a runtime error. */
OAK_TEST_DECL(MapMissingKeyRuntime)
{
  return expect_runtime_error(
      "let mut m = [:] as [string:number];\n"
      "m['a'] = 1;\n"
      "print(m['nope']);\n");
}

/* Integer division by zero is a runtime error. */
OAK_TEST_DECL(IntDivisionByZeroRuntime)
{
  return expect_runtime_error("let x = 1 / 0;\n"
                              "print(x);\n");
}

/* Integer modulo by zero is a runtime error. */
OAK_TEST_DECL(IntModuloByZeroRuntime)
{
  return expect_runtime_error("let x = 7 % 0;\n"
                              "print(x);\n");
}

/* Unbounded recursion exhausts the call-frame stack. */
OAK_TEST_DECL(CallFrameOverflowRuntime)
{
  return expect_runtime_error(
      "fn loop() -> number { return loop() + 1; }\n"
      "print(loop());\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(ArrayIndexOutOfBoundsRuntime),
    OAK_TEST_ENTRY(ArrayNegativeIndexRuntime),
    OAK_TEST_ENTRY(MapMissingKeyRuntime),
    OAK_TEST_ENTRY(IntDivisionByZeroRuntime),
    OAK_TEST_ENTRY(IntModuloByZeroRuntime),
    OAK_TEST_ENTRY(CallFrameOverflowRuntime),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
