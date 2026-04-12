#include "oak_test_ast.h"

OAK_TEST_DECL(ParseReturnStmt)
{
  struct oak_lexer_result_t* lexer = OAK_LEX(
      "fn add(x : number, y : number) -> number { return x + y; }");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  /*
     Expected shape:
       FN_DECL
         IDENT("add")
         FN_PARAM [IDENT("x"), IDENT("number")]
         FN_PARAM [IDENT("y"), IDENT("number")]
         TYPE_NAME("number")
         BLOCK
           STMT_RETURN
             BINARY_ADD
               IDENT("x")
               IDENT("y")
  */

  const struct oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(decl, OAK_NODE_KIND_FN_DECL);
  OAK_CHECK_CHILD_COUNT(decl, 5);

  const struct oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  OAK_CHECK_NODE_KIND(name, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(name, "add");

  const struct oak_ast_node_t* param0 = oak_test_ast_child(decl, 1);
  OAK_CHECK_NODE_KIND(param0, OAK_NODE_KIND_FN_PARAM);

  const struct oak_ast_node_t* param1 = oak_test_ast_child(decl, 2);
  OAK_CHECK_NODE_KIND(param1, OAK_NODE_KIND_FN_PARAM);

  const struct oak_ast_node_t* ret_type = oak_test_ast_child(decl, 3);
  OAK_CHECK_NODE_KIND(ret_type, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(ret_type, "number");

  const struct oak_ast_node_t* body = oak_test_ast_child(decl, 4);
  OAK_CHECK_NODE_KIND(body, OAK_NODE_KIND_BLOCK);
  OAK_CHECK_CHILD_COUNT(body, 1);

  const struct oak_ast_node_t* ret_stmt = oak_test_ast_child(body, 0);
  OAK_CHECK_NODE_KIND(ret_stmt, OAK_NODE_KIND_STMT_RETURN);
  OAK_CHECK_CHILD_COUNT(ret_stmt, 1);

  const struct oak_ast_node_t* add_expr = oak_test_ast_child(ret_stmt, 0);
  OAK_CHECK_NODE_KIND(add_expr, OAK_NODE_KIND_BINARY_ADD);

  const struct oak_ast_node_t* lhs = oak_test_ast_child(add_expr, 0);
  OAK_CHECK_NODE_KIND(lhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(lhs, "x");

  const struct oak_ast_node_t* rhs = oak_test_ast_child(add_expr, 1);
  OAK_CHECK_NODE_KIND(rhs, OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(rhs, "y");

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseReturnStmt)
