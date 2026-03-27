#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

OAK_TEST_DECL(ParseExprPrecedence)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("1 + 2 * 3;");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(stmt, OAK_NODE_KIND_STMT_EXPR) != OAK_SUCCESS)
    return OAK_FAILURE;

  /* Multiplication binds tighter than addition:
       BINARY_ADD
         INT(1)
         BINARY_MUL
           INT(2)
           INT(3) */
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

  if (oak_test_ast_kind(mul->lhs, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)mul->lhs->token->buf != 2)
    return OAK_FAILURE;

  if (oak_test_ast_kind(mul->rhs, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)mul->rhs->token->buf != 3)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseExprPrecedence)
