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
  return expect_runtime_error("let mut m = [:] as [string:number];\n"
                              "m['a'] = 1;\n"
                              "print(m['nope']);\n");
}

/* Number division by zero is a runtime error. */
OAK_TEST_DECL(NumberDivisionByZeroRuntime)
{
  return expect_runtime_error("let x = 1 / 0;\n"
                              "print(x);\n");
}

/* Integer division by zero through intdiv is a runtime error. */
OAK_TEST_DECL(IntDivByZeroRuntime)
{
  return expect_runtime_error("let x = intdiv(1, 0);\n"
                              "print(x);\n");
}

/* Numeric conversions reject non-number operands. */
OAK_TEST_DECL(NumberConversionTypeRuntime)
{
  return expect_runtime_error("print(to_int('x'));\n");
}

/* Math helpers reject invalid operands. */
OAK_TEST_DECL(MathBuiltinRuntime)
{
  OAK_CHECK(expect_runtime_error("print(sqrt(-1));\n") == OAK_TEST_OK);
  OAK_CHECK(expect_runtime_error("print(sin('x'));\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
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
  return expect_runtime_error("fn loop() -> number { return loop() + 1; }\n"
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
    OAK_TEST_ENTRY(NumberDivisionByZeroRuntime),
    OAK_TEST_ENTRY(IntDivByZeroRuntime),
    OAK_TEST_ENTRY(NumberConversionTypeRuntime),
    OAK_TEST_ENTRY(MathBuiltinRuntime),
    OAK_TEST_ENTRY(IntModuloByZeroRuntime),
    OAK_TEST_ENTRY(CallFrameOverflowRuntime),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
