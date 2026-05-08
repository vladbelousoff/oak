#include "oak_count_of.h"
#include "oak_test_pipeline.h"

OAK_TEST_DECL(ModuleScopeNameNotReadableInFunction)
{
  return expect_compile_error("let g = 1;\n"
                              "fn f() -> number { return g; }\n");
}

OAK_TEST_DECL(ModuleScopeNameNotAssignableInFunction)
{
  return expect_compile_error("let mut g = 1;\n"
                              "fn f() { g = 2; }\n");
}

OAK_TEST_DECL(ModuleScopeNameNotReadableInMethod)
{
  return expect_compile_error("record R { x : number;\n"
                              "  fn m(self) -> number { return g; }\n"
                              "}\n"
                              "let g = 1;\n");
}

OAK_TEST_DECL(LocalShadowsModuleScopeNameOk)
{
  return expect_ok("let g = 1;\n"
                   "fn f() -> number { let g = 2; return g; }\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(ModuleScopeNameNotReadableInFunction),
    OAK_TEST_ENTRY(ModuleScopeNameNotAssignableInFunction),
    OAK_TEST_ENTRY(ModuleScopeNameNotReadableInMethod),
    OAK_TEST_ENTRY(LocalShadowsModuleScopeNameOk),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
