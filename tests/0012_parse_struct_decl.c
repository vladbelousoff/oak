#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseStructDecl)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("type Point struct { x : i32; y : i32; }");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  /* PROGRAM_ITEM is a choice that returns its child directly, so the first
     child of PROGRAM is the TYPE_DECL itself.  The 'type' keyword, braces
     are all marked OAK_NODE_SKIP and therefore absent from the AST.
     Expected shape:
       STRUCT_DECL
         TYPE_NAME("Point")
         STRUCT_KEYWORD
         STRUCT_FIELD_DECLS
           STRUCT_FIELD_DECL
             IDENT("x")
             IDENT("i32")
           STRUCT_FIELD_DECL
             IDENT("y")
             IDENT("i32") */
  const oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(decl, OAK_NODE_KIND_STRUCT_DECL) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(decl) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  if (oak_test_ast_kind(name, OAK_NODE_KIND_TYPE_NAME) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(name->token->buf, "Point") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* fields = oak_test_ast_child(decl, 1);
  if (oak_test_ast_kind(fields, OAK_NODE_KIND_STRUCT_FIELD_DECLS) !=
      OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(fields) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* field0 = oak_test_ast_child(fields, 0);
  if (oak_test_ast_kind(field0, OAK_NODE_KIND_STRUCT_FIELD_DECL) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(field0) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* f0_name = oak_test_ast_child(field0, 0);
  if (oak_test_ast_kind(f0_name, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(f0_name->token->buf, "x") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* f0_type = oak_test_ast_child(field0, 1);
  if (oak_test_ast_kind(f0_type, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(f0_type->token->buf, "i32") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* field1 = oak_test_ast_child(fields, 1);
  if (oak_test_ast_kind(field1, OAK_NODE_KIND_STRUCT_FIELD_DECL) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(field1) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* f1_name = oak_test_ast_child(field1, 0);
  if (oak_test_ast_kind(f1_name, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(f1_name->token->buf, "y") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* f1_type = oak_test_ast_child(field1, 1);
  if (oak_test_ast_kind(f1_type, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(f1_type->token->buf, "i32") != 0)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseStructDecl)
