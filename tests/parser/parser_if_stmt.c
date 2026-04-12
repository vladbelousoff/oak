#include "oak_test_ast.h"

OAK_TEST_DECL(ParseIfStmt)
{
  struct oak_lexer_result_t* lexer =
      oak_lexer_tokenize("if x == 1 { y = 2; } else { y = 3; }");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_STMT);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_STMT_IF);

  /*
     Expected shape:
       STMT_IF
         BINARY_EQ
           IDENT("x")
           INT(1)
         BLOCK
           STMT_ASSIGNMENT [IDENT("y"), INT(2)]
         ELSE_BLOCK
           BLOCK
             STMT_ASSIGNMENT [IDENT("y"), INT(3)]
  */

  OAK_CHECK_CHILD_COUNT(root, 3);

  const struct oak_ast_node_t* cond = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(cond, OAK_NODE_KIND_BINARY_EQ);

  const struct oak_ast_node_t* cond_lhs = oak_test_ast_child(cond, 0);
  OAK_CHECK_NODE_KIND(cond_lhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(cond_lhs, "x");

  const struct oak_ast_node_t* then_block = oak_test_ast_child(root, 1);
  OAK_CHECK_NODE_KIND(then_block, OAK_NODE_KIND_BLOCK);
  OAK_CHECK_CHILD_COUNT(then_block, 1);
  const struct oak_ast_node_t* then_stmt = oak_test_ast_child(then_block, 0);
  OAK_CHECK_NODE_KIND(then_stmt, OAK_NODE_KIND_STMT_ASSIGNMENT);

  const struct oak_ast_node_t* else_node = oak_test_ast_child(root, 2);
  OAK_CHECK_NODE_KIND(else_node, OAK_NODE_KIND_ELSE_BLOCK);
  OAK_CHECK_CHILD_COUNT(else_node, 1);

  const struct oak_ast_node_t* else_block = oak_test_ast_child(else_node, 0);
  OAK_CHECK_NODE_KIND(else_block, OAK_NODE_KIND_BLOCK);
  OAK_CHECK_CHILD_COUNT(else_block, 1);
  const struct oak_ast_node_t* else_stmt = oak_test_ast_child(else_block, 0);
  OAK_CHECK_NODE_KIND(else_stmt, OAK_NODE_KIND_STMT_ASSIGNMENT);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseIfStmt)
