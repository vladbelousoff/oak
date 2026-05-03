#include "oak_test_pipeline.h"

OAK_TEST_DECL(UserFnCall)
{
  return expect_ok("fn add(a : number, b : number) -> number { return a + b; }\n"
                   "print(add(1, 2));");
}

OAK_TEST_MAIN(UserFnCall)
