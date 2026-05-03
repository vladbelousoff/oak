#include "oak_test_ast.h"

OAK_TEST_DECL(ParseStringLiteral)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("'hello world'");

  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_EXPR, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  /* The EXPR may collapse straight to STRING via PRIMARY. Walk down until we
   * find the leaf of kind STRING. */
  const struct oak_ast_node_t* node = root;
  while (node && node->kind != OAK_NODE_STRING)
  {
    if (oak_ast_node_child_count(node) == 0)
      break;
    node = oak_test_ast_child(node, 0);
  }
  OAK_CHECK_NODE_KIND(node, OAK_NODE_STRING);
  OAK_CHECK_TOKEN_STR(node, "hello world");

  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseStringLiteral)
