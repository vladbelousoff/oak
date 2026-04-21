#include "oak_test_ast.h"

OAK_TEST_DECL(ParseFnParamMut)
{
  struct oak_lexer_result_t* lexer =
      OAK_LEX("fn foo(mut x : number, y : number) -> number { x = 1; }");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /*
     Expected shape:
       FN_DECL (binary: FN_PROTO, BLOCK); FN_PROTO carries FN_HEAD + FN_PARAMS_AND_RET
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
  OAK_CHECK_TOKEN_STR(name, "foo");

  const struct oak_ast_node_t* params_tail = oak_test_ast_child(proto, 1);
  OAK_CHECK_NODE_KIND(params_tail, OAK_NODE_FN_PARAMS_AND_RET);
  OAK_CHECK_CHILD_COUNT(params_tail, 2);

  const struct oak_ast_node_t* plist = oak_test_ast_child(params_tail, 0);
  OAK_CHECK_NODE_KIND(plist, OAK_NODE_FN_PARAM_LIST);
  OAK_CHECK_CHILD_COUNT(plist, 2);

  const struct oak_ast_node_t* param0 = oak_test_ast_child(plist, 0);
  OAK_CHECK_NODE_KIND(param0, OAK_NODE_FN_PARAM);
  OAK_CHECK_CHILD_COUNT(param0, 3);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param0, 0), OAK_NODE_MUT_KEYWORD);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param0, 1), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param0, 1), "x");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param0, 2), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param0, 2), "number");

  const struct oak_ast_node_t* param1 = oak_test_ast_child(plist, 1);
  OAK_CHECK_NODE_KIND(param1, OAK_NODE_FN_PARAM);
  OAK_CHECK_CHILD_COUNT(param1, 2);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param1, 0), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param1, 0), "y");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param1, 1), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param1, 1), "number");

  const struct oak_ast_node_t* ret_wrap = oak_test_ast_child(params_tail, 1);
  OAK_CHECK_NODE_KIND(ret_wrap, OAK_NODE_FN_RETURN_TYPE);
  OAK_CHECK_CHILD_COUNT(ret_wrap, 1);
  const struct oak_ast_node_t* ret_type = oak_test_ast_child(ret_wrap, 0);
  OAK_CHECK_NODE_KIND(ret_type, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(ret_type, "number");

  const struct oak_ast_node_t* body = oak_test_ast_child(decl, 1);
  OAK_CHECK_NODE_KIND(body, OAK_NODE_BLOCK);
  OAK_CHECK_CHILD_COUNT(body, 1);
  const struct oak_ast_node_t* stmt = oak_test_ast_child(body, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_STMT_ASSIGNMENT);

  oak_parser_free(result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseFnParamMut)
