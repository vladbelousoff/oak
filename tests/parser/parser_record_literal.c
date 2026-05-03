#include "oak_test_ast.h"

OAK_TEST_DECL(ParseRecordLiteral)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("new Point { x: 1, y: 2 }");

  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_EXPR_RECORD_LITERAL, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_EXPR_RECORD_LITERAL);

  /*
     Expected shape:
       EXPR_RECORD_LITERAL
         IDENT("Point")
         RECORD_LITERAL_FIELDS
           RECORD_LITERAL_FIELD [IDENT("x"), INT(1)]
           RECORD_LITERAL_FIELD [IDENT("y"), INT(2)]
  */
  OAK_CHECK_CHILD_COUNT(root, 2);

  const struct oak_ast_node_t* type_ident = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(type_ident, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(type_ident, "Point");

  const struct oak_ast_node_t* fields = oak_test_ast_child(root, 1);
  OAK_CHECK_NODE_KIND(fields, OAK_NODE_RECORD_LITERAL_FIELDS);
  OAK_CHECK_CHILD_COUNT(fields, 2);

  const struct oak_ast_node_t* f0 = oak_test_ast_child(fields, 0);
  OAK_CHECK_NODE_KIND(f0, OAK_NODE_RECORD_LITERAL_FIELD);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(f0, 0), "x");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(f0, 1), OAK_NODE_INT);
  OAK_CHECK_INT_VAL(oak_test_ast_child(f0, 1), 1);

  const struct oak_ast_node_t* f1 = oak_test_ast_child(fields, 1);
  OAK_CHECK_NODE_KIND(f1, OAK_NODE_RECORD_LITERAL_FIELD);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(f1, 0), "y");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(f1, 1), OAK_NODE_INT);
  OAK_CHECK_INT_VAL(oak_test_ast_child(f1, 1), 2);

  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseRecordLiteral)
