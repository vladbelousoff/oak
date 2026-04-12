#include "oak_test_ast.h"

OAK_TEST_DECL(ParseLvalueAssign)
{
  struct oak_lexer_result_t* lexer =
      oak_lexer_tokenize("obj.x = 1; arr[0] = 2; obj.x += 3; arr[i] -= 4;");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);
  OAK_CHECK_CHILD_COUNT(root, 4);

  /*
     Expected shape:
       PROGRAM
         STMT_ASSIGNMENT [MEMBER_ACCESS [IDENT("obj"), IDENT("x")], INT(1)]
         STMT_ASSIGNMENT [INDEX_ACCESS [IDENT("arr"), INT(0)], INT(2)]
         STMT_ADD_ASSIGN (binary)
           lhs: MEMBER_ACCESS [IDENT("obj"), IDENT("x")]
           rhs: INT(3)
         STMT_SUB_ASSIGN (binary)
           lhs: INDEX_ACCESS [IDENT("arr"), IDENT("i")]
           rhs: INT(4)
  */

  const struct oak_ast_node_t* stmt0 = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt0, OAK_NODE_KIND_STMT_ASSIGNMENT);
  OAK_CHECK_CHILD_COUNT(stmt0, 2);
  const struct oak_ast_node_t* s0_lhs = oak_test_ast_child(stmt0, 0);
  OAK_CHECK_NODE_KIND(s0_lhs, OAK_NODE_KIND_MEMBER_ACCESS);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(s0_lhs, 0), "obj");
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(s0_lhs, 1), "x");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(stmt0, 1), OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(oak_test_ast_child(stmt0, 1), 1);

  const struct oak_ast_node_t* stmt1 = oak_test_ast_child(root, 1);
  OAK_CHECK_NODE_KIND(stmt1, OAK_NODE_KIND_STMT_ASSIGNMENT);
  OAK_CHECK_CHILD_COUNT(stmt1, 2);
  const struct oak_ast_node_t* s1_lhs = oak_test_ast_child(stmt1, 0);
  OAK_CHECK_NODE_KIND(s1_lhs, OAK_NODE_KIND_INDEX_ACCESS);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(s1_lhs, 0), "arr");
  OAK_CHECK_INT_VAL(oak_test_ast_child(s1_lhs, 1), 0);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(stmt1, 1), OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(oak_test_ast_child(stmt1, 1), 2);

  const struct oak_ast_node_t* stmt2 = oak_test_ast_child(root, 2);
  OAK_CHECK_NODE_KIND(stmt2, OAK_NODE_KIND_STMT_ADD_ASSIGN);
  OAK_CHECK_CHILD_COUNT(stmt2, 2);
  const struct oak_ast_node_t* s2_lhs = oak_test_ast_child(stmt2, 0);
  OAK_CHECK_NODE_KIND(s2_lhs, OAK_NODE_KIND_MEMBER_ACCESS);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(s2_lhs, 0), "obj");
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(s2_lhs, 1), "x");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(stmt2, 1), OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(oak_test_ast_child(stmt2, 1), 3);

  const struct oak_ast_node_t* stmt3 = oak_test_ast_child(root, 3);
  OAK_CHECK_NODE_KIND(stmt3, OAK_NODE_KIND_STMT_SUB_ASSIGN);
  OAK_CHECK_CHILD_COUNT(stmt3, 2);
  const struct oak_ast_node_t* s3_lhs = oak_test_ast_child(stmt3, 0);
  OAK_CHECK_NODE_KIND(s3_lhs, OAK_NODE_KIND_INDEX_ACCESS);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(s3_lhs, 0), "arr");
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(s3_lhs, 1), "i");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(stmt3, 1), OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(oak_test_ast_child(stmt3, 1), 4);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseLvalueAssign)
