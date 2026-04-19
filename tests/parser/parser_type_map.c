#include "oak_test_ast.h"

OAK_TEST_DECL(ParseTypeMap)
{
  struct oak_lexer_result_t* lexer =
      OAK_LEX("fn lookup() -> [string:number] { }");

  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /*
     Expected shape:
       PROGRAM
         FN_DECL
           IDENT("lookup")
           TYPE_MAP (binary)
             lhs: IDENT("string")
             rhs: IDENT("number")
           BLOCK (empty)
  */

  const struct oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(decl, OAK_NODE_FN_DECL);
  OAK_CHECK_CHILD_COUNT(decl, 3);

  const struct oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  OAK_CHECK_NODE_KIND(name, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(name, "lookup");

  const struct oak_ast_node_t* ret_type = oak_test_ast_child(decl, 1);
  OAK_CHECK_NODE_KIND(ret_type, OAK_NODE_TYPE_MAP);
  OAK_CHECK_CHILD_COUNT(ret_type, 2);

  const struct oak_ast_node_t* key_type = oak_test_ast_child(ret_type, 0);
  OAK_CHECK_NODE_KIND(key_type, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(key_type, "string");

  const struct oak_ast_node_t* val_type = oak_test_ast_child(ret_type, 1);
  OAK_CHECK_NODE_KIND(val_type, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(val_type, "number");

  oak_parser_free(result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseTypeMap)
