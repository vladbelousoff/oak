#include "oak_test_ast.h"

OAK_TEST_DECL(ParseAssignment)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("x = 5;");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /*
     Expected shape:
       STMT_ASSIGNMENT
         IDENT("x")
         INT(5)
  */

  const struct oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_STMT_ASSIGNMENT);
  OAK_CHECK_CHILD_COUNT(stmt, 2);

  const struct oak_ast_node_t* ident = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(ident, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(ident, "x");

  const struct oak_ast_node_t* val = oak_test_ast_child(stmt, 1);
  OAK_CHECK_NODE_KIND(val, OAK_NODE_INT);
  OAK_CHECK_INT_VAL(val, 5);

  oak_parser_free(result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseAssignment)
