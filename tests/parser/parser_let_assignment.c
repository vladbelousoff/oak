#include "oak_test_ast.h"

OAK_TEST_DECL(ParseLetAssignment)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("let y = 10;");

  struct oak_parser_result_t result = {0};
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /*
     Expected shape:
       STMT_LET_ASSIGNMENT
         STMT_ASSIGNMENT
           IDENT("y")
           INT(10)
  */

  const struct oak_ast_node_t* let_stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(let_stmt, OAK_NODE_STMT_LET_ASSIGNMENT);
  OAK_CHECK_CHILD_COUNT(let_stmt, 1);

  const struct oak_ast_node_t* assign = oak_test_ast_child(let_stmt, 0);
  OAK_CHECK_NODE_KIND(assign, OAK_NODE_STMT_ASSIGNMENT);
  OAK_CHECK_CHILD_COUNT(assign, 2);

  const struct oak_ast_node_t* ident = oak_test_ast_child(assign, 0);
  OAK_CHECK_NODE_KIND(ident, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(ident, "y");

  const struct oak_ast_node_t* val = oak_test_ast_child(assign, 1);
  OAK_CHECK_NODE_KIND(val, OAK_NODE_INT);
  OAK_CHECK_INT_VAL(val, 10);

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseLetAssignment)
