#include "oak_test_ast.h"

OAK_TEST_DECL(ParseTypeArray)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("fn items() -> number[] { }");

  struct oak_parser_result_t result = {0};
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /*
     Expected shape:
       PROGRAM
         FN_DECL (binary: FN_PROTO, BLOCK)
           FN_PROTO -> FN_HEAD, FN_PARAMS_AND_RET( plist, FN_RETURN_TYPE -> TYPE_ARRAY )
  */

  const struct oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(decl, OAK_NODE_FN_DECL);
  OAK_CHECK_CHILD_COUNT(decl, 2);

  const struct oak_ast_node_t* proto = oak_test_ast_child(decl, 0);
  OAK_CHECK_NODE_KIND(proto, OAK_NODE_FN_PROTO);

  const struct oak_ast_node_t* head = oak_test_ast_child(proto, 0);
  OAK_CHECK_NODE_KIND(head, OAK_NODE_FN_HEAD);

  const struct oak_ast_node_t* name = oak_test_ast_child(head, 1);
  OAK_CHECK_NODE_KIND(name, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(name, "items");

  const struct oak_ast_node_t* params_tail = oak_test_ast_child(proto, 1);
  OAK_CHECK_NODE_KIND(params_tail, OAK_NODE_FN_PARAMS_AND_RET);
  OAK_CHECK_CHILD_COUNT(params_tail, 2);

  const struct oak_ast_node_t* plist = oak_test_ast_child(params_tail, 0);
  OAK_CHECK_NODE_KIND(plist, OAK_NODE_FN_PARAM_LIST);
  OAK_CHECK_CHILD_COUNT(plist, 1);

  const struct oak_ast_node_t* params = oak_test_ast_child(plist, 0);
  OAK_CHECK_NODE_KIND(params, OAK_NODE_FN_PARAMS);
  OAK_CHECK_CHILD_COUNT(params, 0);

  const struct oak_ast_node_t* ret_wrap = oak_test_ast_child(params_tail, 1);
  OAK_CHECK_NODE_KIND(ret_wrap, OAK_NODE_FN_RETURN_TYPE);
  OAK_CHECK_CHILD_COUNT(ret_wrap, 1);

  const struct oak_ast_node_t* ret_type = oak_test_ast_child(ret_wrap, 0);
  OAK_CHECK_NODE_KIND(ret_type, OAK_NODE_TYPE_ARRAY);
  OAK_CHECK_CHILD_COUNT(ret_type, 1);

  const struct oak_ast_node_t* elem = oak_test_ast_child(ret_type, 0);
  OAK_CHECK_NODE_KIND(elem, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(elem, "number");

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseTypeArray)
