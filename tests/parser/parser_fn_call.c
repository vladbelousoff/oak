#include "oak_test_ast.h"

OAK_TEST_DECL(ParseFnCall)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize("foo(1, 2);");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  /*
     Expected shape:
       PROGRAM
         STMT_EXPR
           FN_CALL
             IDENT("foo")
             FN_CALL_ARG [INT(1)]
             FN_CALL_ARG [INT(2)]
  */

  const struct oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_KIND_STMT_EXPR);

  const struct oak_ast_node_t* call = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(call, OAK_NODE_KIND_FN_CALL);
  OAK_CHECK_CHILD_COUNT(call, 3);

  const struct oak_ast_node_t* callee = oak_test_ast_child(call, 0);
  OAK_CHECK_NODE_KIND(callee, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(callee, "foo");

  const struct oak_ast_node_t* arg0 = oak_test_ast_child(call, 1);
  OAK_CHECK_NODE_KIND(arg0, OAK_NODE_KIND_FN_CALL_ARG);
  OAK_CHECK_CHILD_COUNT(arg0, 1);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(arg0, 0), OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(oak_test_ast_child(arg0, 0), 1);

  const struct oak_ast_node_t* arg1 = oak_test_ast_child(call, 2);
  OAK_CHECK_NODE_KIND(arg1, OAK_NODE_KIND_FN_CALL_ARG);
  OAK_CHECK_CHILD_COUNT(arg1, 1);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(arg1, 0), OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(oak_test_ast_child(arg1, 0), 2);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseFnCall)
