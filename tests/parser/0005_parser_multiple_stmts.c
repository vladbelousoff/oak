#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseMultipleStmts)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("let x = 1; y = x + 2;");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(root) != 2)
    return OAK_FAILURE;

  /*
     Expected shape:
       PROGRAM
         STMT_LET_ASSIGNMENT
           STMT_ASSIGNMENT
             IDENT("x")
             INT(1)
         STMT_ASSIGNMENT
           IDENT("y")
           BINARY_ADD
             IDENT("x")
             INT(2)
  */

  const oak_ast_node_t* let_stmt = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(let_stmt, OAK_NODE_KIND_STMT_LET_ASSIGNMENT) !=
      OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* assign1 = oak_test_ast_child(let_stmt, 0);
  if (oak_test_ast_kind(assign1, OAK_NODE_KIND_STMT_ASSIGNMENT) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* x_ident = oak_test_ast_child(assign1, 0);
  if (oak_test_ast_kind(x_ident, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(x_ident->token->buf, "x") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* one = oak_test_ast_child(assign1, 1);
  if (oak_test_ast_kind(one, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)one->token->buf != 1)
    return OAK_FAILURE;

  const oak_ast_node_t* assign2 = oak_test_ast_child(root, 1);
  if (oak_test_ast_kind(assign2, OAK_NODE_KIND_STMT_ASSIGNMENT) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* y_ident = oak_test_ast_child(assign2, 0);
  if (oak_test_ast_kind(y_ident, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(y_ident->token->buf, "y") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* add = oak_test_ast_child(assign2, 1);
  if (oak_test_ast_kind(add, OAK_NODE_KIND_BINARY_ADD) != OAK_SUCCESS)
    return OAK_FAILURE;

  if (oak_test_ast_kind(add->lhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(add->lhs->token->buf, "x") != 0)
    return OAK_FAILURE;

  if (oak_test_ast_kind(add->rhs, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)add->rhs->token->buf != 2)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseMultipleStmts)
