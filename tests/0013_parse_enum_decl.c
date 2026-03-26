#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_ast.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(ParseEnumDecl)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("type Color enum { Red Green Blue }");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (oak_test_ast_kind(root, OAK_NODE_KIND_PROGRAM) != OAK_SUCCESS)
    return OAK_FAILURE;

  /*
     Expected shape:
       ENUM_DECL
         IDENT("Color")
         IDENT("Red")
         IDENT("Green")
         IDENT("Blue")
  */

  const oak_ast_node_t* decl = oak_test_ast_child(root, 0);
  if (oak_test_ast_kind(decl, OAK_NODE_KIND_ENUM_DECL) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (oak_test_ast_child_count(decl) != 4)
    return OAK_FAILURE;

  const oak_ast_node_t* name = oak_test_ast_child(decl, 0);
  if (oak_test_ast_kind(name, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(name->token->buf, "Color") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* field0 = oak_test_ast_child(decl, 1);
  if (oak_test_ast_kind(field0, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(field0->token->buf, "Red") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* field1 = oak_test_ast_child(decl, 2);
  if (oak_test_ast_kind(field1, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(field1->token->buf, "Green") != 0)
    return OAK_FAILURE;

  const oak_ast_node_t* field2 = oak_test_ast_child(decl, 3);
  if (oak_test_ast_kind(field2, OAK_NODE_KIND_IDENT) != OAK_SUCCESS)
    return OAK_FAILURE;
  if (strcmp(field2->token->buf, "Blue") != 0)
    return OAK_FAILURE;

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_SUCCESS;
}

OAK_TEST_MAIN(ParseEnumDecl)
