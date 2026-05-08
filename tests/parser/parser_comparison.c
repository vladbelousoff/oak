#include "oak_count_of.h"
#include "oak_test_ast.h"

static enum oak_test_status_t
check_binary_comparison(const char* source,
                        const enum oak_node_kind_t expected_kind)
{
  struct oak_lexer_result_t* lexer = OAK_LEX(source);

  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  /*
     Expected shape:
       STMT_EXPR
         expected_kind
           IDENT("a")
           IDENT("b")
  */

  const struct oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_STMT_EXPR);

  const struct oak_ast_node_t* binary = oak_test_ast_child(stmt, 0);
  OAK_CHECK_NODE_KIND(binary, expected_kind);
  OAK_CHECK_NODE_KIND(binary->lhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(binary->lhs, "a");
  OAK_CHECK_NODE_KIND(binary->rhs, OAK_NODE_IDENT);
  OAK_CHECK_TOKEN_STR(binary->rhs, "b");

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_DECL(ParseComparison)
{
  static const struct
  {
    const char* source;
    enum oak_node_kind_t kind;
  } cases[] = {
    { "a == b;", OAK_NODE_BINARY_EQ },
    { "a != b;", OAK_NODE_BINARY_NEQ },
  };

  for (usize i = 0; i < oak_count_of(cases); ++i)
  {
    const enum oak_test_status_t status =
        check_binary_comparison(cases[i].source, cases[i].kind);
    if (status != OAK_TEST_OK)
      return status;
  }

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ParseComparison)
