#include "oak_test_ast.h"

OAK_TEST_DECL(ParseExprEmptyCollections)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("[]; [:];");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  const oak_ast_node_t* stmt0 = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt0, OAK_NODE_KIND_STMT_EXPR);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(stmt0, 0), OAK_NODE_KIND_EXPR_EMPTY_ARRAY);

  const oak_ast_node_t* stmt1 = oak_test_ast_child(root, 1);
  OAK_CHECK_NODE_KIND(stmt1, OAK_NODE_KIND_STMT_EXPR);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(stmt1, 0), OAK_NODE_KIND_EXPR_EMPTY_MAP);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseExprEmptyCollections)
