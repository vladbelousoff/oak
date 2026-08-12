#include "oak_count_of.h"
#include "oak_test_pipeline.h"

OAK_TEST_DECL(AnonFnLetAssign)
{
  return expect_ok(
      "let double = fn(x: number) -> number { return x * 2; };\n"
      "print(double(5));\n");
}

OAK_TEST_DECL(AnonFnVoid)
{
  return expect_ok(
      "let greet = fn(x: number) { print(x); };\n"
      "greet(42);\n");
}

OAK_TEST_DECL(NamedFnAsValue)
{
  return expect_ok(
      "fn add(a: number, b: number) -> number { return a + b; }\n"
      "let f = add;\n"
      "print(f(1, 2));\n");
}

OAK_TEST_DECL(PassAnonFnAsArg)
{
  return expect_ok(
      "fn apply(f: fn(number) -> number, x: number) -> number {\n"
      "  return f(x);\n"
      "}\n"
      "let double = fn(x: number) -> number { return x * 2; };\n"
      "print(apply(double, 5));\n");
}

OAK_TEST_DECL(PassNamedFnAsArg)
{
  return expect_ok(
      "fn apply(f: fn(number) -> number, x: number) -> number {\n"
      "  return f(x);\n"
      "}\n"
      "fn triple(x: number) -> number { return x * 3; }\n"
      "print(apply(triple, 5));\n");
}

OAK_TEST_DECL(FnTypeInParam)
{
  return expect_ok(
      "fn run(f: fn(number, number) -> number, a: number, b: number) -> number {\n"
      "  return f(a, b);\n"
      "}\n"
      "fn sub(a: number, b: number) -> number { return a - b; }\n"
      "print(run(sub, 10, 3));\n");
}

OAK_TEST_DECL(AnonFnNoClosureError)
{
  return expect_compile_error(
      "fn outer() -> number {\n"
      "  let x = 10;\n"
      "  let f = fn() -> number { return x; };\n"
      "  return f();\n"
      "}\n"
      "print(outer());\n");
}

OAK_TEST_DECL(AnonFnArityMismatchRuntime)
{
  return expect_runtime_error(
      "fn apply(f: fn(number) -> number, x: number) -> number {\n"
      "  return f(x);\n"
      "}\n"
      "fn add(a: number, b: number) -> number { return a + b; }\n"
      "print(apply(add, 5));\n");
}

OAK_TEST_DECL(AnonFnImmediateCall)
{
  return expect_ok(
      "print(fn(x: number) -> number { return x + 1; }(41));\n");
}

OAK_TEST_DECL(AnonFnMultipleDecls)
{
  return expect_ok(
      "let inc = fn(x: number) -> number { return x + 1; };\n"
      "let dec = fn(x: number) -> number { return x - 1; };\n"
      "print(inc(10));\n"
      "print(dec(10));\n");
}

int main(void)
{
  const oak_test_t tests[] = {
    OAK_TEST_ENTRY(AnonFnLetAssign),
    OAK_TEST_ENTRY(AnonFnVoid),
    OAK_TEST_ENTRY(NamedFnAsValue),
    OAK_TEST_ENTRY(PassAnonFnAsArg),
    OAK_TEST_ENTRY(PassNamedFnAsArg),
    OAK_TEST_ENTRY(FnTypeInParam),
    OAK_TEST_ENTRY(AnonFnNoClosureError),
    OAK_TEST_ENTRY(AnonFnArityMismatchRuntime),
    OAK_TEST_ENTRY(AnonFnImmediateCall),
    OAK_TEST_ENTRY(AnonFnMultipleDecls),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
