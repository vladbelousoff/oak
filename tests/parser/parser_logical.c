#include "oak_test_ast.h"

OAK_TEST_DECL(ParseLogical)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("a && b || c;");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /* && binds tighter than ||:
       STMT_EXPR
         BINARY_OR
           BINARY_AND
             IDENT("a")
             IDENT("b")
           IDENT("c") */

  const struct oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_STMT_EXPR);

  const struct oak_ast_node_t* or_node = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(or_node, OAK_NODE_BINARY_OR);

  const struct oak_ast_node_t* and_node = or_node->lhs;
  OAK_CHECK_NODE_KIND(and_node, OAK_NODE_BINARY_AND);
  OAK_CHECK_NODE_KIND(and_node->lhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(and_node->lhs, "a");
  OAK_CHECK_NODE_KIND(and_node->rhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(and_node->rhs, "b");

  OAK_CHECK_NODE_KIND(or_node->rhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(or_node->rhs, "c");

  oak_parser_free(result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseLogical)
