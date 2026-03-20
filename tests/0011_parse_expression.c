#include "oak_lex.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

OAK_TEST_DECL(ParseExpression)
{
  oak_lex_t lex;
  oak_lex_tokenize("1 + 2 * 3;", &lex);

  oak_parser_result_t* result = oak_parse(&lex, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(stmt, OAK_NODE_KIND_STATEMENT) != OAK_SUCCESS)
    return OAK_FAILURE;

  /* STATEMENT has one child: the expression (semicolon is skipped).
     Multiplication binds tighter than addition, so the tree should be:
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
  if (*(int*)add->lhs->tok->buf != 1)
    return OAK_FAILURE;

  const oak_ast_node_t* mul = add->rhs;
  if (oak_test_ast_kind(mul, OAK_NODE_KIND_BINARY_MUL) != OAK_SUCCESS)
    return OAK_FAILURE;

  if (oak_test_ast_kind(mul->lhs, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)mul->lhs->tok->buf != 2)
    return OAK_FAILURE;

  if (oak_test_ast_kind(mul->rhs, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)mul->rhs->tok->buf != 3)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lex_cleanup(&lex);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseExpression)
