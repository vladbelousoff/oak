#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseFnParamMut)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("fn foo(mut x : int, y : int) -> int { x = 1; }");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  /*
     Expected shape:
       FN_DECL
         IDENT("foo")
         FN_PARAM [MUT_KEYWORD, IDENT("x"), IDENT("int")]
         FN_PARAM [IDENT("y"), IDENT("int")]
         TYPE_NAME("int")
         STMT_ASSIGNMENT
           IDENT("x")
           INT(1)
  */

  const oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(decl, OAK_NODE_KIND_FN_DECL) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(decl) != 5)
    return OAK_FAILURE;

  const oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  if (oak_test_ast_kind(name, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(name->token->buf, "foo") != 0)
    return OAK_FAILURE;

  // First param has 'mut' before the name: [MUT_KEYWORD, IDENT("x"),
  // IDENT("int")]
  const oak_ast_node_t* param0 = oak_test_ast_child(decl, 1);
  if (oak_test_ast_kind(param0, OAK_NODE_KIND_FN_PARAM) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(param0) != 3)
    return OAK_FAILURE;
  if (oak_test_ast_kind(oak_test_ast_child(param0, 0),
                        OAK_NODE_KIND_MUT_KEYWORD) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_kind(oak_test_ast_child(param0, 1), OAK_NODE_KIND_IDENT) !=
      OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(oak_test_ast_child(param0, 1)->token->buf, "x") != 0)
    return OAK_FAILURE;
  if (oak_test_ast_kind(oak_test_ast_child(param0, 2), OAK_NODE_KIND_IDENT) !=
      OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(oak_test_ast_child(param0, 2)->token->buf, "int") != 0)
    return OAK_FAILURE;

  // Second param has no 'mut': [IDENT("y"), IDENT("int")]
  const oak_ast_node_t* param1 = oak_test_ast_child(decl, 2);
  if (oak_test_ast_kind(param1, OAK_NODE_KIND_FN_PARAM) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(param1) != 2)
    return OAK_FAILURE;
  if (oak_test_ast_kind(oak_test_ast_child(param1, 0), OAK_NODE_KIND_IDENT) !=
      OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(oak_test_ast_child(param1, 0)->token->buf, "y") != 0)
    return OAK_FAILURE;
  if (oak_test_ast_kind(oak_test_ast_child(param1, 1), OAK_NODE_KIND_IDENT) !=
      OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(oak_test_ast_child(param1, 1)->token->buf, "int") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* ret_type = oak_test_ast_child(decl, 3);
  if (oak_test_ast_kind(ret_type, OAK_NODE_KIND_TYPE_NAME) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(ret_type->token->buf, "int") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* stmt = oak_test_ast_child(decl, 4);
  if (oak_test_ast_kind(stmt, OAK_NODE_KIND_STMT_ASSIGNMENT) != OAK_SUCCESS)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseFnParamMut)
