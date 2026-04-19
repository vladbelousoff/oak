#include "oak_test_ast.h"

OAK_TEST_DECL(ParseCompoundAssign)
{
  struct oak_lexer_result_t* lexer =
      OAK_LEX("a += 1; b -= 2; c *= 3; d /= 4; e %= 5;");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);
  OAK_CHECK_CHILD_COUNT(root, 5);

  /*
     Expected shape:
       PROGRAM
         STMT_ADD_ASSIGN  -> IDENT("a"), INT(1)
         STMT_SUB_ASSIGN  -> IDENT("b"), INT(2)
         STMT_MUL_ASSIGN  -> IDENT("c"), INT(3)
         STMT_DIV_ASSIGN  -> IDENT("d"), INT(4)
         STMT_MOD_ASSIGN  -> IDENT("e"), INT(5)
  */

  const struct
  {
    enum oak_node_kind_t kind;
    const char* name;
    int value;
  } cases[] = {
    { OAK_NODE_STMT_ADD_ASSIGN, "a", 1 },
    { OAK_NODE_STMT_SUB_ASSIGN, "b", 2 },
    { OAK_NODE_STMT_MUL_ASSIGN, "c", 3 },
    { OAK_NODE_STMT_DIV_ASSIGN, "d", 4 },
    { OAK_NODE_STMT_MOD_ASSIGN, "e", 5 },
  };

  for (int i = 0; i < 5; ++i)
  {
    const struct oak_ast_node_t* stmt = oak_test_ast_child(root, i);
    OAK_CHECK_NODE_KIND(stmt, cases[i].kind);
    OAK_CHECK_CHILD_COUNT(stmt, 2);

    const struct oak_ast_node_t* ident = oak_test_ast_child(stmt, 0);
    OAK_CHECK_NODE_KIND(ident, OAK_NODE_IDENT);
    OAK_CHECK_TOKEN_STR(ident, cases[i].name);

    const struct oak_ast_node_t* val = oak_test_ast_child(stmt, 1);
    OAK_CHECK_NODE_KIND(val, OAK_NODE_INT);
    OAK_CHECK_INT_VAL(val, cases[i].value);
  }

  oak_parser_free(result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseCompoundAssign)
