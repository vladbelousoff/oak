#include "oak_test_ast.h"

OAK_TEST_DECL(ParseUnaryNeg)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize("-5;");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  /*
     Expected shape:
       STMT_EXPR
         UNARY_NEG
           INT(5)
  */

  const struct oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_KIND_STMT_EXPR);

  const struct oak_ast_node_t* neg = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(neg, OAK_NODE_KIND_UNARY_NEG);
  OAK_CHECK_NODE_KIND(neg->child, OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(neg->child, 5);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseUnaryNeg)
