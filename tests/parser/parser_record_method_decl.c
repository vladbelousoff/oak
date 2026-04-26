#include "oak_test_ast.h"

OAK_TEST_DECL(ParseRecordWithInstanceMethod)
{
  struct oak_lexer_result_t* lexer = OAK_LEX(
      "record Point {\n"
      "  x : number;\n"
      "  fn dist(self) -> number { return self.x; }\n"
      "}");

  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  const struct oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(decl, OAK_NODE_RECORD_DECL);
  OAK_CHECK_CHILD_COUNT(decl, 2);

  const struct oak_ast_node_t* fields = oak_test_ast_child(decl, 1);
  OAK_CHECK_NODE_KIND(fields, OAK_NODE_RECORD_FIELDS);
  OAK_CHECK_CHILD_COUNT(fields, 2);

  const struct oak_ast_node_t* field0 = oak_test_ast_child(fields, 0);
  OAK_CHECK_NODE_KIND(field0, OAK_NODE_RECORD_FIELD_DECL);

  const struct oak_ast_node_t* method = oak_test_ast_child(fields, 1);
  OAK_CHECK_NODE_KIND(method, OAK_NODE_FN_DECL);
  OAK_CHECK_CHILD_COUNT(method, 2);

  const struct oak_ast_node_t* proto = oak_test_ast_child(method, 0);
  OAK_CHECK_NODE_KIND(proto, OAK_NODE_FN_PROTO);
  const struct oak_ast_node_t* head = oak_test_ast_child(proto, 0);
  const struct oak_ast_node_t* pfx = oak_test_ast_child(head, 0);
  OAK_CHECK_NODE_KIND(pfx, OAK_NODE_FN_PREFIX);
  OAK_CHECK_CHILD_COUNT(pfx, 0);

  const struct oak_ast_node_t* name = oak_test_ast_child(head, 1);
  OAK_CHECK_NODE_KIND(name, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(name, "dist");

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseRecordWithInstanceMethod)
