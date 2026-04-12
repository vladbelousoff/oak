#include "oak_test_ast.h"

OAK_TEST_DECL(ParseFnReceiver)
{
  struct oak_lexer_result_t* lexer =
      oak_lexer_tokenize("fn Vec.push(val : int) { x = val; }");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  /*
     Expected shape:
       FN_DECL
         FN_RECEIVER
           IDENT("Vec")
         IDENT("push")
         FN_PARAM [IDENT("val"), IDENT("int")]
         BLOCK
           STMT_ASSIGNMENT
             IDENT("x")
             IDENT("val")
  */

  const struct oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(decl, OAK_NODE_KIND_FN_DECL);
  OAK_CHECK_CHILD_COUNT(decl, 4);

  const struct oak_ast_node_t* receiver = oak_test_ast_child(decl, 0);
  OAK_CHECK_NODE_KIND(receiver, OAK_NODE_KIND_FN_RECEIVER);
  OAK_CHECK_CHILD_COUNT(receiver, 1);
  const struct oak_ast_node_t* receiver_ident = oak_test_ast_child(receiver, 0);
  OAK_CHECK_NODE_KIND(receiver_ident, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(receiver_ident, "Vec");

  const struct oak_ast_node_t* name = oak_test_ast_child(decl, 1);
  OAK_CHECK_NODE_KIND(name, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(name, "push");

  const struct oak_ast_node_t* param = oak_test_ast_child(decl, 2);
  OAK_CHECK_NODE_KIND(param, OAK_NODE_KIND_FN_PARAM);
  OAK_CHECK_CHILD_COUNT(param, 2);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param, 0), OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param, 0), "val");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param, 1), OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param, 1), "int");

  const struct oak_ast_node_t* body = oak_test_ast_child(decl, 3);
  OAK_CHECK_NODE_KIND(body, OAK_NODE_KIND_BLOCK);
  OAK_CHECK_CHILD_COUNT(body, 1);

  const struct oak_ast_node_t* stmt = oak_test_ast_child(body, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_KIND_STMT_ASSIGNMENT);
  OAK_CHECK_CHILD_COUNT(stmt, 2);

  const struct oak_ast_node_t* stmt_lhs = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(stmt_lhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(stmt_lhs, "x");

  const struct oak_ast_node_t* stmt_rhs = oak_test_ast_child(stmt, 1);
  OAK_CHECK_NODE_KIND(stmt_rhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(stmt_rhs, "val");

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseFnReceiver)
