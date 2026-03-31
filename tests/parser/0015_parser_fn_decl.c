#include "oak_test_ast.h"

OAK_TEST_DECL(ParseFnDecl)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("fn add(x : int, y : int) { x = 1; }");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  /*
     Expected shape:
       FN_DECL
         IDENT("add")
         FN_PARAM [IDENT("x"), IDENT("int")]
         FN_PARAM [IDENT("y"), IDENT("int")]
         STMT_ASSIGNMENT
           IDENT("x")
           INT(1)
  */

  const oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(decl, OAK_NODE_KIND_FN_DECL);
  OAK_CHECK_CHILD_COUNT(decl, 4);

  const oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  OAK_CHECK_NODE_KIND(name, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(name, "add");

  const oak_ast_node_t* param0 = oak_test_ast_child(decl, 1);
  OAK_CHECK_NODE_KIND(param0, OAK_NODE_KIND_FN_PARAM);
  OAK_CHECK_CHILD_COUNT(param0, 2);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param0, 0), OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param0, 0), "x");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param0, 1), OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param0, 1), "int");

  const oak_ast_node_t* param1 = oak_test_ast_child(decl, 2);
  OAK_CHECK_NODE_KIND(param1, OAK_NODE_KIND_FN_PARAM);
  OAK_CHECK_CHILD_COUNT(param1, 2);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param1, 0), OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param1, 0), "y");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(param1, 1), OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(param1, 1), "int");

  const oak_ast_node_t* stmt = oak_test_ast_child(decl, 3);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_KIND_STMT_ASSIGNMENT);
  OAK_CHECK_CHILD_COUNT(stmt, 2);

  const oak_ast_node_t* stmt_ident = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(stmt_ident, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(stmt_ident, "x");

  const oak_ast_node_t* stmt_val = oak_test_ast_child(stmt, 1);
  OAK_CHECK_NODE_KIND(stmt_val, OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(stmt_val, 1);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseFnDecl)
