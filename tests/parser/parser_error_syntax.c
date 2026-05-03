#include "oak_test_ast.h"

/* Each invalid program either yields a null root from oak_parser_root or
 * leaves the parser with a non-zero error count. */
static enum oak_test_status_t expect_parse_error(const char* source)
{
  struct oak_lexer_result_t* lexer = OAK_LEX(source);
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);

  const struct oak_ast_node_t* root = oak_parser_root(&result);
  const int errs = oak_parser_error_count(&result);
  const enum oak_test_status_t ok = (root == null || errs > 0)
                                        ? OAK_TEST_OK
                                        : OAK_TEST_FAIL;

  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return ok;
}

/* let-binding without an initializer is invalid. */
OAK_TEST_DECL(ParseErrorLetMissingInit)
{
  return expect_parse_error("let x;");
}

/* Assignment without a value is invalid. */
OAK_TEST_DECL(ParseErrorAssignNoRhs)
{
  return expect_parse_error("x = ;");
}

/* Unmatched opening brace inside a function body is invalid. */
OAK_TEST_DECL(ParseErrorUnmatchedBrace)
{
  return expect_parse_error("fn f() -> number { return 1;");
}

/* `record` without a body is invalid. */
OAK_TEST_DECL(ParseErrorRecordNoBody)
{
  return expect_parse_error("record Foo;");
}

/* A trailing operator with no rhs is invalid. */
OAK_TEST_DECL(ParseErrorBinaryNoRhs)
{
  return expect_parse_error("let x = 1 + ;");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(ParseErrorLetMissingInit),
    OAK_TEST_ENTRY(ParseErrorAssignNoRhs),
    OAK_TEST_ENTRY(ParseErrorUnmatchedBrace),
    OAK_TEST_ENTRY(ParseErrorRecordNoBody),
    OAK_TEST_ENTRY(ParseErrorBinaryNoRhs),
  };
  return oak_test_run(tests, (int)(sizeof(tests) / sizeof(tests[0])));
}
