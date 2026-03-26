#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseStructDecl)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("type Point struct { x : number; y : number; }");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  /*
     Expected shape:
       STRUCT_DECL
         TYPE_NAME("Point")
         STRUCT_FIELD_DECL  (binary: lhs=IDENT, rhs=IDENT)
           lhs: IDENT("x")
           rhs: IDENT("number")
         STRUCT_FIELD_DECL  (binary: lhs=IDENT, rhs=IDENT)
           lhs: IDENT("y")
           rhs: IDENT("number")
  */

  const oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(decl, OAK_NODE_KIND_STRUCT_DECL) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(decl) != 3)
    return OAK_FAILURE;

  const oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  if (oak_test_ast_kind(name, OAK_NODE_KIND_TYPE_NAME) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(name->token->buf, "Point") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* field0 = oak_test_ast_child(decl, 1);
  if (oak_test_ast_kind(field0, OAK_NODE_KIND_STRUCT_FIELD_DECL) != OAK_SUCCESS)
    return OAK_FAILURE;

  if (oak_test_ast_kind(field0->lhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(field0->lhs->token->buf, "x") != 0)
    return OAK_FAILURE;

  if (oak_test_ast_kind(field0->rhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(field0->rhs->token->buf, "number") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* field1 = oak_test_ast_child(decl, 2);
  if (oak_test_ast_kind(field1, OAK_NODE_KIND_STRUCT_FIELD_DECL) != OAK_SUCCESS)
    return OAK_FAILURE;

  if (oak_test_ast_kind(field1->lhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(field1->lhs->token->buf, "y") != 0)
    return OAK_FAILURE;

  if (oak_test_ast_kind(field1->rhs, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(field1->rhs->token->buf, "number") != 0)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseStructDecl)
