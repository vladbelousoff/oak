#include "oak_test_ast.h"

OAK_TEST_DECL(ParseBoolLiteral)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("true; false;");

  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /*
     Expected shape:
       PROGRAM
         STMT_EXPR
           TRUE
         STMT_EXPR
           FALSE
  */

  const struct oak_ast_node_t* stmt_true = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt_true, OAK_NODE_STMT_EXPR);
  const struct oak_ast_node_t* true_node = oak_test_ast_child(stmt_true, 0);
  OAK_CHECK_NODE_KIND(true_node, OAK_NODE_TRUE);

  const struct oak_ast_node_t* stmt_false = oak_test_ast_child(root, 1);
  OAK_CHECK_NODE_KIND(stmt_false, OAK_NODE_STMT_EXPR);
  const struct oak_ast_node_t* false_node = oak_test_ast_child(stmt_false, 0);
  OAK_CHECK_NODE_KIND(false_node, OAK_NODE_FALSE);

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseBoolLiteral)
