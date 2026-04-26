#include "oak_test_ast.h"

OAK_TEST_DECL(ParseRecordDecl)
{
  struct oak_lexer_result_t* lexer =
      OAK_LEX("record Point { x : number; y : number; }");

  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /*
     Expected shape:
       RECORD_DECL (binary: lhs = type name, rhs = field list)
         lhs: IDENT("Point")     -- TYPE_NAME choice
         rhs: RECORD_FIELDS
           RECORD_FIELD_DECL  (binary: lhs=IDENT, rhs=IDENT)
             lhs: IDENT("x")
             rhs: IDENT("number")
           RECORD_FIELD_DECL
             lhs: IDENT("y")
             rhs: IDENT("number")
  */

  const struct oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(decl, OAK_NODE_RECORD_DECL);
  OAK_CHECK_CHILD_COUNT(decl, 2);

  const struct oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  OAK_CHECK_NODE_KIND(name, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(name, "Point");

  const struct oak_ast_node_t* fields = oak_test_ast_child(decl, 1);
  OAK_CHECK_NODE_KIND(fields, OAK_NODE_RECORD_FIELDS);
  OAK_CHECK_CHILD_COUNT(fields, 2);

  const struct oak_ast_node_t* field0 = oak_test_ast_child(fields, 0);
  OAK_CHECK_NODE_KIND(field0, OAK_NODE_RECORD_FIELD_DECL);
  OAK_CHECK_NODE_KIND(field0->lhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(field0->lhs, "x");
  OAK_CHECK_NODE_KIND(field0->rhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(field0->rhs, "number");

  const struct oak_ast_node_t* field1 = oak_test_ast_child(fields, 1);
  OAK_CHECK_NODE_KIND(field1, OAK_NODE_RECORD_FIELD_DECL);
  OAK_CHECK_NODE_KIND(field1->lhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(field1->lhs, "y");
  OAK_CHECK_NODE_KIND(field1->rhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(field1->rhs, "number");

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseRecordDecl)
