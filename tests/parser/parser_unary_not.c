#include "oak_test_ast.h"

OAK_TEST_DECL(ParseUnaryNot)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize("!flag;");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  /*
     Expected shape:
       STMT_EXPR
         UNARY_NOT
           IDENT("flag")
  */

  const struct oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_KIND_STMT_EXPR);

  const struct oak_ast_node_t* not_node = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(not_node, OAK_NODE_KIND_UNARY_NOT);
  OAK_CHECK_NODE_KIND(not_node->child, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(not_node->child, "flag");

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseUnaryNot)
