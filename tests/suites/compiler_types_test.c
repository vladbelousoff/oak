/*
 * Compiler: record type declaration, field typing, and weak references.
 *
 * Merges what used to be two files (type binding and field binding). The two
 * halves are the same question asked from either end -- does a declared type
 * flow correctly into literals, parameters, returns, fields, and assignments.
 */

#include "oak_test_support.h"

OAK_TEST_SUITE(compiler_types);

UTEST_F(compiler_types, records_bind_their_name_and_fields)
{
  static const oak_case_t cases[] = {
    { "record Color { r : number; g : number; b : number; }\n"
      "let c = new Color { r : 255, g : 128, b : 0 };\n"
      "print(c.r);\n",
      OAK_NULL },
    { "record Wrapper { value : number; }\n"
      "let w = new Wrapper { value : 42 };\n"
      "print(w.value);\n",
      OAK_NULL },
    /* A record with no fields is declared without a body block. */
    { "record Empty;\n"
      "let e = new Empty {};\n",
      OAK_NULL },
    { "record Named { label : string; }\n"
      "let n = new Named { label : 'hello' };\n"
      "print(n.label);\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/* Records, functions, enums, and module-scope bindings share one namespace. */
UTEST_F(compiler_types, top_level_names_do_not_collide)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; }\n"
      "record Point { y : number; }\n",
      "duplicate record 'Point'" },
    { "record Clash;\n"
      "fn Clash() {}\n",
      "duplicate" },
    { "enum State { Ready }\n"
      "let State = 1;\n",
      "duplicate" },
    { "record Bad { x : number; x : string; }\n",
      "duplicate field 'x' in record 'Bad'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_types, unknown_record_types_are_rejected)
{
  static const oak_case_t cases[] = {
    /* Call position, not a `let` initializer: see the note on
     * compiler_semantics.let_initializers_report_a_generic_diagnostic. */
    { "print(new Ghost { x : 1 });\n", "unknown record type 'Ghost'" },
    { "record Point { x : number; }\n"
      "print(new Ponit { x : 1 });\n",
      "unknown record type 'Ponit'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_types, record_types_flow_through_signatures_and_fields)
{
  static const oak_case_t cases[] = {
    { "record Size { w : number; h : number; }\n"
      "fn area(s : Size) -> number { return s.w * s.h; }\n"
      "let sz = new Size { w : 4, h : 5 };\n"
      "print(area(sz));\n",
      OAK_NULL },
    { "record Pair { a : number; b : number; }\n"
      "fn make_pair(x : number, y : number) -> Pair {\n"
      "  return new Pair { a : x, b : y };\n"
      "}\n"
      "let p = make_pair(3, 7);\n"
      "print(p.a);\n",
      OAK_NULL },
    /* A record may be the declared type of another record's field... */
    { "record Inner { z : number; }\n"
      "record Outer { inner : Inner; }\n"
      "let i = new Inner { z : 9 };\n"
      "let o = new Outer { inner : i };\n"
      "print(o.inner.z);\n",
      OAK_NULL },
    /* ...to arbitrary depth, with the chain typed at every step. */
    { "record A { x : number; }\n"
      "record B { a : A; }\n"
      "record C { b : B; }\n"
      "let a = new A { x : 1 };\n"
      "let b = new B { a : a };\n"
      "let c = new C { b : b };\n"
      "print(c.b.a.x);\n",
      OAK_NULL },
    /* A record-typed field read is itself typed, so it can be passed on. */
    { "record Inner { v : number; }\n"
      "record Outer { inner : Inner; }\n"
      "fn read(i : Inner) -> number { return i.v; }\n"
      "let i = new Inner { v : 3 };\n"
      "let o = new Outer { inner : i };\n"
      "print(read(o.inner));\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/*
 * Record types are nominal, not structural: two records with identical field
 * lists are still different types. This is the single most important property
 * of the type system to keep pinned down.
 */
UTEST_F(compiler_types, identical_layouts_are_still_distinct_types)
{
  static const oak_case_t cases[] = {
    { "record A { x : number; }\n"
      "record B { x : number; }\n"
      "fn take_a(v : A) -> number { return v.x; }\n"
      "let b = new B { x : 7 };\n"
      "take_a(b);\n",
      "expected type 'A', found 'B'" },
    { "record Inner { z : number; }\n"
      "record Other { z : number; }\n"
      "record Outer { inner : Inner; }\n"
      "let o2 = new Other { z : 1 };\n"
      "let bad = new Outer { inner : o2 };\n",
      "expected type 'Inner', got 'Other'" },
    { "record A { v : number; }\n"
      "record B { v : number; }\n"
      "record Outer { a : A; }\n"
      "fn take_b(x : B) -> number { return x.v; }\n"
      "let a = new A { v : 1 };\n"
      "let o = new Outer { a : a };\n"
      "take_b(o.a);\n",
      "expected type 'B', found 'A'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_types, record_literal_fields_are_checked)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "let p = new Point { x : 'bad', y : 1 };\n",
      "expected type 'number', got 'string'" },
    { "record Label { text : string; }\n"
      "let l = new Label { text : 99 };\n",
      "expected type 'string', got 'number'" },
    { "record Point { x : number; }\n"
      "let p = new Point { x : 1, z : 2 };\n",
      "no such field 'z' on record 'Point'" },
    { "record Point { x : number; y : number; }\n"
      "let p = new Point { x : 1, x : 2, y : 3 };\n",
      "duplicate field 'x' in record literal" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_types, field_reads_carry_their_declared_type)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "fn double(n : number) -> number { return n * 2; }\n"
      "let p = new Point { x : 5, y : 6 };\n"
      "print(double(p.x));\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

UTEST_F(compiler_types, unknown_fields_and_non_record_receivers_are_rejected)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "print(p.X);\n",
      "no such field 'X' on record 'Point'" },
    { "let n = 42;\n"
      "print(n.x);\n",
      "requires a record receiver" },
    { "let s = 'hello';\n"
      "print(s.x);\n",
      "requires a record receiver" },
    { "let n = 42;\n"
      "n.x = 1;\n",
      "requires a record receiver" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_types, field_assignment_types_are_checked)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; }\n"
      "let p = new Point { x : 1 };\n"
      "p.x = 'bad';\n",
      "cannot assign value of type 'string' to field 'x'" },
    { "record Named { label : string; }\n"
      "let n = new Named { label : 'ok' };\n"
      "n.label = 42;\n",
      "cannot assign value of type 'number' to field 'label'" },
    { "record A { v : number; }\n"
      "record B { v : number; }\n"
      "record Container { item : A; }\n"
      "let b = new B { v : 1 };\n"
      "let a = new A { v : 2 };\n"
      "let c = new Container { item : a };\n"
      "c.item = b;\n",
      "cannot assign value of type 'B' to field 'item' of type 'A'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_types, field_assignment_on_a_mutable_record_works)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "p.x = 10;\n"
      "print(p.x);\n",
      OAK_NULL },
    { "record Named { label : string; }\n"
      "let n = new Named { label : 'old' };\n"
      "n.label = 'new';\n"
      "print(n.label);\n",
      OAK_NULL },
    /* A chain built entirely from inline literals is fresh all the way down,
     * so no read-only reference is being written through. */
    { "record A { n : number; }\n"
      "record B { a : A; }\n"
      "record C { b : B; }\n"
      "let c = new C { b : new B { a : new A { n : 123 } } };\n"
      "c.b.a.n = 100;\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/*
 * Wrapping an immutable record in a fresh one must not launder it into a
 * mutable reference. The wrapper is a temporary, so it would otherwise count as
 * writable; it is only as writable as what was put inside it.
 */
UTEST_F(compiler_types, a_mutable_owner_does_not_make_its_contents_mutable)
{
  static const oak_case_t cases[] = {
    { "record Inner { v : number; }\n"
      "record Outer { inner : Inner; }\n"
      "fn read(i : Inner) {\n"
      "  let o = new Outer { inner : i };\n"
      "  o.inner.v = 99;\n"
      "}\n",
      "immutable" },
    { "record Foo { abc : number; }\n"
      "record Bar { foo : Foo; }\n"
      "fn read(foo : Foo) {\n"
      "  let bar = new Bar { foo : foo };\n"
      "  bar.foo.abc = 100;\n"
      "}\n",
      "immutable" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_types, weak_annotates_fields_and_parameters)
{
  static const oak_case_t cases[] = {
    { "record Node { value : number; }\n"
      "record Link { target : Node weak; }\n"
      "fn read(target : Node weak) -> number {\n"
      "  return target.value;\n"
      "}\n"
      "let n = new Node { value : 7 };\n"
      "let l = new Link { target : n };\n"
      "print(read(n));\n"
      "print(l.target.value);\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/*
 * A weak field never keeps its target alive, so it must be given something
 * that is already owned elsewhere -- a temporary would be destroyed
 * immediately, leaving the field dangling on the next line.
 */
UTEST_F(compiler_types, weak_rejects_temporaries_and_does_not_satisfy_strong)
{
  static const oak_case_t cases[] = {
    { "record Node { value : number; }\n"
      "record Link { target : Node weak; }\n"
      "let l = new Link { target : new Node { value : 1 } };\n",
      "cannot create weak reference from a temporary value" },
    { "record Node { value : number; }\n"
      "record Link { target : Node weak; }\n"
      "fn read(target : Node) -> number { return target.value; }\n"
      "let n = new Node { value : 7 };\n"
      "let l = new Link { target : n };\n"
      "print(read(l.target));\n",
      "expected type" },
    { "record R { s : string weak; }\n", "weak cannot be applied to strings" },
    { "record R { n : number weak; }\n",
      "weak can only be applied to refcounted types" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* The record body is where methods live: a `static` one alongside an instance
 * one, both reached through the type they are declared in. */
UTEST_F(compiler_types, functions_inside_a_record_body_are_methods)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number;\n"
      "  fn static origin() -> Point {\n"
      "    return new Point { x : 0, y : 0 };\n"
      "  }\n"
      "  fn dup() -> number { return self.x; }\n"
      "}\n"
      "print(Point.origin().dup());\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/* The `export` and `@Attr` wrappers accept any declaration, so a nested record
 * or enum reaches the compiler and has to be turned away there rather than by
 * the grammar. */
UTEST_F(compiler_types, a_record_body_holds_only_fields_and_methods)
{
  static const oak_case_t cases[] = {
    { "record P { x : number;\n"
      "  export enum E { A }\n"
      "}\n",
      "move this declaration out of record 'P'" },
    { "record P { x : number;\n"
      "  export record Q { y : number; }\n"
      "}\n",
      "move this declaration out of record 'P'" },
    /* An empty body is still a mistake; `record P;` is how you spell it. */
    { "record P {}\n", "use 'record P;' instead of '{}'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* The two spellings a pre-methods-in-records program uses. Both are dead
 * grammar, so the token-level "expected X, got Y" would say nothing about why;
 * these pin the hints that replace it. */
UTEST_F(compiler_types, the_old_method_syntax_explains_itself)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; }\n"
      "fn Point.dup(self) -> number { return self.x; }\n",
      "methods are declared inside their record" },
    { "record Point { x : number;\n"
      "  fn dup(self) -> number { return self.x; }\n"
      "}\n",
      "the receiver is not a parameter" },
    { "record Point { x : number;\n"
      "  fn bump(mut self, d : number) { self.x += d; }\n"
      "}\n",
      "'fn mut name()' for a mutable one" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}
