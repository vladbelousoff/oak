#include "oak_test_ast.h"

/* `a.b.c` parses left-associatively as MEMBER_ACCESS(MEMBER_ACCESS(a, b), c). */
OAK_TEST_DECL(ParseFieldAccessChain)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("a.b.c;");

  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  const struct oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_STMT_EXPR);

  const struct oak_ast_node_t* outer = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(outer, OAK_NODE_MEMBER_ACCESS);
  OAK_CHECK_CHILD_COUNT(outer, 2);

  const struct oak_ast_node_t* inner = oak_test_ast_child(outer, 0);
  OAK_CHECK_NODE_KIND(inner, OAK_NODE_MEMBER_ACCESS);
  OAK_CHECK_CHILD_COUNT(inner, 2);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(inner, 0), "a");
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(inner, 1), "b");

  OAK_CHECK_TOKEN_STR(oak_test_ast_child(outer, 1), "c");

  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseFieldAccessChain)
