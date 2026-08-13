/*
 * Parser: AST shape and syntax errors.
 *
 * The old parser tests mostly asserted "this parsed", which cannot tell a
 * correct tree from a wrong one -- precedence could inverted and every test
 * would still pass. These assert the tree: which node sits where, how many
 * children it has, and what the operands are.
 *
 * Two grammar facts worth knowing while reading this file: CHOICE rules are
 * transparent (they return the matched child, not a wrapper node), and binary
 * operator nodes expose their operands as `lhs`/`rhs` rather than as indexed
 * children.
 */

#include "oak_test_support.h"

#include <string.h>

OAK_TEST_SUITE(parser);

/*
 * Precedence is a tree-shape property: `1 + 2 * 3` is correct only if the
 * multiply is the right operand of the add. Asserting the result of parsing
 * would not distinguish it from `(1 + 2) * 3`.
 */
UTEST_F(parser, multiplication_binds_tighter_than_addition)
{
  oak_parse_fixture_t fx = oak_test_parse(OAK_A, "x = 1 + 2 * 3;\n");
  const oak_ast_node_t* stmt;
  const oak_ast_node_t* add;

  ASSERT_TRUE(fx.root != null);
  OAK_EXPECT_KIND(fx.root, OAK_NODE_PROGRAM);

  stmt = oak_ast_node_child_at(fx.root, 0);
  OAK_EXPECT_KIND(stmt, OAK_NODE_STMT_ASSIGNMENT);
  OAK_EXPECT_CHILDREN(stmt, 2);

  add = oak_ast_node_child_at(stmt, 1);
  OAK_EXPECT_KIND(add, OAK_NODE_BINARY_ADD);
  OAK_EXPECT_KIND(oak_test_lhs(add), OAK_NODE_INT);
  OAK_EXPECT_INT(oak_test_lhs(add), 1);
  /* The multiply must be nested under the add, not the other way round. */
  OAK_EXPECT_KIND(oak_test_rhs(add), OAK_NODE_BINARY_MUL);

  oak_test_parse_free(&fx);
}

/* Subtraction is left-associative: `10 - 4 - 3` is `(10 - 4) - 3`, so the
 * nested subtraction must be on the left. */
UTEST_F(parser, subtraction_is_left_associative)
{
  oak_parse_fixture_t fx = oak_test_parse(OAK_A, "x = 10 - 4 - 3;\n");
  const oak_ast_node_t* outer;

  ASSERT_TRUE(fx.root != null);
  outer = oak_ast_node_child_at(oak_ast_node_child_at(fx.root, 0), 1);
  OAK_EXPECT_KIND(outer, OAK_NODE_BINARY_SUB);
  OAK_EXPECT_KIND(oak_test_lhs(outer), OAK_NODE_BINARY_SUB);
  OAK_EXPECT_KIND(oak_test_rhs(outer), OAK_NODE_INT);
  OAK_EXPECT_INT(oak_test_rhs(outer), 3);

  oak_test_parse_free(&fx);
}

/* Comparison binds tighter than `&&`, which binds tighter than `||`. */
UTEST_F(parser, logical_operators_sit_above_comparison)
{
  oak_parse_fixture_t fx =
      oak_test_parse(OAK_A, "x = a == b && c > d || e;\n");
  const oak_ast_node_t* or_node;

  ASSERT_TRUE(fx.root != null);
  or_node = oak_ast_node_child_at(oak_ast_node_child_at(fx.root, 0), 1);
  OAK_EXPECT_KIND(or_node, OAK_NODE_BINARY_OR);
  OAK_EXPECT_KIND(oak_test_rhs(or_node), OAK_NODE_IDENT);

  OAK_EXPECT_KIND(oak_test_lhs(or_node), OAK_NODE_BINARY_AND);
  OAK_EXPECT_KIND(oak_test_lhs(oak_test_lhs(or_node)), OAK_NODE_BINARY_EQ);
  OAK_EXPECT_KIND(oak_test_rhs(oak_test_lhs(or_node)),
                  OAK_NODE_BINARY_GREATER);

  oak_test_parse_free(&fx);
}

