#include "oak_test_ast.h"

OAK_TEST_DECL(ParseEnumDecl)
{
  struct oak_lexer_result_t* lexer =
      OAK_LEX("type Color enum { Red, Green, Blue }");

  struct oak_parser_result_t result = {0};
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /*
     Expected shape:
       ENUM_DECL (binary: lhs = name, rhs = variant list)
         lhs: IDENT("Color")
         rhs: ENUM_VARIANTS
           IDENT("Red")
           IDENT("Green")
           IDENT("Blue")
  */

  const struct oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(decl, OAK_NODE_ENUM_DECL);
  OAK_CHECK_CHILD_COUNT(decl, 2);

  const struct oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  OAK_CHECK_NODE_KIND(name, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(name, "Color");

  const struct oak_ast_node_t* variants = oak_test_ast_child(decl, 1);
  OAK_CHECK_NODE_KIND(variants, OAK_NODE_ENUM_VARIANTS);
  OAK_CHECK_CHILD_COUNT(variants, 3);

  const struct oak_ast_node_t* field0 = oak_test_ast_child(variants, 0);
  OAK_CHECK_NODE_KIND(field0, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(field0, "Red");

  const struct oak_ast_node_t* field1 = oak_test_ast_child(variants, 1);
  OAK_CHECK_NODE_KIND(field1, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(field1, "Green");

  const struct oak_ast_node_t* field2 = oak_test_ast_child(variants, 2);
  OAK_CHECK_NODE_KIND(field2, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(field2, "Blue");

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseEnumDecl)
