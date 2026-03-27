#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseAssignment)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("x = 5;");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  /*
     Expected shape:
       STMT_ASSIGNMENT
         IDENT("x")
         INT(5)
  */

  const oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(stmt, OAK_NODE_KIND_STMT_ASSIGNMENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(stmt) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* ident = oak_test_ast_child(stmt, 0);
  if (oak_test_ast_kind(ident, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(ident->token->buf, "x") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* val = oak_test_ast_child(stmt, 1);
  if (oak_test_ast_kind(val, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)val->token->buf != 5)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseAssignment)
