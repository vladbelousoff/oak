#include "oak_test_ast.h"

OAK_TEST_DECL(ParseWhileBreakContinue)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("while x < 10 { "
                                                 "  if x == 5 { break; } "
                                                 "  x = x + 1; "
                                                 "  continue; "
                                                 "}");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_STMT);
  const oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_STMT_WHILE);

  /*
     Expected shape:
       STMT_WHILE
         BINARY_LESS (condition)
         STMT_IF
         STMT_ASSIGNMENT
         STMT_CONTINUE
  */

  OAK_CHECK_CHILD_COUNT(root, 4);

  const oak_ast_node_t* cond = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(cond, OAK_NODE_KIND_BINARY_LESS);

  const oak_ast_node_t* if_stmt = oak_test_ast_child(root, 1);
  OAK_CHECK_NODE_KIND(if_stmt, OAK_NODE_KIND_STMT_IF);

  const oak_ast_node_t* break_stmt = oak_test_ast_child(if_stmt, 1);
  OAK_CHECK_NODE_KIND(break_stmt, OAK_NODE_KIND_STMT_BREAK);
  OAK_CHECK_CHILD_COUNT(break_stmt, 0);

  const oak_ast_node_t* assign = oak_test_ast_child(root, 2);
  OAK_CHECK_NODE_KIND(assign, OAK_NODE_KIND_STMT_ASSIGNMENT);

  const oak_ast_node_t* cont_stmt = oak_test_ast_child(root, 3);
  OAK_CHECK_NODE_KIND(cont_stmt, OAK_NODE_KIND_STMT_CONTINUE);
  OAK_CHECK_CHILD_COUNT(cont_stmt, 0);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseWhileBreakContinue)
