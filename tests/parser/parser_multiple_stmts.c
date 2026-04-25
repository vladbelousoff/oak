#include "oak_test_ast.h"

OAK_TEST_DECL(ParseMultipleStmts)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("let x = 1; y = x + 2;");

  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);
  OAK_CHECK_CHILD_COUNT(root, 2);

  /*
     Expected shape:
       PROGRAM
         STMT_LET_ASSIGNMENT
           STMT_ASSIGNMENT
             IDENT("x")
             INT(1)
         STMT_ASSIGNMENT
           IDENT("y")
           BINARY_ADD
             IDENT("x")
             INT(2)
  */

  const struct oak_ast_node_t* let_stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(let_stmt, OAK_NODE_STMT_LET_ASSIGNMENT);

  const struct oak_ast_node_t* assign1 = oak_test_ast_child(let_stmt, 0);
  OAK_CHECK_NODE_KIND(assign1, OAK_NODE_STMT_ASSIGNMENT);

  const struct oak_ast_node_t* x_ident = oak_test_ast_child(assign1, 0);
  OAK_CHECK_NODE_KIND(x_ident, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(x_ident, "x");

  const struct oak_ast_node_t* one = oak_test_ast_child(assign1, 1);
  OAK_CHECK_NODE_KIND(one, OAK_NODE_INT);
  OAK_CHECK_INT_VAL(one, 1);

  const struct oak_ast_node_t* assign2 = oak_test_ast_child(root, 1);
  OAK_CHECK_NODE_KIND(assign2, OAK_NODE_STMT_ASSIGNMENT);

  const struct oak_ast_node_t* y_ident = oak_test_ast_child(assign2, 0);
  OAK_CHECK_NODE_KIND(y_ident, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(y_ident, "y");

  const struct oak_ast_node_t* add = oak_test_ast_child(assign2, 1);
  OAK_CHECK_NODE_KIND(add, OAK_NODE_BINARY_ADD);
  OAK_CHECK_NODE_KIND(add->lhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(add->lhs, "x");
  OAK_CHECK_NODE_KIND(add->rhs, OAK_NODE_INT);
  OAK_CHECK_INT_VAL(add->rhs, 2);

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseMultipleStmts)
