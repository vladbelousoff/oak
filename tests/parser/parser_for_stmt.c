#include "oak_test_ast.h"

OAK_TEST_DECL(ParseForStmt)
{
  struct oak_lexer_result_t* lexer =
      OAK_LEX("for i from 0 to 10 { x = x + 1; }");

  struct oak_parser_result_t result = {0};
  oak_parse(lexer, OAK_NODE_STMT, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_STMT_FOR_FROM);

  /*
     Expected shape:
       STMT_FOR_FROM
         IDENT("i")
         INT(0)
         INT(10)
         BLOCK
           STMT_ASSIGNMENT
             IDENT("x")
             BINARY_ADD
               IDENT("x")
               INT(1)
  */

  OAK_CHECK_CHILD_COUNT(root, 4);

  const struct oak_ast_node_t* ident = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(ident, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(ident, "i");

  const struct oak_ast_node_t* from_expr = oak_test_ast_child(root, 1);
  OAK_CHECK_NODE_KIND(from_expr, OAK_NODE_INT);
  OAK_CHECK_INT_VAL(from_expr, 0);

  const struct oak_ast_node_t* to_expr = oak_test_ast_child(root, 2);
  OAK_CHECK_NODE_KIND(to_expr, OAK_NODE_INT);
  OAK_CHECK_INT_VAL(to_expr, 10);

  const struct oak_ast_node_t* body = oak_test_ast_child(root, 3);
  OAK_CHECK_NODE_KIND(body, OAK_NODE_BLOCK);
  OAK_CHECK_CHILD_COUNT(body, 1);

  const struct oak_ast_node_t* body_stmt = oak_test_ast_child(body, 0);
  OAK_CHECK_NODE_KIND(body_stmt, OAK_NODE_STMT_ASSIGNMENT);

  const struct oak_ast_node_t* assign_lhs = oak_test_ast_child(body_stmt, 0);
  OAK_CHECK_NODE_KIND(assign_lhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(assign_lhs, "x");

  const struct oak_ast_node_t* assign_rhs = oak_test_ast_child(body_stmt, 1);
  OAK_CHECK_NODE_KIND(assign_rhs, OAK_NODE_BINARY_ADD);

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseForStmt)
