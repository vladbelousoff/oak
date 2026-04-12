#include "oak_test_ast.h"

OAK_TEST_DECL(ParseIndexAccess)
{
  struct oak_lexer_result_t* lexer =
      OAK_LEX("arr[0]; obj.items[i]; matrix[0][1];");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_KIND_PROGRAM);
  OAK_CHECK_CHILD_COUNT(root, 3);

  /*
     Expected shape:
       PROGRAM
         STMT_EXPR
           INDEX_ACCESS [IDENT("arr"), INT(0)]
         STMT_EXPR
           INDEX_ACCESS [MEMBER_ACCESS [IDENT("obj"), IDENT("items")],
     IDENT("i")] STMT_EXPR INDEX_ACCESS [INDEX_ACCESS [IDENT("matrix"), INT(0)],
     INT(1)]
  */

  const struct oak_ast_node_t* stmt0 = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt0, OAK_NODE_KIND_STMT_EXPR);
  const struct oak_ast_node_t* idx0 = oak_test_ast_child(stmt0, 0);
  OAK_CHECK_NODE_KIND(idx0, OAK_NODE_KIND_INDEX_ACCESS);
  OAK_CHECK_CHILD_COUNT(idx0, 2);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(idx0, 0), OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(idx0, 0), "arr");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(idx0, 1), OAK_NODE_KIND_INT);
  OAK_CHECK_INT_VAL(oak_test_ast_child(idx0, 1), 0);

  const struct oak_ast_node_t* stmt1 = oak_test_ast_child(root, 1);
  OAK_CHECK_NODE_KIND(stmt1, OAK_NODE_KIND_STMT_EXPR);
  const struct oak_ast_node_t* idx1 = oak_test_ast_child(stmt1, 0);
  OAK_CHECK_NODE_KIND(idx1, OAK_NODE_KIND_INDEX_ACCESS);
  OAK_CHECK_CHILD_COUNT(idx1, 2);
  const struct oak_ast_node_t* member = oak_test_ast_child(idx1, 0);
  OAK_CHECK_NODE_KIND(member, OAK_NODE_KIND_MEMBER_ACCESS);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(member, 0), "obj");
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(member, 1), "items");
  OAK_CHECK_NODE_KIND(oak_test_ast_child(idx1, 1), OAK_NODE_KIND_IDENT);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(idx1, 1), "i");

  const struct oak_ast_node_t* stmt2 = oak_test_ast_child(root, 2);
  OAK_CHECK_NODE_KIND(stmt2, OAK_NODE_KIND_STMT_EXPR);
  const struct oak_ast_node_t* outer = oak_test_ast_child(stmt2, 0);
  OAK_CHECK_NODE_KIND(outer, OAK_NODE_KIND_INDEX_ACCESS);
  const struct oak_ast_node_t* inner = oak_test_ast_child(outer, 0);
  OAK_CHECK_NODE_KIND(inner, OAK_NODE_KIND_INDEX_ACCESS);
  OAK_CHECK_TOKEN_STR(oak_test_ast_child(inner, 0), "matrix");
  OAK_CHECK_INT_VAL(oak_test_ast_child(inner, 1), 0);
  OAK_CHECK_INT_VAL(oak_test_ast_child(outer, 1), 1);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseIndexAccess)
