#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseFnCall)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("foo(x = 1, y = 2);");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  /*
     Expected shape:
       PROGRAM
         STMT_EXPR
           FN_CALL
             IDENT("foo")
             FN_CALL_ARG [IDENT("x"), INT(1)]
             FN_CALL_ARG [IDENT("y"), INT(2)]
  */

  const oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(stmt, OAK_NODE_KIND_STMT_EXPR) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* call = oak_test_ast_child(stmt, 0);
  if (oak_test_ast_kind(call, OAK_NODE_KIND_FN_CALL) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(call) != 3)
    return OAK_FAILURE;

  const oak_ast_node_t* callee = oak_test_ast_child(call, 0);
  if (oak_test_ast_kind(callee, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(callee->token->buf, "foo") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* arg0 = oak_test_ast_child(call, 1);
  if (oak_test_ast_kind(arg0, OAK_NODE_KIND_FN_CALL_ARG) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(arg0) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* arg0_name = oak_test_ast_child(arg0, 0);
  if (oak_test_ast_kind(arg0_name, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(arg0_name->token->buf, "x") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* arg0_val = oak_test_ast_child(arg0, 1);
  if (oak_test_ast_kind(arg0_val, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)arg0_val->token->buf != 1)
    return OAK_FAILURE;

  const oak_ast_node_t* arg1 = oak_test_ast_child(call, 2);
  if (oak_test_ast_kind(arg1, OAK_NODE_KIND_FN_CALL_ARG) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(arg1) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* arg1_name = oak_test_ast_child(arg1, 0);
  if (oak_test_ast_kind(arg1_name, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(arg1_name->token->buf, "y") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* arg1_val = oak_test_ast_child(arg1, 1);
  if (oak_test_ast_kind(arg1_val, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)arg1_val->token->buf != 2)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseFnCall)