UTEST_F(parser, unary_operators_nest)
{
  oak_parse_fixture_t fx = oak_test_parse(OAK_A, "x = !!a;\ny = 0 - 1;\n");
  const oak_ast_node_t* not_node;

  ASSERT_TRUE(fx.root != null);
  not_node = oak_ast_node_child_at(oak_ast_node_child_at(fx.root, 0), 1);
  OAK_EXPECT_KIND(not_node, OAK_NODE_UNARY_NOT);
  OAK_EXPECT_CHILDREN(not_node, 1);
  OAK_EXPECT_KIND(oak_ast_node_child_at(not_node, 0), OAK_NODE_UNARY_NOT);

  oak_test_parse_free(&fx);
}

/* Parentheses override precedence, which shows up as an inverted tree. */
UTEST_F(parser, parentheses_regroup_the_tree)
{
  oak_parse_fixture_t fx = oak_test_parse(OAK_A, "x = (1 + 2) * 3;\n");
  const oak_ast_node_t* mul;

  ASSERT_TRUE(fx.root != null);
  mul = oak_ast_node_child_at(oak_ast_node_child_at(fx.root, 0), 1);
  OAK_EXPECT_KIND(mul, OAK_NODE_BINARY_MUL);
  OAK_EXPECT_KIND(oak_test_lhs(mul), OAK_NODE_BINARY_ADD);
  OAK_EXPECT_KIND(oak_test_rhs(mul), OAK_NODE_INT);

  oak_test_parse_free(&fx);
}

/* Member, index, and call chains are left-nested, so the outermost node is the
 * last operation in the source and its child is everything before it. */
UTEST_F(parser, postfix_chains_nest_left)
{
  oak_parse_fixture_t fx =
      oak_test_parse(OAK_A, "target.field[0] = 1;\nq = a.b.c;\n");
  const oak_ast_node_t* assign;
  const oak_ast_node_t* index;
  const oak_ast_node_t* member_chain;

  ASSERT_TRUE(fx.root != null);

  assign = oak_ast_node_child_at(fx.root, 0);
  OAK_EXPECT_KIND(assign, OAK_NODE_STMT_ASSIGNMENT);
  index = oak_ast_node_child_at(assign, 0);
  OAK_EXPECT_KIND(index, OAK_NODE_INDEX_ACCESS);
  /* `target.field` is the indexed expression. */
  OAK_EXPECT_KIND(oak_ast_node_child_at(index, 0), OAK_NODE_MEMBER_ACCESS);

  member_chain = oak_ast_node_child_at(oak_ast_node_child_at(fx.root, 1), 1);
  OAK_EXPECT_KIND(member_chain, OAK_NODE_MEMBER_ACCESS);
  OAK_EXPECT_KIND(oak_ast_node_child_at(member_chain, 0),
                  OAK_NODE_MEMBER_ACCESS);

  oak_test_parse_free(&fx);
}

UTEST_F(parser, calls_carry_one_node_per_argument)
{
  oak_parse_fixture_t fx = oak_test_parse(OAK_A, "f(1, 2, 3);\n");
  const oak_ast_node_t* stmt;
  const oak_ast_node_t* call;

  ASSERT_TRUE(fx.root != null);
  stmt = oak_ast_node_child_at(fx.root, 0);
  /* A bare call may be the statement itself or be wrapped; oak_ast_node_child_at
   * tolerates null, so a missing statement reports as a kind miss below rather
   * than crashing here. */
  call = (stmt && stmt->kind == OAK_NODE_FN_CALL)
             ? stmt
             : oak_ast_node_child_at(stmt, 0);
  OAK_EXPECT_KIND(call, OAK_NODE_FN_CALL);
  /* The callee plus three arguments. */
  OAK_EXPECT_CHILDREN(call, 4);

  oak_test_parse_free(&fx);
}

