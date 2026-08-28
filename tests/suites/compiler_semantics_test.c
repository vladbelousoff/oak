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
    { "record P { x : number;\n"
      "  fn get() -> number { return self.x; }\n"
      "}\n"
      "let p = new P { x : 1 };\n"
      "print(p.get);\n",
      "'get' is a method, call it as 'get()'" },
    { "record P { x : number; }\n"
      "let p = new P { x : 1 };\n"
      "print(p.x());\n",
      "'x' is a field, drop the '()' to read it" },
    { "record P { x : number;\n"
      "  fn static make() -> P { return new P { x : 1 }; }\n"
      "}\n"
      "let p = new P { x : 1 };\n"
      "print(p.make());\n",
      "'make' is a static method, call it as 'P.make()'" },
    { "record P { x : number;\n"
      "  fn get() -> number { return self.x; }\n"
      "}\n"
      "let n = P.get();\n",
      "'get' is an instance method of record 'P'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* Rebinding a local is not a mutation of anything shared -- the slot belongs to
 * this frame -- so `let` needs no qualifier to allow it. What a binding may be
 * rebound *to* is the access question, and compiler_mut covers it. */
UTEST_F(compiler_semantics, a_binding_may_be_reassigned)
{
  static const oak_case_t cases[] = {
    { "let x = 1;\n"
      "x = 2;\n"
      "print(x);\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

UTEST_F(compiler_semantics, assignment_to_an_undeclared_name_is_rejected)
{
  static const oak_case_t cases[] = {
    { "x = 2;\n", "undefined variable 'x'" },
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
    { "let g = 1;\n"
      "fn f() { g = 2; }\n",
      "not visible here (module scope only)" },
    { "record R { x : number;\n"
      "  fn m() -> number { return g; }\n"
      "}\n"
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
      OAK_NULL },
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
    { "return 1;\n", OAK_NULL },
  };

  OAK_EXPECT_REJECTED_CASES(cases);
}

UTEST_F(compiler_semantics, functions_without_an_arrow_are_void)
{
  static const oak_case_t cases[] = {
    { "fn side() { return; }\n"
      "fn main() { side(); }\n"
      "main();\n",
      OAK_NULL },
    { "fn noop() { }\n"
      "noop();\n",
      OAK_NULL },
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
      OAK_NULL },
    /* A trailing comma in the variant list is accepted. */
    { "enum Status { Off, On, }\n"
      "let s = Status.On;\n",
      OAK_NULL },
    /* Two enums keep independent ordinals. */
    { "enum A { X, Y }\n"
      "enum B { P, Q }\n"
      "let x = A.X;\n"
      "let q = B.Q;\n",
      OAK_NULL },
    /* An enum round-trips through a parameter of its own type. */
    { "enum Color { Red, Green, Blue }\n"
      "fn use_color(c : Color) -> Color { return c; }\n"
      "use_color(Color.Blue);\n",
      OAK_NULL },
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

/* Two enums may share a variant name.
 *
 * This follows from the test above: a bare variant name resolves to nothing,
 * so `Left` alone is never a question the compiler has to answer, and
 * `Key.Left` and `MouseButton.Left` are unambiguous. The registry used to
 * reject the pair anyway, which forced any binding that mirrors a real API to
 * rename one of them -- raylib's mouse buttons are Left and Right, and so are
 * its cursor keys. */
UTEST_F(compiler_semantics, two_enums_may_share_a_variant_name)
{
  static const oak_case_t cases[] = {
    { "enum Key { Left, Right }\n"
      "enum MouseButton { Left, Right }\n"
      "print(Key.Left);\n"
      "print(MouseButton.Right);\n",
      OAK_NULL },
    /* Ordinals stay per-enum, so the shared name is the only thing shared. */
    { "enum A { X, Y }\n"
      "enum B { Y, X }\n"
      "fn take_a(v : A) -> A { return v; }\n"
      "take_a(A.Y);\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/* What the relaxation above does not relax. A name repeated inside one enum is
 * still ambiguous, and the two types stay distinct despite the shared spelling
 * -- which is what makes sharing safe rather than merely permitted. */
UTEST_F(compiler_semantics, a_shared_variant_name_stays_scoped_to_its_enum)
{
  static const oak_case_t cases[] = {
    { "enum Key { Left, Left }\n", "duplicate enum variant 'Left'" },
    { "enum Key { Left, Right }\n"
      "enum MouseButton { Left, Right }\n"
      "print(Key.Left == MouseButton.Left);\n",
      "may only be compared to the same enum type" },
    { "enum Key { Left, Right }\n"
      "enum MouseButton { Left, Right }\n"
      "fn press(k : Key) -> Key { return k; }\n"
      "press(MouseButton.Left);\n",
      "expected type 'Key'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* Variants must be comma-separated; whitespace alone does not separate them. */
UTEST_F(compiler_semantics, space_separated_variants_are_rejected)
{
  static const oak_case_t cases[] = {
    { "enum Dir { North South East West }\n"
      "let a = Dir.North;\n",
      OAK_NULL },
  };

  OAK_EXPECT_REJECTED_CASES(cases);
}

/*
 * to_string is a global conversion, and a method only on records.
 *
 * The split is the point. Turning a number, a bool or an array into text is a
 * conversion like to_int, so it sits beside it as a free function; a record's
 * text form is the record's own business, so there it is a method the type may
 * define and the builtin is only the default. It is also the only route from a
 * number to a string, because `+` does not coerce.
 */
UTEST_F(compiler_semantics, to_string_converts_any_value)
{
  static const oak_case_t cases[] = {
    { "print(to_string(5));\n", "5" },
    { "print(to_string(true));\n", "true" },
    { "print(to_string('already'));\n", "already" },
    /* The case the method form could not do: the result is a real string, so
     * it binds and concatenates. */
    { "let s = to_string(42);\n"
      "print('score: ' + s);\n",
      "score: 42" },
    { "print('score: ' + to_string(42));\n", "score: 42" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

UTEST_F(compiler_semantics, records_carry_to_string_as_a_method)
{
  static const oak_case_t cases[] = {
    /* A record may define its own, and it wins over the builtin. */
    { "record P {\n"
      "  x : number;\n"
      "  export fn to_string() -> string { return 'P'; }\n"
      "}\n"
      "let p = new P { x : 1 };\n"
      "print('v=' + p.to_string());\n",
      "v=P" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/*
 * A weak field prints as the identity of its target, never as its contents.
 *
 * A weak reference is there to break an ownership cycle, so it is exactly the
 * edge that points back into the part of the graph already being printed.
 * Following it would print the same records over and over until the depth cap
 * stopped it, so the builtin stops at the edge itself; the strong fields around
 * it still nest as deeply as they go.
 */
UTEST_F(compiler_semantics, to_string_does_not_follow_a_weak_field)
{
  const oak_run_result_t r = oak_test_source(OAK_A,
                                             "record Parent {\n"
                                             "  name : string;\n"
                                             "  child : Child;\n"
                                             "}\n"
                                             "record Child {\n"
                                             "  name : string;\n"
                                             "  parent : Parent weak;\n"
                                             "}\n"
                                             "let c = new Child {\n"
                                             "  name : 'c',\n"
                                             "  parent : none\n"
                                             "};\n"
                                             "let p = new Parent {\n"
                                             "  name : 'p',\n"
                                             "  child : c\n"
                                             "};\n"
                                             "c.parent = p;\n"
                                             "print(to_string(p));\n");
  ASSERT_TRUE(r.compiled);
  ASSERT_EQ(OAK_VM_OK, r.run);
  /* The strong field is still expanded. */
  EXPECT_TRUE(strstr(r.out, "\"child\"") != OAK_NULL);
  EXPECT_TRUE(strstr(r.out, "\"name\": \"c\"") != OAK_NULL);
  /* The weak one names its target instead of repeating it. */
  EXPECT_TRUE(strstr(r.out, "\"parent\": \"<weak Parent @") != OAK_NULL);
  /* One occurrence of the outer record, so nothing walked back around. */
  {
    const char* at = strstr(r.out, "\"name\": \"p\"");
    ASSERT_TRUE(at != OAK_NULL);
    EXPECT_TRUE(strstr(at + 1, "\"name\": \"p\"") == OAK_NULL);
  }
}

/* And nothing else has the method, so the two forms cannot be confused. */
UTEST_F(compiler_semantics, to_string_is_not_a_method_on_non_records)
{
  static const oak_case_t cases[] = {
    { "let n = 5;\n"
      "print(n.to_string());\n",
      "no method 'to_string' on number" },
    { "let b = true;\n"
      "print(b.to_string());\n",
      "no method 'to_string' on bool" },
    { "print('hi'.to_string());\n", "no method 'to_string' on string" },
    { "let a = [1, 2];\n"
      "print(a.to_string());\n",
      "no method 'to_string' on array" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}
