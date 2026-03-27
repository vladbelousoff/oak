#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseLogical)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("a && b || c;");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  /* && binds tighter than ||:
       STMT_EXPR
         BINARY_OR
           BINARY_AND
             IDENT("a")
             IDENT("b")
           IDENT("c") */

  const oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(stmt, OAK_NODE_KIND_STMT_EXPR) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* or_node = oak_test_ast_child(stmt, 0);
  if (oak_test_ast_kind(or_node, OAK_NODE_KIND_BINARY_OR) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* and_node = or_node->lhs;
  if (oak_test_ast_kind(and_node, OAK_NODE_KIND_BINARY_AND) != OAK_SUCCESS)
    return OAK_FAILURE;

  if (oak_test_ast_kind(and_node->lhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(and_node->lhs->token->buf, "a") != 0)
    return OAK_FAILURE;

  if (oak_test_ast_kind(and_node->rhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(and_node->rhs->token->buf, "b") != 0)
    return OAK_FAILURE;

  if (oak_test_ast_kind(or_node->rhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(or_node->rhs->token->buf, "c") != 0)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseLogical)
