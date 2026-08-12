#include "oak_test_pipeline.h"

OAK_TEST_DECL(VoidFnOmittedArrowOk)
{
  return expect_ok("fn side() { return; }\n"
                   "fn main() { side(); }\n"
                   "main();\n");
}

OAK_TEST_DECL(ExplicitVoidReturnTypeRejected)
{
  return expect_compile_error("fn f() -> void { return; }\n");
}

OAK_TEST_DECL(VoidFnImplicitReturnAtEndOk)
{
  return expect_ok("fn noop() { }\n"
                   "noop();\n");
}

OAK_TEST_DECL(VoidFnCannotReturnValue)
{
  return expect_compile_error("fn bad() { return 1; }\n");
}

OAK_TEST_DECL(NonVoidFnMustReturnValue)
{
  return expect_compile_error("fn need() -> number { return; }\n");
}

OAK_TEST_DECL(CannotUseVoidInLet)
{
  return expect_compile_error("fn v() { }\n"
                              "let x = v();\n");
}

OAK_TEST_DECL(VoidCallAsStmtOk)
{
  return expect_ok("fn v() { }\n"
                   "v();\n");
}

OAK_TEST_DECL(PrintTreatedAsVoid)
{
  return expect_compile_error("let x = print(1);\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static oak_test_t tests[] = {
    OAK_TEST_ENTRY(VoidFnOmittedArrowOk),
    OAK_TEST_ENTRY(ExplicitVoidReturnTypeRejected),
    OAK_TEST_ENTRY(VoidFnImplicitReturnAtEndOk),
    OAK_TEST_ENTRY(VoidFnCannotReturnValue),
    OAK_TEST_ENTRY(NonVoidFnMustReturnValue),
    OAK_TEST_ENTRY(CannotUseVoidInLet),
    OAK_TEST_ENTRY(VoidCallAsStmtOk),
    OAK_TEST_ENTRY(PrintTreatedAsVoid),
  };
  return oak_test_run(tests, (int)(sizeof(tests) / sizeof(tests[0])));
}
