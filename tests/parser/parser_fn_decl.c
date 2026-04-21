#include "oak_test_ast.h"

OAK_TEST_DECL(ParseFnDecl)
{
  struct oak_lexer_result_t* lexer =
      OAK_LEX("fn add(x : number, y : number) { x = 1; }");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /*
     Expected shape:
       FN_DECL
         IDENT("add")
         FN_PARAM_LIST
           FN_PARAM [IDENT("x"), IDENT("number")]
           FN_PARAM [IDENT("y"), IDENT("number")]
         BLOCK
           STMT_ASSIGNMENT
             IDENT("x")
             INT(1)
  */

  const struct oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(decl, OAK_NODE_FN_DECL);
  OAK_CHECK_CHILD_COUNT(decl, 3);

  const struct oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  OAK_CHECK_NODE_KIND(name, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(name, "add");

  const struct oak_ast_node_t* plist = oak_test_ast_child(decl, 1);
  OAK_CHECK_NODE_KIND(plist, OAK_NODE_FN_PARAM_LIST);
  OAK_CHECK_CHILD_COUNT(plist, 2);

  const struct oak_ast_node_t* param0 = oak_test_ast_child(plist, 0);
  OAK_CHECK_NODE_KIND(param0, OAK_NODE_FN_PARAM);
  OAK_CHECK_CHILD_COUNT(param0, 2);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param0, 0), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param0, 0), "x");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param0, 1), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param0, 1), "number");

  const struct oak_ast_node_t* param1 = oak_test_ast_child(plist, 1);
  OAK_CHECK_NODE_KIND(param1, OAK_NODE_FN_PARAM);
  OAK_CHECK_CHILD_COUNT(param1, 2);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param1, 0), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param1, 0), "y");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param1, 1), OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param1, 1), "number");

  const struct oak_ast_node_t* body = oak_test_ast_child(decl, 2);
  OAK_CHECK_NODE_KIND(body, OAK_NODE_BLOCK);
  OAK_CHECK_CHILD_COUNT(body, 1);

  const struct oak_ast_node_t* stmt = oak_test_ast_child(body, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_STMT_ASSIGNMENT);
  OAK_CHECK_CHILD_COUNT(stmt, 2);

  const struct oak_ast_node_t* stmt_ident = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(stmt_ident, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(stmt_ident, "x");

  const struct oak_ast_node_t* stmt_val = oak_test_ast_child(stmt, 1);
  OAK_CHECK_NODE_KIND(stmt_val, OAK_NODE_INT);
  OAK_CHECK_INT_VAL(stmt_val, 1);

  oak_parser_free(result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseFnDecl)
