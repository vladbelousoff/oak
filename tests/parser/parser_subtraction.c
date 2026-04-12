#include "oak_test_ast.h"

OAK_TEST_DECL(ParseSubtraction)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize("10 - 3;");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  /*
     Expected shape:
       STMT_EXPR
         BINARY_SUB
           INT(10)
           INT(3)
  */

  const struct oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_KIND_STMT_EXPR);

  const struct oak_ast_node_t* sub = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(sub, OAK_NODE_KIND_BINARY_SUB);
  OAK_CHECK_NODE_KIND(sub->lhs, OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(sub->lhs, 10);
  OAK_CHECK_NODE_KIND(sub->rhs, OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(sub->rhs, 3);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseSubtraction)
