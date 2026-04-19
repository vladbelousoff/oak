#include "oak_test_ast.h"

OAK_TEST_DECL(ParseDivisionModulo)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("12 / 4 % 3;");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /* / and % have the same precedence and are left-associative:
       STMT_EXPR
         BINARY_MOD
           BINARY_DIV
             INT(12)
             INT(4)
           INT(3) */

  const struct oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_STMT_EXPR);

  const struct oak_ast_node_t* mod = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(mod, OAK_NODE_BINARY_MOD);

  const struct oak_ast_node_t* div = mod->lhs;
  OAK_CHECK_NODE_KIND(div, OAK_NODE_BINARY_DIV);
  OAK_CHECK_NODE_KIND(div->lhs, OAK_NODE_INT);
  OAK_CHECK_INT_VAL(div->lhs, 12);
  OAK_CHECK_NODE_KIND(div->rhs, OAK_NODE_INT);
  OAK_CHECK_INT_VAL(div->rhs, 4);

  OAK_CHECK_NODE_KIND(mod->rhs, OAK_NODE_INT);
  OAK_CHECK_INT_VAL(mod->rhs, 3);

  oak_parser_free(result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseDivisionModulo)
