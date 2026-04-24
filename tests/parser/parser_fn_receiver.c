#include "oak_test_ast.h"

OAK_TEST_DECL(ParseFnReceiver)
{
  struct oak_lexer_result_t* lexer =
      OAK_LEX("fn Vec.push(val : number) { x = val; }");

  struct oak_parser_result_t result = {0};
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /*
     Expected shape:
       FN_DECL (binary: FN_PROTO, BLOCK)
         FN_PROTO
           FN_HEAD (binary: FN_PREFIX, IDENT("push"))
             FN_PREFIX -> FN_RECEIVER(Vec.) ...
           FN_PARAMS_AND_RET -> FN_PARAM_LIST ...
         BLOCK ...
  */

  const struct oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(decl, OAK_NODE_FN_DECL);
  OAK_CHECK_CHILD_COUNT(decl, 2);

  const struct oak_ast_node_t* proto = oak_test_ast_child(decl, 0);
  OAK_CHECK_NODE_KIND(proto, OAK_NODE_FN_PROTO);

  const struct oak_ast_node_t* head = oak_test_ast_child(proto, 0);
  OAK_CHECK_NODE_KIND(head, OAK_NODE_FN_HEAD);

  const struct oak_ast_node_t* pfx = oak_test_ast_child(head, 0);
  OAK_CHECK_NODE_KIND(pfx, OAK_NODE_FN_PREFIX);
  OAK_CHECK_CHILD_COUNT(pfx, 1);

  const struct oak_ast_node_t* receiver = oak_test_ast_child(pfx, 0);
  OAK_CHECK_NODE_KIND(receiver, OAK_NODE_FN_RECEIVER);
  OAK_CHECK_CHILD_COUNT(receiver, 1);
  const struct oak_ast_node_t* receiver_ident = oak_test_ast_child(receiver, 0);
  OAK_CHECK_NODE_KIND(receiver_ident, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(receiver_ident, "Vec");

  const struct oak_ast_node_t* name = oak_test_ast_child(head, 1);
  OAK_CHECK_NODE_KIND(name, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(name, "push");

  const struct oak_ast_node_t* params_tail = oak_test_ast_child(proto, 1);
  OAK_CHECK_NODE_KIND(params_tail, OAK_NODE_FN_PARAMS_AND_RET);
  OAK_CHECK_CHILD_COUNT(params_tail, 1);

  const struct oak_ast_node_t* plist = oak_test_ast_child(params_tail, 0);
  OAK_CHECK_NODE_KIND(plist, OAK_NODE_FN_PARAM_LIST);
  OAK_CHECK_CHILD_COUNT(plist, 1);

  const struct oak_ast_node_t* params = oak_test_ast_child(plist, 0);
  OAK_CHECK_NODE_KIND(params, OAK_NODE_FN_PARAMS);
  OAK_CHECK_CHILD_COUNT(params, 1);

  const struct oak_ast_node_t* param = oak_test_ast_child(params, 0);
  OAK_CHECK_NODE_KIND(param, OAK_NODE_FN_PARAM);
  OAK_CHECK_CHILD_COUNT(param, 2);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param, 0), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param, 0), "val");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param, 1), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param, 1), "number");

  const struct oak_ast_node_t* body = oak_test_ast_child(decl, 1);
  OAK_CHECK_NODE_KIND(body, OAK_NODE_BLOCK);
  OAK_CHECK_CHILD_COUNT(body, 1);

  const struct oak_ast_node_t* stmt = oak_test_ast_child(body, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_STMT_ASSIGNMENT);
  OAK_CHECK_CHILD_COUNT(stmt, 2);

  const struct oak_ast_node_t* stmt_lhs = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(stmt_lhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(stmt_lhs, "x");

  const struct oak_ast_node_t* stmt_rhs = oak_test_ast_child(stmt, 1);
  OAK_CHECK_NODE_KIND(stmt_rhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(stmt_rhs, "val");

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseFnReceiver)
