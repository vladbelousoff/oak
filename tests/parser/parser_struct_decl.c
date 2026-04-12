#include "oak_test_ast.h"

OAK_TEST_DECL(ParseStructDecl)
{
  struct oak_lexer_result_t* lexer =
      OAK_LEX("type Point struct { x : number; y : number; }");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  /*
     Expected shape:
       STRUCT_DECL
         TYPE_NAME("Point")
         STRUCT_FIELD_DECL  (binary: lhs=IDENT, rhs=IDENT)
           lhs: IDENT("x")
           rhs: IDENT("number")
         STRUCT_FIELD_DECL  (binary: lhs=IDENT, rhs=IDENT)
           lhs: IDENT("y")
           rhs: IDENT("number")
  */

  const struct oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(decl, OAK_NODE_KIND_STRUCT_DECL);
  OAK_CHECK_CHILD_COUNT(decl, 3);

  const struct oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  OAK_CHECK_NODE_KIND(name, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(name, "Point");

  const struct oak_ast_node_t* field0 = oak_test_ast_child(decl, 1);
  OAK_CHECK_NODE_KIND(field0, OAK_NODE_KIND_STRUCT_FIELD_DECL);
  OAK_CHECK_NODE_KIND(field0->lhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(field0->lhs, "x");
  OAK_CHECK_NODE_KIND(field0->rhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(field0->rhs, "number");

  const struct oak_ast_node_t* field1 = oak_test_ast_child(decl, 2);
  OAK_CHECK_NODE_KIND(field1, OAK_NODE_KIND_STRUCT_FIELD_DECL);
  OAK_CHECK_NODE_KIND(field1->lhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(field1->lhs, "y");
  OAK_CHECK_NODE_KIND(field1->rhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(field1->rhs, "number");

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseStructDecl)
