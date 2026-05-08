#include "oak_test_ast.h"

/* `for v in arr { ... }` produces FOR_IN with [IDENT, EXPR, BLOCK]. */
OAK_TEST_DECL(ParseForInValue)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("for v in arr { }");

  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_STMT, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_STMT_FOR_IN);
  OAK_CHECK_CHILD_COUNT(root, 3);

  OAK_CHECK_NODE_KIND(oak_test_ast_child(root, 0), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(root, 0), "v");

  OAK_CHECK_NODE_KIND(oak_test_ast_child(root, 1), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(root, 1), "arr");

  OAK_CHECK_NODE_KIND(oak_test_ast_child(root, 2), OAK_NODE_BLOCK);

  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

/* `for i, v in arr { ... }` produces FOR_IN with [IDENT, IDENT, EXPR, BLOCK].
 */
OAK_TEST_DECL(ParseForInIndexValue)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("for i, v in arr { }");

  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_STMT, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_STMT_FOR_IN);
  OAK_CHECK_CHILD_COUNT(root, 4);

  OAK_CHECK_NODE_KIND(oak_test_ast_child(root, 0), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(root, 0), "i");

  OAK_CHECK_NODE_KIND(oak_test_ast_child(root, 1), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(root, 1), "v");

  OAK_CHECK_NODE_KIND(oak_test_ast_child(root, 2), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(root, 2), "arr");

  OAK_CHECK_NODE_KIND(oak_test_ast_child(root, 3), OAK_NODE_BLOCK);

  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(ParseForInValue),
    OAK_TEST_ENTRY(ParseForInIndexValue),
  };
  return oak_test_run(tests, (int)(sizeof(tests) / sizeof(tests[0])));
}
