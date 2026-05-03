#include "oak_test_pipeline.h"

OAK_TEST_DECL(FnArgArray)
{
  /* Pass a number[] to a function that indexes into it. */
  const char* source = "fn first(arr: number[]) -> number { return arr[0]; }\n"
                       "let a = [10, 20, 30];\n"
                       "print(first(a));";
  OAK_CHECK(expect_ok(source) == OAK_TEST_OK);

  /* Pass a mutable array and push into it from inside the function. */
  const char* source_mut = "fn append(mut arr: number[]) -> number {\n"
                           "  arr.push(99);\n"
                           "  return arr.size();\n"
                           "}\n"
                           "let mut b = [1, 2];\n"
                           "print(append(b));";
  OAK_CHECK(expect_ok(source_mut) == OAK_TEST_OK);

  /* Passing a plain number where number[] is expected must fail. */
  const char* bad = "fn first(arr: number[]) -> number { return arr[0]; }\n"
                    "print(first(42));";
  OAK_CHECK(expect_compile_error(bad) == OAK_TEST_OK);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(FnArgArray)
