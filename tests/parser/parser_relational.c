#include "oak_test_ast.h"

OAK_TEST_DECL(ParseRelational)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("a < b;");

  struct oak_parser_result_t result = {0};
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /*
     Expected shape:
       STMT_EXPR
         BINARY_LESS
           IDENT("a")
           IDENT("b")
  */

  const struct oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_STMT_EXPR);

  const struct oak_ast_node_t* less = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(less, OAK_NODE_BINARY_LESS);
  OAK_CHECK_NODE_KIND(less->lhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(less->lhs, "a");
  OAK_CHECK_NODE_KIND(less->rhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(less->rhs, "b");

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseRelational)