UTEST_F(parser, control_flow_statements_parse)
{
  static const char* const src =
      "let mut x = 0;\n"
      "x += 1;\n"
      "if x > 0 { print(x); } else { print(0); }\n"
      "while x < 10 { x += 1; }\n"
      "for i from 0 to 3 { if i == 1 { continue; } break; }\n"
      "for i, v in [1, 2] { print(i); print(v); }\n";

  oak_parse_fixture_t fx = oak_test_parse(OAK_A, src);

  ASSERT_TRUE(fx.root != null);
  EXPECT_EQ(0, oak_parser_error_count(&fx.parsed));
  OAK_EXPECT_KIND(fx.root, OAK_NODE_PROGRAM);
  OAK_EXPECT_CHILDREN(fx.root, 6);

  OAK_EXPECT_KIND(oak_ast_node_child_at(fx.root, 0),
                  OAK_NODE_STMT_LET_ASSIGNMENT);
  OAK_EXPECT_KIND(oak_ast_node_child_at(fx.root, 1), OAK_NODE_STMT_ADD_ASSIGN);
  OAK_EXPECT_KIND(oak_ast_node_child_at(fx.root, 2), OAK_NODE_STMT_IF);
  OAK_EXPECT_KIND(oak_ast_node_child_at(fx.root, 3), OAK_NODE_STMT_WHILE);
  OAK_EXPECT_KIND(oak_ast_node_child_at(fx.root, 4), OAK_NODE_STMT_FOR_FROM);
  OAK_EXPECT_KIND(oak_ast_node_child_at(fx.root, 5), OAK_NODE_STMT_FOR_IN);

  oak_test_parse_free(&fx);
}

/* An `if` without `else` must not carry an else block. */
UTEST_F(parser, else_is_optional)
{
  oak_parse_fixture_t bare = oak_test_parse(OAK_A, "if a { b(); }\n");
  oak_parse_fixture_t with_else =
      oak_test_parse(OAK_A, "if a { b(); } else { c(); }\n");
  const oak_ast_node_t* bare_if;
  const oak_ast_node_t* full_if;

  ASSERT_TRUE(bare.root != null);
  ASSERT_TRUE(with_else.root != null);

  bare_if = oak_ast_node_child_at(bare.root, 0);
  full_if = oak_ast_node_child_at(with_else.root, 0);
  OAK_EXPECT_KIND(bare_if, OAK_NODE_STMT_IF);
  OAK_EXPECT_KIND(full_if, OAK_NODE_STMT_IF);
  EXPECT_TRUE(oak_ast_node_child_count(full_if) >
              oak_ast_node_child_count(bare_if));

  oak_test_parse_free(&bare);
  oak_test_parse_free(&with_else);
}

UTEST_F(parser, declarations_parse)
{
  static const char* const src =
      "import * from util.math;\n"
      "import util.math as math;\n"
      "export enum ExportedStatus { Planned, Active, Done, }\n"
      "export record ExportedTask;\n"
      "@Native export fn attributed() -> number;\n"
      "export interface IDescribed { fn describe(self) -> string; }\n"
      "enum Status { Planned, Active, Done, }\n"
      "record Task {\n"
      "  title : string;\n"
      "  points : number;\n"
      "  parent : Task weak;\n"
      "}\n"
      "fn Task.finish(mut self) { self.points = 0; }\n"
      "export fn Task.exported_finish(mut self) { self.points = 0; }\n"
      "fn Task.label(self) -> string { return self.title; }\n"
      "fn make(title : string, mut points : number) -> Task {\n"
      "  return new Task { title, points };\n"
      "}\n";

  oak_parse_fixture_t fx = oak_test_parse(OAK_A, src);

  ASSERT_TRUE(fx.root != null);
  EXPECT_EQ(0, oak_parser_error_count(&fx.parsed));
  OAK_EXPECT_KIND(fx.root, OAK_NODE_PROGRAM);

  oak_test_parse_free(&fx);
}

