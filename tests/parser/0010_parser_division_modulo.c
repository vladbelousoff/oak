#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

OAK_TEST_DECL(ParseDivisionModulo)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("12 / 4 % 3;");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  /* / and % have the same precedence and are left-associative:
       STMT_EXPR
         BINARY_MOD
           BINARY_DIV
             INT(12)
             INT(4)
           INT(3) */

  const oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(stmt, OAK_NODE_KIND_STMT_EXPR) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* mod = oak_test_ast_child(stmt, 0);
  if (oak_test_ast_kind(mod, OAK_NODE_KIND_BINARY_MOD) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* div = mod->lhs;
  if (oak_test_ast_kind(div, OAK_NODE_KIND_BINARY_DIV) != OAK_SUCCESS)
    return OAK_FAILURE;

  if (oak_test_ast_kind(div->lhs, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)div->lhs->token->buf != 12)
    return OAK_FAILURE;

  if (oak_test_ast_kind(div->rhs, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)div->rhs->token->buf != 4)
    return OAK_FAILURE;

  if (oak_test_ast_kind(mod->rhs, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)mod->rhs->token->buf != 3)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseDivisionModulo)
