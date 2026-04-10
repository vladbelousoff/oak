#include "oak_test_ast.h"

OAK_TEST_DECL(ParseFnCallExpr)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("1 + foo(2) * 3;");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  /*
     Function call binds tighter than * and +:
       BINARY_ADD
         INT(1)
         BINARY_MUL
           FN_CALL
             IDENT("foo")
             FN_CALL_ARG [INT(2)]
           INT(3)
  */

  const oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_KIND_STMT_EXPR);

  const oak_ast_node_t* add = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(add, OAK_NODE_KIND_BINARY_ADD);
  OAK_CHECK_NODE_KIND(add->lhs, OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(add->lhs, 1);

  const oak_ast_node_t* mul = add->rhs;
  OAK_CHECK_NODE_KIND(mul, OAK_NODE_KIND_BINARY_MUL);

  const oak_ast_node_t* call = mul->lhs;
  OAK_CHECK_NODE_KIND(call, OAK_NODE_KIND_FN_CALL);
  OAK_CHECK_CHILD_COUNT(call, 2);

  const oak_ast_node_t* callee = oak_test_ast_child(call, 0);
  OAK_CHECK_NODE_KIND(callee, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(callee, "foo");

  const oak_ast_node_t* arg = oak_test_ast_child(call, 1);
  OAK_CHECK_NODE_KIND(arg, OAK_NODE_KIND_FN_CALL_ARG);
  OAK_CHECK_CHILD_COUNT(arg, 1);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(arg, 0), OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(oak_test_ast_child(arg, 0), 2);

  OAK_CHECK_NODE_KIND(mul->rhs, OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(mul->rhs, 3);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseFnCallExpr)
