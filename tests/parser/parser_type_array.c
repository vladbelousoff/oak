#include "oak_test_ast.h"

OAK_TEST_DECL(ParseTypeArray)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("fn items() -> number[] { }");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /*
     Expected shape:
       PROGRAM
         FN_DECL
           IDENT("items")
           FN_PARAM_LIST (empty)
           FN_RETURN_TYPE
             TYPE_ARRAY (unary)
               child: IDENT("number")
           BLOCK (empty)
  */

  const struct oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(decl, OAK_NODE_FN_DECL);
  OAK_CHECK_CHILD_COUNT(decl, 4);

  const struct oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  OAK_CHECK_NODE_KIND(name, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(name, "items");

  const struct oak_ast_node_t* plist = oak_test_ast_child(decl, 1);
  OAK_CHECK_NODE_KIND(plist, OAK_NODE_FN_PARAM_LIST);
  OAK_CHECK_CHILD_COUNT(plist, 0);

  const struct oak_ast_node_t* ret_wrap = oak_test_ast_child(decl, 2);
  OAK_CHECK_NODE_KIND(ret_wrap, OAK_NODE_FN_RETURN_TYPE);
  OAK_CHECK_CHILD_COUNT(ret_wrap, 1);

  const struct oak_ast_node_t* ret_type = oak_test_ast_child(ret_wrap, 0);
  OAK_CHECK_NODE_KIND(ret_type, OAK_NODE_TYPE_ARRAY);
  OAK_CHECK_CHILD_COUNT(ret_type, 1);

  const struct oak_ast_node_t* elem = oak_test_ast_child(ret_type, 0);
  OAK_CHECK_NODE_KIND(elem, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(elem, "number");

  oak_parser_free(result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseTypeArray)
