#include "oak_count_of.h"
#include "oak_test_pipeline.h"

/* =========================================================================
 * Names: undefined / duplicate / immutable
 * ========================================================================= */

OAK_TEST_DECL(UndefinedVariableInExpressionRejected)
{
  return expect_compile_error("let x = y + 1;\n");
}

OAK_TEST_DECL(UndefinedVariableInAssignmentRejected)
{
  return expect_parse_or_compile_error("y = 5;\n");
}

OAK_TEST_DECL(UndefinedFunctionCallRejected)
{
  return expect_compile_error("print(does_not_exist(1, 2));\n");
}

OAK_TEST_DECL(AssignToImmutableBindingRejected)
{
  return expect_compile_error("let x = 1;\n"
                              "x = 2;\n");
}

/* =========================================================================
 * Function calls: arity / argument types
 * ========================================================================= */

OAK_TEST_DECL(FnCallTooFewArgsRejected)
{
  return expect_compile_error(
      "fn add(a : number, b : number) -> number { return a + b; }\n"
      "print(add(1));\n");
}

OAK_TEST_DECL(FnCallTooManyArgsRejected)
{
  return expect_compile_error(
      "fn add(a : number, b : number) -> number { return a + b; }\n"
      "print(add(1, 2, 3));\n");
}

/* =========================================================================
 * Type lookups
 * ========================================================================= */

OAK_TEST_DECL(UnknownRecordTypeRejected)
{
  return expect_parse_or_compile_error("let p = new Missing { x : 1 };\n");
}

OAK_TEST_DECL(UnknownFieldOnRecordRejected)
{
  return expect_compile_error("record Point { x : number; y : number; }\n"
                              "let p = new Point { x : 1, y : 2 };\n"
                              "print(p.z);\n");
}

OAK_TEST_DECL(UnknownMethodOnRecordRejected)
{
  return expect_compile_error("record Point { x : number; y : number; }\n"
                              "let p = new Point { x : 1, y : 2 };\n"
                              "p.flip();\n");
}

/* =========================================================================
 * Control-flow placement
 * ========================================================================= */

OAK_TEST_DECL(BreakOutsideLoopRejected)
{
  return expect_compile_error("break;\n");
}

OAK_TEST_DECL(ContinueOutsideLoopRejected)
{
  return expect_compile_error("continue;\n");
}

OAK_TEST_DECL(ReturnAtModuleScopeRejected)
{
  return expect_parse_or_compile_error("return 1;\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(UndefinedVariableInExpressionRejected),
    OAK_TEST_ENTRY(UndefinedVariableInAssignmentRejected),
    OAK_TEST_ENTRY(UndefinedFunctionCallRejected),
    OAK_TEST_ENTRY(AssignToImmutableBindingRejected),
    OAK_TEST_ENTRY(FnCallTooFewArgsRejected),
    OAK_TEST_ENTRY(FnCallTooManyArgsRejected),
    OAK_TEST_ENTRY(UnknownRecordTypeRejected),
    OAK_TEST_ENTRY(UnknownFieldOnRecordRejected),
    OAK_TEST_ENTRY(UnknownMethodOnRecordRejected),
    OAK_TEST_ENTRY(BreakOutsideLoopRejected),
    OAK_TEST_ENTRY(ContinueOutsideLoopRejected),
    OAK_TEST_ENTRY(ReturnAtModuleScopeRejected),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
