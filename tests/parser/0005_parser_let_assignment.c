#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseLetAssignment)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("let y = 10;");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  /*
     Expected shape:
       STMT_LET_ASSIGNMENT
         STMT_ASSIGNMENT
           IDENT("y")
           INT(10)
  */

  const oak_ast_node_t* let_stmt = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(let_stmt, OAK_NODE_KIND_STMT_LET_ASSIGNMENT) !=
      OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(let_stmt) != 1)
    return OAK_FAILURE;

  const oak_ast_node_t* assign = oak_test_ast_child(let_stmt, 0);
  if (oak_test_ast_kind(assign, OAK_NODE_KIND_STMT_ASSIGNMENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(assign) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* ident = oak_test_ast_child(assign, 0);
  if (oak_test_ast_kind(ident, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(ident->token->buf, "y") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* val = oak_test_ast_child(assign, 1);
  if (oak_test_ast_kind(val, OAK_NODE_KIND_INT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (*(int*)val->token->buf != 10)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseLetAssignment)
