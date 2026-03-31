#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseIfStmt)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("if x == 1 { y = 2; } else { y = 3; }");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_STMT);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_STMT_IF) != OAK_SUCCESS)
    return OAK_FAILURE;

  /*
     Expected shape:
       STMT_IF
         BINARY_EQ
           IDENT("x")
           INT(1)
         STMT_ASSIGNMENT [IDENT("y"), INT(2)]
         ELSE_BLOCK
           STMT_ASSIGNMENT [IDENT("y"), INT(3)]
  */

  if (oak_test_ast_child_count(root) != 3)
    return OAK_FAILURE;

  const oak_ast_node_t* cond = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(cond, OAK_NODE_KIND_BINARY_EQ) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* cond_lhs = oak_test_ast_child(cond, 0);
  if (oak_test_ast_kind(cond_lhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(cond_lhs->token->buf, "x") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* then_stmt = oak_test_ast_child(root, 1);
  if (oak_test_ast_kind(then_stmt, OAK_NODE_KIND_STMT_ASSIGNMENT) != OAK_SUCCESS)
    return OAK_FAILURE;

  const oak_ast_node_t* else_block = oak_test_ast_child(root, 2);
  if (oak_test_ast_kind(else_block, OAK_NODE_KIND_ELSE_BLOCK) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(else_block) != 1)
    return OAK_FAILURE;

  const oak_ast_node_t* else_stmt = oak_test_ast_child(else_block, 0);
  if (oak_test_ast_kind(else_stmt, OAK_NODE_KIND_STMT_ASSIGNMENT) != OAK_SUCCESS)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseIfStmt)
