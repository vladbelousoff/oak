#include "oak_test_ast.h"

OAK_TEST_DECL(ParseComparison)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize("a == b;");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  /*
     Expected shape:
       STMT_EXPR
         BINARY_EQ
           IDENT("a")
           IDENT("b")
  */

  const struct oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_KIND_STMT_EXPR);

  const struct oak_ast_node_t* eq = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(eq, OAK_NODE_KIND_BINARY_EQ);
  OAK_CHECK_NODE_KIND(eq->lhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(eq->lhs, "a");
  OAK_CHECK_NODE_KIND(eq->rhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(eq->rhs, "b");

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseComparison)
