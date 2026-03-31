#include "oak_test_ast.h"

OAK_TEST_DECL(ParseForStmt)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("for i from 0 to 10 { x = x + 1; }");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_STMT);
  const oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_STMT_FOR);

  /*
     Expected shape:
       STMT_FOR
         IDENT("i")
         INT(0)
         INT(10)
         STMT_ASSIGNMENT
           IDENT("x")
           BINARY_ADD
             IDENT("x")
             INT(1)
  */

  OAK_CHECK_CHILD_COUNT(root, 4);

  const oak_ast_node_t* ident = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(ident, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(ident, "i");

  const oak_ast_node_t* from_expr = oak_test_ast_child(root, 1);
  OAK_CHECK_NODE_KIND(from_expr, OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(from_expr, 0);

  const oak_ast_node_t* to_expr = oak_test_ast_child(root, 2);
  OAK_CHECK_NODE_KIND(to_expr, OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(to_expr, 10);

  const oak_ast_node_t* body_stmt = oak_test_ast_child(root, 3);
  OAK_CHECK_NODE_KIND(body_stmt, OAK_NODE_KIND_STMT_ASSIGNMENT);

  const oak_ast_node_t* assign_lhs = oak_test_ast_child(body_stmt, 0);
  OAK_CHECK_NODE_KIND(assign_lhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(assign_lhs, "x");

  const oak_ast_node_t* assign_rhs = oak_test_ast_child(body_stmt, 1);
  OAK_CHECK_NODE_KIND(assign_rhs, OAK_NODE_KIND_BINARY_ADD);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseForStmt)
