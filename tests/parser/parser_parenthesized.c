#include "oak_test_ast.h"

OAK_TEST_DECL(ParseParenthesized)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("(1 + 2) * 3;");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  /* Parentheses override default precedence:
       STMT_EXPR
         BINARY_MUL
           BINARY_ADD
             INT(1)
             INT(2)
           INT(3) */

  const oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_KIND_STMT_EXPR);

  const oak_ast_node_t* mul = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(mul, OAK_NODE_KIND_BINARY_MUL);

  const oak_ast_node_t* add = mul->lhs;
  OAK_CHECK_NODE_KIND(add, OAK_NODE_KIND_BINARY_ADD);
  OAK_CHECK_NODE_KIND(add->lhs, OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(add->lhs, 1);
  OAK_CHECK_NODE_KIND(add->rhs, OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(add->rhs, 2);

  OAK_CHECK_NODE_KIND(mul->rhs, OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(mul->rhs, 3);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseParenthesized)
