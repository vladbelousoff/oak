#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseFnCallExpr)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("1 + foo(x = 2) * 3;");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  /*
     Function call binds tighter than * and +:
       BINARY_ADD
         INT(1)
         BINARY_MUL
           FN_CALL
             IDENT("foo")
             FN_CALL_ARG [IDENT("x"), INT(2)]
           INT(3)
  */

  const oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(stmt, OAK_NODE_KIND_STMT_EXPR) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* add = oak_test_ast_child(stmt, 0);
  if (oak_test_ast_kind(add, OAK_NODE_KIND_BINARY_ADD) != OAK_SUCCESS)
    return OAK_FAILURE;

  if (oak_test_ast_kind(add->lhs, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)add->lhs->token->buf != 1)
    return OAK_FAILURE;

  const oak_ast_node_t* mul = add->rhs;
  if (oak_test_ast_kind(mul, OAK_NODE_KIND_BINARY_MUL) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* call = mul->lhs;
  if (oak_test_ast_kind(call, OAK_NODE_KIND_FN_CALL) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(call) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* callee = oak_test_ast_child(call, 0);
  if (oak_test_ast_kind(callee, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(callee->token->buf, "foo") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* arg = oak_test_ast_child(call, 1);
  if (oak_test_ast_kind(arg, OAK_NODE_KIND_FN_CALL_ARG) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(arg) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* arg_name = oak_test_ast_child(arg, 0);
  if (oak_test_ast_kind(arg_name, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(arg_name->token->buf, "x") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* arg_val = oak_test_ast_child(arg, 1);
  if (oak_test_ast_kind(arg_val, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)arg_val->token->buf != 2)
    return OAK_FAILURE;

  if (oak_test_ast_kind(mul->rhs, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)mul->rhs->token->buf != 3)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseFnCallExpr)
