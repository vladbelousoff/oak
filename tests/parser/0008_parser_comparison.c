#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseComparison)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("a == b;");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  /*
     Expected shape:
       STMT_EXPR
         BINARY_EQ
           IDENT("a")
           IDENT("b")
  */

  const oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(stmt, OAK_NODE_KIND_STMT_EXPR) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* eq = oak_test_ast_child(stmt, 0);
  if (oak_test_ast_kind(eq, OAK_NODE_KIND_BINARY_EQ) != OAK_SUCCESS)
    return OAK_FAILURE;

  if (oak_test_ast_kind(eq->lhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(eq->lhs->token->buf, "a") != 0)
    return OAK_FAILURE;

  if (oak_test_ast_kind(eq->rhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(eq->rhs->token->buf, "b") != 0)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseComparison)
