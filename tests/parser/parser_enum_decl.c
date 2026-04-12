#include "oak_test_ast.h"

OAK_TEST_DECL(ParseEnumDecl)
{
  struct oak_lexer_result_t* lexer =
      oak_lexer_tokenize("type Color enum { Red Green Blue }");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  /*
     Expected shape:
       ENUM_DECL
         IDENT("Color")
         IDENT("Red")
         IDENT("Green")
         IDENT("Blue")
  */

  const struct oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(decl, OAK_NODE_KIND_ENUM_DECL);
  OAK_CHECK_CHILD_COUNT(decl, 4);

  const struct oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  OAK_CHECK_NODE_KIND(name, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(name, "Color");

  const struct oak_ast_node_t* field0 = oak_test_ast_child(decl, 1);
  OAK_CHECK_NODE_KIND(field0, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(field0, "Red");

  const struct oak_ast_node_t* field1 = oak_test_ast_child(decl, 2);
  OAK_CHECK_NODE_KIND(field1, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(field1, "Green");

  const struct oak_ast_node_t* field2 = oak_test_ast_child(decl, 3);
  OAK_CHECK_NODE_KIND(field2, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(field2, "Blue");

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseEnumDecl)
