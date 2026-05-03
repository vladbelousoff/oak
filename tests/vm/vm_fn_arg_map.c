#include "oak_test_pipeline.h"

OAK_TEST_DECL(FnArgMap)
{
  /* Pass a [string:number] map to a function that looks up a key. */
  const char* source =
      "fn get_x(m: [string:number]) -> number { return m['x']; }\n"
      "let m = ['x': 42, 'y': 7];\n"
      "print(get_x(m));";
  OAK_CHECK(expect_ok(source) == OAK_TEST_OK);

  /* Pass a mutable map and insert into it from inside the function. */
  const char* source_mut = "fn insert(mut m: [string:number]) -> number {\n"
                           "  m['z'] = 100;\n"
                           "  return m['z'];\n"
                           "}\n"
                           "let mut scores = ['a': 1];\n"
                           "print(insert(scores));";
  OAK_CHECK(expect_ok(source_mut) == OAK_TEST_OK);

  /* Passing a number[] where [string:number] is expected must fail. */
  const char* bad =
      "fn get_x(m: [string:number]) -> number { return m['x']; }\n"
      "let a = [1, 2, 3];\n"
      "print(get_x(a));";
  OAK_CHECK(expect_compile_error(bad) == OAK_TEST_OK);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(FnArgMap)
