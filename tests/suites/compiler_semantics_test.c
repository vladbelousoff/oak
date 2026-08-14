/*
 * Compiler: name resolution, call checking, scoping, void functions, enums.
 *
 * Every negative case asserts a substring of the actual diagnostic. Asserting
 * only "compilation failed" would let these tests pass while the compiler
 * rejects the program for a completely unrelated reason -- which is exactly
 * what happens when a change breaks name lookup and every arity test still
 * goes green because the program now fails to parse instead.
 */

#include "oak_test_support.h"

OAK_TEST_SUITE(compiler_semantics);

UTEST_F(compiler_semantics, undefined_names_are_rejected)
{
  static const oak_case_t cases[] = {
    { "print(y);\n", "undefined variable 'y'" },
    { "print(does_not_exist(1, 2));\n", "undefined function 'does_not_exist'" },
    { "record Point { x : number; }\n"
      "let p = new Point { x : 1 };\n"
      "print(p.z);\n",
      "no such field 'z' on record 'Point'" },
    { "record Point { x : number; }\n"
      "let p = new Point { x : 1 };\n"
      "p.flip();\n",
      "no method 'flip' on record 'Point'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/*
 * Inference reports "no type" and "returns nothing" through the same void
 * type, and every caller of oak_reject_void runs before the expression is
 * compiled -- so an unresolvable initializer used to be reported as the
 * generic "this expression has no value (void)" while the very same
 * expression in call position reported properly. These cases pin the specific
 * diagnostic to the position that used to lose it.
 */
UTEST_F(compiler_semantics, let_initializers_report_the_specific_cause)
{
  static const oak_case_t cases[] = {
    { "let x = y + 1;\n", "undefined variable 'y'" },
    { "enum Color { Red, Green, Blue }\n"
      "let c = Color.Purple;\n",
      "'Purple' is not a variant of enum 'Color'" },
    { "record Point { x : number; }\n"
      "let p = new Point { x : 1 };\n"
      "let z = p.z;\n",
      "no such field 'z' on record 'Point'" },
    { "record Point { x : number; }\n"
      "let p = new Point { x : 1 };\n"
      "let z = p.flip();\n",
      "no method 'flip' on record 'Point'" },
    { "let p = new Nowhere { x : 1 };\n", "unknown record type 'Nowhere'" },
    { "let n = 1;\n"
      "let z = n.foo;\n",
      "field access '.foo' requires a record receiver, got 'number'" },
    { "let n = 1;\n"
      "let z = n[0];\n",
      "cannot index a value of type 'number'" },
    { "let n = 1;\n"
      "let z = n.foo();\n",
      "no method 'foo' on number" },
    { "let a = [1, 2];\n"
      "let z = a.nope();\n",
      "no method 'nope' on array of 'number[]'" },
    { "let s = 'hi';\n"
      "let z = s.nope();\n",
      "no method 'nope' on string" },
    { "record Point { x : number; }\n"
      "let z = Point;\n",
      "record 'Point' is a type, not a value" },
    { "enum Color { Red }\n"
      "let z = Color;\n",
      "enum 'Color' is a type, not a value" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/*
 * Reaching for a member of the wrong kind is a slip, not ignorance of the
 * type, so the diagnostic says which kind the name actually has.
 */
UTEST_F(compiler_semantics, member_diagnostics_name_the_other_kind)
{
  static const oak_case_t cases[] = {
    { "record P { x : number; }\n"
      "fn P.get(self) -> number { return self.x; }\n"
      "let p = new P { x : 1 };\n"
      "print(p.get);\n",
      "'get' is a method, call it as 'get()'" },
    { "record P { x : number; }\n"
      "let p = new P { x : 1 };\n"
      "print(p.x());\n",
      "'x' is a field, drop the '()' to read it" },
    { "record P { x : number; }\n"
      "fn P.make() -> P { return new P { x : 1 }; }\n"
      "let p = new P { x : 1 };\n"
      "print(p.make());\n",
      "'make' is a static method, call it as 'P.make()'" },
    { "record P { x : number; }\n"
      "fn P.get(self) -> number { return self.x; }\n"
      "let n = P.get();\n",
      "'get' is an instance method of record 'P'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_semantics, assignment_to_immutable_binding_is_rejected)
{
  static const oak_case_t cases[] = {
    { "let x = 1;\n"
      "x = 2;\n",
      "cannot assign to immutable variable 'x'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_semantics, duplicate_top_level_names_are_rejected)
{
  static const oak_case_t cases[] = {
    { "fn f() { }\n"
      "fn f() { }\n",
      "duplicate function 'f'" },
    { "record R { x : number; }\n"
      "record R { y : number; }\n",
      "duplicate record 'R'" },
    { "enum E { A }\n"
      "enum E { B }\n",
      "enum 'E' conflicts" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_semantics, call_arity_is_checked)
{
  static const oak_case_t cases[] = {
    { "fn add(a : number, b : number) -> number { return a + b; }\n"
      "print(add(1));\n",
      "expects 2 arguments, got 1" },
    { "fn add(a : number, b : number) -> number { return a + b; }\n"
      "print(add(1, 2, 3));\n",
      "expects 2 arguments, got 3" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_semantics, argument_types_are_checked)
{
  static const oak_case_t cases[] = {
    { "fn echo(s : string) -> number { return 1; }\n"
      "print(echo(42));\n",
      "expected type 'string'" },
    { "fn takes_bool(b : bool) { }\n"
      "takes_bool('yes');\n",
      "expected type 'bool'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_semantics, return_type_is_checked)
{
  static const oak_case_t cases[] = {
    { "fn f() -> number { return 'text'; }\n", "return type mismatch" },
    { "fn f() -> number { return; }\n", "missing return value" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/*
 * Module-scope bindings are deliberately not visible inside function or method
 * bodies -- functions see only their parameters and locals. This is what keeps
 * the language free of implicit global state.
 */
UTEST_F(compiler_semantics, module_scope_names_are_invisible_inside_bodies)
{
  static const oak_case_t cases[] = {
    { "let g = 1;\n"
      "fn f() -> number { return g; }\n",
      "not visible here (module scope only)" },
    { "let mut g = 1;\n"
      "fn f() { g = 2; }\n",
      "not visible here (module scope only)" },
    { "record R { x : number; }\n"
      "fn R.m(self) -> number { return g; }\n"
      "let g = 1;\n",
      "not visible here (module scope only)" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_semantics, a_local_may_shadow_a_module_scope_name)
{
  static const oak_case_t cases[] = {
    { "let g = 1;\n"
      "fn f() -> number { let g = 2; return g; }\n"
      "print(f());\n",
      null },
  };

  OAK_EXPECT_OK_CASES(cases);
}

UTEST_F(compiler_semantics, loop_control_outside_a_loop_is_rejected)
{
  static const oak_case_t cases[] = {
    { "break;\n", "'break' used outside of a loop" },
    { "continue;\n", "'continue' used outside of a loop" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* `return` at module scope may be caught by either stage depending on how the
 * grammar evolves, so this only asserts that it never reaches the VM. */
UTEST_F(compiler_semantics, return_at_module_scope_is_rejected)
{
  static const oak_case_t cases[] = {
    { "return 1;\n", null },
  };

  OAK_EXPECT_REJECTED_CASES(cases);
}

UTEST_F(compiler_semantics, functions_without_an_arrow_are_void)
{
  static const oak_case_t cases[] = {
    { "fn side() { return; }\n"
      "fn main() { side(); }\n"
      "main();\n",
      null },
    { "fn noop() { }\n"
      "noop();\n",
      null },
  };

  OAK_EXPECT_OK_CASES(cases);
}

UTEST_F(compiler_semantics, void_is_not_a_writable_type)
{
  static const oak_case_t cases[] = {
    /* `void` is spelled by omitting the arrow, never written out. */
    { "fn f() -> void { return; }\n", "'void' is not allowed after '->'" },
    { "fn bad() { return 1; }\n", "void function cannot return a value" },
    { "fn v() { }\n"
      "let x = v();\n",
      "this expression has no value (void)" },
    { "let x = print(1);\n", "this expression has no value (void)" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_semantics, enum_variants_are_accessed_through_their_type)
{
  static const oak_case_t cases[] = {
    { "enum Color { Red, Green, Blue }\n"
      "let c = Color.Green;\n"
      "print(c == Color.Green);\n",
      null },
    /* A trailing comma in the variant list is accepted. */
    { "enum Status { Off, On, }\n"
      "let s = Status.On;\n",
      null },
    /* Two enums keep independent ordinals. */
    { "enum A { X, Y }\n"
      "enum B { P, Q }\n"
      "let x = A.X;\n"
      "let q = B.Q;\n",
      null },
    /* An enum round-trips through a parameter of its own type. */
    { "enum Color { Red, Green, Blue }\n"
      "fn use_color(c : Color) -> Color { return c; }\n"
      "use_color(Color.Blue);\n",
      null },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/* Enum values are their own type: they are neither numbers nor each other. */
UTEST_F(compiler_semantics, enums_do_not_decay_to_numbers)
{
  static const oak_case_t cases[] = {
    { "enum Status { Off, On }\n"
      "let s = Status.On;\n"
      "let x = s + 10;\n",
      "operator not supported on enum values" },
    { "enum Color { Red, Green, Blue }\n"
      "fn use_color(c : number) -> number { return c; }\n"
      "use_color(Color.Blue);\n",
      "expected type 'number'" },
    { "enum A { X, Y }\n"
      "enum B { P, Q }\n"
      "let r = A.X == B.P;\n",
      "may only be compared to the same enum type" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_semantics, unknown_and_unqualified_variants_are_rejected)
{
  static const oak_case_t cases[] = {
    { "enum Color { Red, Green, Blue }\n"
      "print(Color.Purple);\n",
      "'Purple' is not a variant of enum 'Color'" },
    /* Variants never enter the enclosing scope on their own. */
    { "enum Color { Red, Green, Blue }\n"
      "print(Green);\n",
      "undefined variable 'Green'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* Variants must be comma-separated; whitespace alone does not separate them. */
UTEST_F(compiler_semantics, space_separated_variants_are_rejected)
{
  static const oak_case_t cases[] = {
    { "enum Dir { North South East West }\n"
      "let a = Dir.North;\n",
      null },
  };

  OAK_EXPECT_REJECTED_CASES(cases);
}
