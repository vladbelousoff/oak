#include "oak_test_ast.h"

OAK_TEST_DECL(ParseMemberAccess)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize("obj.x + obj.y;");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);

  /*
     Expected shape:
       PROGRAM
         STMT_EXPR
           BINARY_ADD
             MEMBER_ACCESS [IDENT("obj"), IDENT("x")]
             MEMBER_ACCESS [IDENT("obj"), IDENT("y")]
  */

  const struct oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_KIND_STMT_EXPR);

  const struct oak_ast_node_t* add = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(add, OAK_NODE_KIND_BINARY_ADD);
  OAK_CHECK_CHILD_COUNT(add, 2);

  const struct oak_ast_node_t* lhs = oak_test_ast_child(add, 0);
  OAK_CHECK_NODE_KIND(lhs, OAK_NODE_KIND_MEMBER_ACCESS);
  OAK_CHECK_CHILD_COUNT(lhs, 2);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(lhs, 0), OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(lhs, 0), "obj");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(lhs, 1), OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(lhs, 1), "x");

  const struct oak_ast_node_t* rhs = oak_test_ast_child(add, 1);
  OAK_CHECK_NODE_KIND(rhs, OAK_NODE_KIND_MEMBER_ACCESS);
  OAK_CHECK_CHILD_COUNT(rhs, 2);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(rhs, 0), OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(rhs, 0), "obj");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(rhs, 1), OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(rhs, 1), "y");

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseMemberAccess)
