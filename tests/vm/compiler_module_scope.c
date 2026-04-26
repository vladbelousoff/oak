#include "oak_compiler.h"
#include "oak_count_of.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"

#include <string.h>

static enum oak_test_status_t expect_ok(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = { 0 };
  oak_compile(root, &cr);
  OAK_CHECK(cr.chunk != null);

  oak_compile_result_free(&cr);
  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

static enum oak_test_status_t expect_compile_error(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = { 0 };
  oak_compile(root, &cr);
  OAK_CHECK(cr.chunk == null);

  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(ModuleScopeNameNotReadableInFunction)
{
  return expect_compile_error(
      "let g = 1;\n"
      "fn f() -> number { return g; }\n");
}

OAK_TEST_DECL(ModuleScopeNameNotAssignableInFunction)
{
  return expect_compile_error(
      "let mut g = 1;\n"
      "fn f() { g = 2; }\n");
}

OAK_TEST_DECL(ModuleScopeNameNotReadableInMethod)
{
  return expect_compile_error(
      "record R { x : number;\n"
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
