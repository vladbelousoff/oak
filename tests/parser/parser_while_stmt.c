#include "oak_test_ast.h"

OAK_TEST_DECL(ParseWhileStmt)
{
  struct oak_lexer_result_t* lexer =
      oak_lexer_tokenize("while x < 10 { x = x + 1; }");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_STMT);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_STMT_WHILE);

  /*
     Expected shape:
       STMT_WHILE (binary: lhs=cond, rhs=block)
         BINARY_LESS
           IDENT("x")
           INT(10)
         BLOCK
           STMT_ASSIGNMENT
             IDENT("x")
             BINARY_ADD
               IDENT("x")
               INT(1)
  */

  OAK_CHECK_CHILD_COUNT(root, 2);

  const struct oak_ast_node_t* cond = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(cond, OAK_NODE_KIND_BINARY_LESS);

  const struct oak_ast_node_t* cond_lhs = oak_test_ast_child(cond, 0);
  OAK_CHECK_NODE_KIND(cond_lhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(cond_lhs, "x");

  const struct oak_ast_node_t* body = oak_test_ast_child(root, 1);
  OAK_CHECK_NODE_KIND(body, OAK_NODE_KIND_BLOCK);
  OAK_CHECK_CHILD_COUNT(body, 1);

  const struct oak_ast_node_t* body_stmt = oak_test_ast_child(body, 0);
  OAK_CHECK_NODE_KIND(body_stmt, OAK_NODE_KIND_STMT_ASSIGNMENT);

  const struct oak_ast_node_t* assign_lhs = oak_test_ast_child(body_stmt, 0);
  OAK_CHECK_NODE_KIND(assign_lhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(assign_lhs, "x");

  const struct oak_ast_node_t* assign_rhs = oak_test_ast_child(body_stmt, 1);
  OAK_CHECK_NODE_KIND(assign_rhs, OAK_NODE_KIND_BINARY_ADD);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseWhileStmt)
