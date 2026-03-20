#include "oak_lex.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseTypeDecl)
{
  oak_lex_t lex;
  oak_lex_tokenize("type Point { x : i32; y : i32; }", &lex);

  oak_parser_result_t* result = oak_parse(&lex, OAK_NODE_KIND_PROGRAM);
  oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  /* PROGRAM_ITEM is a choice that returns its child directly, so the first
     child of PROGRAM is the TYPE_DECL itself.  The 'type' keyword, braces
     are all marked OAK_NODE_SKIP and therefore absent from the AST.
     Expected shape:
       TYPE_DECL
         TYPE_NAME("Point")
         TYPE_FIELD_DECLS
           TYPE_FIELD_DECL
             IDENT("x")
             IDENT("i32")
           TYPE_FIELD_DECL
             IDENT("y")
             IDENT("i32") */
  const oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(decl, OAK_NODE_KIND_TYPE_DECL) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(decl) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  if (oak_test_ast_kind(name, OAK_NODE_KIND_TYPE_NAME) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(name->tok->buf, "Point") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* fields = oak_test_ast_child(decl, 1);
  if (oak_test_ast_kind(fields, OAK_NODE_KIND_TYPE_FIELD_DECLS) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(fields) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* field0 = oak_test_ast_child(fields, 0);
  if (oak_test_ast_kind(field0, OAK_NODE_KIND_TYPE_FIELD_DECL) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(field0) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* f0_name = oak_test_ast_child(field0, 0);
  if (oak_test_ast_kind(f0_name, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(f0_name->tok->buf, "x") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* f0_type = oak_test_ast_child(field0, 1);
  if (oak_test_ast_kind(f0_type, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(f0_type->tok->buf, "i32") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* field1 = oak_test_ast_child(fields, 1);
  if (oak_test_ast_kind(field1, OAK_NODE_KIND_TYPE_FIELD_DECL) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(field1) != 2)
    return OAK_FAILURE;

  const oak_ast_node_t* f1_name = oak_test_ast_child(field1, 0);
  if (oak_test_ast_kind(f1_name, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(f1_name->tok->buf, "y") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* f1_type = oak_test_ast_child(field1, 1);
  if (oak_test_ast_kind(f1_type, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(f1_type->tok->buf, "i32") != 0)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lex_cleanup(&lex);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseTypeDecl)
