#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseFnDecl)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("fn add(x : int, y : int) -> int { x = 1; }");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  /*
     Expected shape:
       FN_DECL
         IDENT("add")
         FN_PARAM (binary: lhs=IDENT("x"), rhs=IDENT("int"))
         FN_PARAM (binary: lhs=IDENT("y"), rhs=IDENT("int"))
         TYPE_NAME("int")
         STMT_ASSIGNMENT
           IDENT("x")
           INT(1)
  */

  const oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(decl, OAK_NODE_KIND_FN_DECL) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(decl) != 5)
    return OAK_FAILURE;

  const oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  if (oak_test_ast_kind(name, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(name->token->buf, "add") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* param0 = oak_test_ast_child(decl, 1);
  if (oak_test_ast_kind(param0, OAK_NODE_KIND_FN_PARAM) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_kind(param0->lhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(param0->lhs->token->buf, "x") != 0)
    return OAK_FAILURE;
  if (oak_test_ast_kind(param0->rhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(param0->rhs->token->buf, "int") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* param1 = oak_test_ast_child(decl, 2);
  if (oak_test_ast_kind(param1, OAK_NODE_KIND_FN_PARAM) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_kind(param1->lhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(param1->lhs->token->buf, "y") != 0)
    return OAK_FAILURE;
  if (oak_test_ast_kind(param1->rhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(param1->rhs->token->buf, "int") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* ret_type = oak_test_ast_child(decl, 3);
  if (oak_test_ast_kind(ret_type, OAK_NODE_KIND_TYPE_NAME) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(ret_type->token->buf, "int") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* stmt = oak_test_ast_child(decl, 4);
  if (oak_test_ast_kind(stmt, OAK_NODE_KIND_STMT_ASSIGNMENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(stmt) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* stmt_ident = oak_test_ast_child(stmt, 0);
  if (oak_test_ast_kind(stmt_ident, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(stmt_ident->token->buf, "x") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* stmt_val = oak_test_ast_child(stmt, 1);
  if (oak_test_ast_kind(stmt_val, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)stmt_val->token->buf != 1)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseFnDecl)
