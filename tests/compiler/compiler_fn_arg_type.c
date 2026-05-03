#include "oak_test_pipeline.h"

OAK_TEST_DECL(FnCallArgTypeMismatchFailsCompile)
{
  return expect_compile_error(
      "fn echo(s : string) -> number { return 1; }\n"
      "print(echo(42));");
}

OAK_TEST_MAIN(FnCallArgTypeMismatchFailsCompile)
