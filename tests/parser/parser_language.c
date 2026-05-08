#include "oak_count_of.h"
#include "oak_test_ast.h"

static enum oak_test_status_t parse_ok(const char* source,
                                        const enum oak_node_kind_t start)
{
  struct oak_lexer_result_t* lexer = OAK_LEX(source);
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, start, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);
  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

static enum oak_test_status_t parse_error(const char* source)
{
  struct oak_lexer_result_t* lexer = OAK_LEX(source);
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  const int error_count = oak_parser_error_count(&result);
  OAK_CHECK(root == null || error_count > 0);
  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(ParseExpressionPrecedenceAndLvalues)
{
  struct oak_lexer_result_t* lexer =
      OAK_LEX("target.field[0] = 1 + 2 * 3;\n"
              "a.b.c;\n"
              "a == b;\n"
              "a != b;\n"
              "a <= b;\n"
              "a >= b;\n");
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK_NODE_KIND(root, OAK_NODE_PROGRAM);

  const struct oak_ast_node_t* stmt = oak_test_ast_child(root, 0);
  OAK_CHECK_NODE_KIND(stmt, OAK_NODE_STMT_ASSIGNMENT);
  OAK_CHECK_CHILD_COUNT(stmt, 2);
  OAK_CHECK_NODE_KIND(oak_test_ast_child(stmt, 0), OAK_NODE_INDEX_ACCESS);

  const struct oak_ast_node_t* add = oak_test_ast_child(stmt, 1);
  OAK_CHECK_NODE_KIND(add, OAK_NODE_BINARY_ADD);
  OAK_CHECK_NODE_KIND(add->lhs, OAK_NODE_INT);
  OAK_CHECK_INT_VAL(add->lhs, 1);
  OAK_CHECK_NODE_KIND(add->rhs, OAK_NODE_BINARY_MUL);

  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(ParseStatementsAndControlFlow)
{
  OAK_CHECK(parse_ok("let mut x = 0;\n"
                     "x += 1;\n"
                     "if x > 0 { print(x); } else { print(0); }\n"
                     "while x < 10 { x += 1; }\n"
                     "for i from 0 to 3 { if i == 1 { continue; } break; }\n"
                     "for i, v in [1, 2] { print(i); print(v); }\n",
                     OAK_NODE_PROGRAM) == OAK_TEST_OK);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(ParseFunctionsRecordsEnumsAndModules)
{
  OAK_CHECK(parse_ok("import util.math as math;\n"
                     "enum Status { Planned, Active, Done, }\n"
                     "record Task {\n"
                     "  title : string;\n"
                     "  points : number;\n"
                     "  fn finish(mut self) { self.points = 0; }\n"
                     "  fn label(self) -> string { return self.title; }\n"
                     "}\n"
                     "fn make(title : string, mut points : number) -> Task {\n"
                     "  return new Task { title, points };\n"
                     "}\n",
                     OAK_NODE_PROGRAM) == OAK_TEST_OK);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(ParseCollectionTypesAndLiterals)
{
  OAK_CHECK(parse_ok("let nums = [] as number[];\n"
                     "let words = ['a', 'b'];\n"
                     "let map = [:] as [string:number];\n"
                     "let scores = ['alice': 1, 'bob': 2];\n"
                     "fn first(values : number[]) -> number { return values[0]; }\n"
                     "fn get(values : [string:number]) -> number { return values['x']; }\n",
                     OAK_NODE_PROGRAM) == OAK_TEST_OK);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(ParseSyntaxErrors)
{
  OAK_CHECK(parse_error("let x;") == OAK_TEST_OK);
  OAK_CHECK(parse_error("let x = ;") == OAK_TEST_OK);
  OAK_CHECK(parse_error("x = ;") == OAK_TEST_OK);
  OAK_CHECK(parse_error("if true {") == OAK_TEST_OK);
  OAK_CHECK(parse_error("fn f() -> number { return 1;") == OAK_TEST_OK);
  OAK_CHECK(parse_error("record Point") == OAK_TEST_OK);
  OAK_CHECK(parse_error("record Point;") == OAK_TEST_OK);
  OAK_CHECK(parse_error("1 + ;") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(ParseExpressionPrecedenceAndLvalues),
    OAK_TEST_ENTRY(ParseStatementsAndControlFlow),
    OAK_TEST_ENTRY(ParseFunctionsRecordsEnumsAndModules),
    OAK_TEST_ENTRY(ParseCollectionTypesAndLiterals),
    OAK_TEST_ENTRY(ParseSyntaxErrors),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