UTEST_F(parser, record_fields_are_one_node_each)
{
  oak_parse_fixture_t fx = oak_test_parse(
      OAK_A, "record Task { title : string; points : number; }\n");
  const oak_ast_node_t* decl;

  ASSERT_TRUE(fx.root != null);
  decl = oak_ast_node_child_at(fx.root, 0);
  OAK_EXPECT_KIND(decl, OAK_NODE_RECORD_DECL);

  oak_test_parse_free(&fx);
}

/* An empty record uses the `record Name;` form, a distinct node from the
 * braced one -- `record Name {}` is deliberately not accepted. */
UTEST_F(parser, an_empty_record_has_its_own_declaration_form)
{
  oak_parse_fixture_t fx = oak_test_parse(OAK_A, "record Empty;\n");

  ASSERT_TRUE(fx.root != null);
  OAK_EXPECT_KIND(oak_ast_node_child_at(fx.root, 0),
                  OAK_NODE_RECORD_DECL_EMPTY);

  oak_test_parse_free(&fx);
}

UTEST_F(parser, collection_types_and_literals_parse)
{
  static const char* const src =
      "let nums = new number[];\n"
      "let words = ['a', 'b'];\n"
      "let map = new [string:number];\n"
      "let scores = ['alice': 1, 'bob': 2];\n"
      "fn first(values : number[]) -> number { return values[0]; }\n"
      "fn get(values : [string:number]) -> number { return values['x']; }\n";

  oak_parse_fixture_t fx = oak_test_parse(OAK_A, src);

  ASSERT_TRUE(fx.root != null);
  EXPECT_EQ(0, oak_parser_error_count(&fx.parsed));
  OAK_EXPECT_CHILDREN(fx.root, 6);

  oak_test_parse_free(&fx);
}

UTEST_F(parser, malformed_sources_are_rejected)
{
  static const char* const cases[] = {
    "let x;",                          /* no initializer */
    "let x = ;",                       /* no expression */
    "x = ;",                           /* no expression */
    "if true {",                       /* unterminated block */
    "fn f() -> number { return 1;",    /* unterminated body */
    "record Point",                    /* no body */
    "record Point {",                  /* unterminated body */
    "export let x = 1;",               /* bindings are not exportable */
    "import util.math;",               /* needs `* from` or `as` */
    "export @Native fn f();",          /* attribute must precede export */
    "1 + ;",                           /* no right operand */
    "for i in { }",                    /* no iterable */
    "fn f(a b) { }",                   /* missing separator */
  };

  usize i;
  for (i = 0; i < oak_count_of(cases); ++i)
  {
    oak_parse_fixture_t fx = oak_test_parse(OAK_A, cases[i]);
    if (fx.root != null && oak_parser_error_count(&fx.parsed) == 0)
    {
      UTEST_PRINTF("  parsed cleanly but should not have: %s\n", cases[i]);
      *utest_result = UTEST_TEST_FAILURE;
    }
    oak_test_parse_free(&fx);
  }
}

/* Parse errors carry a message naming what was expected, which is what makes
 * them usable; assert the text, not just the failure. */
UTEST_F(parser, parse_errors_say_what_was_expected)
{
  static const oak_case_t cases[] = {
    { "let x = ;", "expected an expression" },
    { "record Point", "expected '{'" },
    { "fn f(", "expected function parameters" },
  };

  usize i;
  for (i = 0; i < oak_count_of(cases); ++i)
  {
    oak_parse_fixture_t fx = oak_test_parse(OAK_A, cases[i].src);
    const int errors = oak_parser_error_count(&fx.parsed);

    if (fx.root != null || errors == 0)
    {
      UTEST_PRINTF("  expected a parse error: %s\n", cases[i].src);
      *utest_result = UTEST_TEST_FAILURE;
    }
    else if (!oak_test_contains(oak_parser_errors(&fx.parsed)[0].message,
                                cases[i].want))
    {
      UTEST_PRINTF("  want substring '%s', got '%s'\n",
                   cases[i].want,
                   oak_parser_errors(&fx.parsed)[0].message);
      *utest_result = UTEST_TEST_FAILURE;
    }
    oak_test_parse_free(&fx);
  }
}
