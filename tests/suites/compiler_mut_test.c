/*
 * Compiler: the read-only/read-write access model.
 *
 * The rule that ties this suite together: a binding declares no access of its
 * own -- it inherits whatever its initializer already grants -- and access is
 * never launderable. Read-only access originates at a parameter or receiver
 * declared without `mut`, and everything reached from one stays read-only: it
 * cannot become writable by being copied into a binding, passed to a `mut`
 * parameter, stored in a writable record's field, or pushed into a writable
 * collection. Value types (numbers, bools) are exempt because they are copied,
 * not referenced.
 *
 * A fresh value (`new T { ... }`, a literal, a call result) is writable, since
 * nothing else can be holding it yet.
 */

#include "oak_test_support.h"

OAK_TEST_SUITE(compiler_mut);

/* Copying out of a read-only source must not widen it. The copy compiles --
 * there is nothing wrong with naming the value -- and the write through it is
 * what fails. */
UTEST_F(compiler_mut, a_binding_inherits_its_sources_access)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "fn read(p : Point) {\n"
      "  let copy = p;\n"
      "  copy.x = 99;\n"
      "}\n",
      "cannot assign to field 'x' of immutable record" },
    { "record Inner { z : number; }\n"
      "record Outer { inner : Inner; }\n"
      "fn read(outer : Outer) {\n"
      "  let copy = outer.inner;\n"
      "  copy.z = 99;\n"
      "}\n",
      "cannot assign to field 'z' of immutable record" },
    { "record A { x : number; }\n"
      "record B { a : A; }\n"
      "record C { b : B; }\n"
      "fn read(c : C) {\n"
      "  let copy = c.b;\n"
      "  copy.a.x = 99;\n"
      "}\n",
      "cannot assign to field 'x' of immutable record" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* An enum variant is an integer constant, so it belongs with the scalars the
 * access model exempts rather than with the heap values it protects. It is
 * singled out here because its type id sits in the same range as a record's,
 * which is the one thing that could make it look like a reference. */
UTEST_F(compiler_mut, an_enum_is_a_value_and_never_an_immutable_reference)
{
  static const oak_case_t cases[] = {
    { "enum Mode { A, B }\n"
      "record R { m : Mode; }\n"
      "let r = new R { m : Mode.A };\n"
      "r.m = Mode.B;\n"
      "print(r.m);\n",
      OAK_NULL },
    /* Reaching the variant through a binding must not change the answer. */
    { "enum Mode { A, B }\n"
      "record R { m : Mode; }\n"
      "let r = new R { m : Mode.A };\n"
      "let next = Mode.B;\n"
      "r.m = next;\n"
      "print(r.m);\n",
      OAK_NULL },
    /* Nor must reading it out of a read-only parameter: storing a real
       reference from `src` into a writable record is exactly what the model
       forbids, and an enum has to be exempt from that. */
    { "enum Mode { A, B }\n"
      "record R { m : Mode; }\n"
      "fn copy_mode(src : R, mut dst : R) {\n"
      "  dst.m = src.m;\n"
      "}\n"
      "let a = new R { m : Mode.B };\n"
      "let b = new R { m : Mode.A };\n"
      "copy_mode(a, b);\n"
      "print(b.m);\n",
      OAK_NULL },
    { "enum Mode { A, B }\n"
      "let modes = [ Mode.A ];\n"
      "modes[0] = Mode.B;\n"
      "print(modes[0]);\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/* A function is a heap object, and so refcounted, but nothing can be written
 * through one: it has no fields, no elements, and no method that mutates it.
 * Storing a named function in a record field is how a callback is registered,
 * and the access model has no reason to stand in the way of it. */
UTEST_F(compiler_mut, a_function_is_not_mutable_state)
{
  static const oak_case_t cases[] = {
    { "record D { cb : () -> void; }\n"
      "fn hi() { print('hi'); }\n"
      "let d = new D { cb : hi };\n"
      "d.cb = hi;\n"
      "print('ok');\n",
      OAK_NULL },
    /* A literal holding a named function stays writable, where a literal
       holding a read-only record reference would not. */
    { "record D { cb : () -> void; n : number; }\n"
      "fn hi() { print('hi'); }\n"
      "let d = new D { cb : hi, n : 0 };\n"
      "d.n = 1;\n"
      "print(d.n);\n",
      OAK_NULL },
    /* And it may be handed to a `mut` parameter. */
    { "record D { cb : () -> void; }\n"
      "fn hi() { print('hi'); }\n"
      "fn install(mut d : D, f : () -> void) { d.cb = f; }\n"
      "let d = new D { cb : hi };\n"
      "install(d, hi);\n"
      "print('ok');\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/* Scalars are copied rather than referenced, so their source's access is
 * irrelevant -- as are fresh values, which nothing else can be holding. */
UTEST_F(compiler_mut, a_binding_from_a_copy_or_a_fresh_value_is_writable)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "fn read(p : Point) -> number {\n"
      "  let x = p.x;\n"
      "  x = 99;\n"
      "  return x;\n"
      "}\n"
      "print(read(new Point { x : 3, y : 4 }));\n",
      OAK_NULL },
    { "fn read(arr : number[]) -> number {\n"
      "  let x = arr[0];\n"
      "  x = 42;\n"
      "  return x;\n"
      "}\n"
      "print(read([1, 2, 3]));\n",
      OAK_NULL },
    { "let x = 42;\n"
      "x = x + 1;\n"
      "print(x);\n",
      OAK_NULL },
    { "fn make_num() -> number { return 10; }\n"
      "let x = make_num();\n"
      "x = x * 2;\n"
      "print(x);\n",
      OAK_NULL },
    /* A writable source stays writable however many times it is renamed. */
    { "record Point { x : number; y : number; }\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "let copy = p;\n"
      "copy.x = 99;\n"
      "print(p.x);\n",
      OAK_NULL },
    { "record Inner { z : number; }\n"
      "record Outer { inner : Inner; }\n"
      "let outer = new Outer { inner : new Inner { z : 7 } };\n"
      "let copy = outer.inner;\n"
      "copy.z = 8;\n"
      "print(outer.inner.z);\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

UTEST_F(compiler_mut, fields_of_an_immutable_record_cannot_be_assigned)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "fn read(p : Point) { p.x = 99; }\n",
      "cannot assign to field 'x' of immutable record" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_mut, fields_of_a_mutable_record_can_be_assigned)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "p.x = 99;\n"
      "print(p.x);\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/*
 * The containers are the interesting case: a read-only reference must not
 * become writable by being stored somewhere writable. Each of these would
 * otherwise be a hole in the model.
 */
UTEST_F(compiler_mut, immutable_references_cannot_be_stored_in_mutable_places)
{
  static const oak_case_t cases[] = {
    /* Rebinding is allowed, but not when it would widen what the slot grants:
     * `b` was declared from a fresh value, so it is writable. */
    { "record Point { x : number; }\n"
      "fn read(a : Point) {\n"
      "  let b = new Point { x : 2 };\n"
      "  b = a;\n"
      "}\n",
      "cannot store immutable reference in mutable" },
    { "record Inner { v : number; }\n"
      "record Outer { inner : Inner; }\n"
      "fn read(i : Inner) {\n"
      "  let o = new Outer { inner : new Inner { v : 0 } };\n"
      "  o.inner = i;\n"
      "}\n",
      "cannot store immutable reference in mutable" },
    { "record Point { x : number; }\n"
      "fn read(p : Point) {\n"
      "  let points = new Point[];\n"
      "  points.push(p);\n"
      "}\n",
      "cannot store immutable reference in mutable" },
    { "record Point { x : number; }\n"
      "fn read(p : Point) {\n"
      "  let points = new [string:Point];\n"
      "  points['p'] = p;\n"
      "}\n",
      "cannot store immutable reference in mutable" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* A literal is a temporary, so nothing else holds it -- but writing through it
 * reaches whatever was put inside. Building one over a read-only reference
 * therefore yields a read-only literal rather than an error: the diagnostic
 * arrives at the write, like every other narrowing. */
UTEST_F(compiler_mut, a_literal_is_only_as_writable_as_its_contents)
{
  static const oak_case_t error_cases[] = {
    { "record Point { x : number; }\n"
      "fn read(p : Point) {\n"
      "  let arr = [p];\n"
      "  arr[0].x = 9;\n"
      "}\n",
      "cannot assign to field 'x' of immutable record" },
    { "record Point { x : number; }\n"
      "record Holder { p : Point; }\n"
      "fn read(p : Point) {\n"
      "  let h = new Holder { p : p };\n"
      "  h.p.x = 9;\n"
      "}\n",
      "cannot assign to field 'x' of immutable record" },
    { "record Point { x : number; }\n"
      "fn read(p : Point) {\n"
      "  let m = ['a' : p];\n"
      "  m['a'].x = 9;\n"
      "}\n",
      "cannot assign to field 'x' of immutable record" },
    /* A literal nested inside another must not launder either. */
    { "record Point { x : number; }\n"
      "record Holder { points : Point[]; }\n"
      "fn read(p : Point) {\n"
      "  let h = new Holder { points : [p] };\n"
      "  h.points[0].x = 9;\n"
      "}\n",
      "cannot assign to field 'x' of immutable record" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(error_cases);

  /* Scalars read out of a read-only record are copies, so they do not make the
   * literal that holds them read-only. */
  static const oak_case_t ok_cases[] = {
    { "record Point { x : number; y : number; }\n"
      "fn read(p : Point) -> number {\n"
      "  let coords = [p.x, p.y];\n"
      "  coords[0] = 99;\n"
      "  return coords[0];\n"
      "}\n"
      "print(read(new Point { x : 1, y : 2 }));\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(ok_cases);
}

/* Rebinding a local points this frame's slot somewhere else, which no other
 * holder can observe, so it needs no permission of its own. */
UTEST_F(compiler_mut, rebinding_a_local_is_always_allowed)
{
  static const oak_case_t cases[] = {
    { "let x = 1;\n"
      "x = 2;\n"
      "print(x);\n",
      OAK_NULL },
    /* A read-only slot may be rebound; it just stays read-only. */
    { "record Point { x : number; }\n"
      "fn read(a : Point, b : Point) -> number {\n"
      "  let p = a;\n"
      "  p = b;\n"
      "  return p.x;\n"
      "}\n"
      "print(read(new Point { x : 1 }, new Point { x : 2 }));\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

UTEST_F(compiler_mut, mut_parameters_require_a_mutable_argument)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "fn move_point(mut p : Point) -> number { return p.x; }\n"
      "fn read(p : Point) { move_point(p); }\n",
      "cannot pass an immutable value to a mutable parameter" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_mut, mut_parameters_accept_mutable_and_copied_arguments)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "fn move_point(mut p : Point) -> number { return p.x; }\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "print(move_point(p));\n",
      OAK_NULL },
    /* A number is copied into the parameter, so the caller's binding does not
     * need to be writable. */
    { "fn double(mut x : number) -> number { x = x * 2; return x; }\n"
      "let n = 5;\n"
      "print(double(n));\n",
      OAK_NULL },
    /* `mut` governs writability, not ownership, so the same binding may be
     * passed to several parameters at once. */
    { "record Point { x : number; y : number; }\n"
      "fn add_y(mut a : Point, b : Point) { a.y = a.y + b.y; }\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "add_y(p, p);\n"
      "print(p.y);\n",
      OAK_NULL },
    { "record Point { x : number; y : number; }\n"
      "fn swap(mut a : Point, mut b : Point) {\n"
      "  let tmp = a.x;\n"
      "  a.x = b.x;\n"
      "  b.x = tmp;\n"
      "}\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "let q = new Point { x : 3, y : 4 };\n"
      "swap(p, q);\n"
      "print(p.x);\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

UTEST_F(compiler_mut, mut_self_methods_require_a_mutable_receiver)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number;\n"
      "  fn mut shift(dx : number, dy : number) -> number {\n"
      "    self.x = self.x + dx;\n"
      "    self.y = self.y + dy;\n"
      "    return self.x + self.y;\n"
      "  }\n"
      "}\n"
      "fn read(p : Point) { p.shift(3, 4); }\n",
      "cannot call mutable method on an immutable receiver" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_mut, self_methods_match_their_receivers_mutability)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number;\n"
      "  fn mut shift(dx : number, dy : number) -> number {\n"
      "    self.x = self.x + dx;\n"
      "    self.y = self.y + dy;\n"
      "    return self.x + self.y;\n"
      "  }\n"
      "}\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "print(p.shift(3, 4));\n",
      OAK_NULL },
    /* A read-only method is callable on a read-only receiver. */
    { "record Point { x : number; y : number;\n"
      "  fn sum() -> number { return self.x + self.y; }\n"
      "}\n"
      "fn read(p : Point) -> number { return p.sum(); }\n"
      "print(read(new Point { x : 3, y : 4 }));\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/* A loop variable is a reference into the collection, so it carries the same
 * access. Before access was inherited there was no way to spell this: loop
 * variables were always read-only, and elements could not be updated in
 * place. */
UTEST_F(compiler_mut, loop_variables_inherit_the_collections_access)
{
  static const oak_case_t error_cases[] = {
    { "record Point { x : number;\n"
      "  fn mut bump() { self.x = self.x + 1; }\n"
      "}\n"
      "fn read(points : Point[]) {\n"
      "  for p in points { p.bump(); }\n"
      "}\n",
      "cannot call mutable method on an immutable receiver" },
    { "record Point { x : number; }\n"
      "fn read(points : Point[]) {\n"
      "  for p in points { p.x = 9; }\n"
      "}\n",
      "cannot assign to field 'x' of immutable record" },
    /* Map values are places in the collection too. */
    { "record Point { x : number; }\n"
      "fn read(points : [string:Point]) {\n"
      "  for k, p in points { p.x = 9; }\n"
      "}\n",
      "cannot assign to field 'x' of immutable record" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(error_cases);

  static const oak_case_t ok_cases[] = {
    { "record Point { x : number;\n"
      "  fn mut bump() { self.x = self.x + 1; }\n"
      "}\n"
      "let points = [new Point { x : 1 }, new Point { x : 2 }];\n"
      "for p in points { p.bump(); }\n"
      "for p in points { print(p.x); }\n",
      OAK_NULL },
    { "record Point { x : number; }\n"
      "let points = new [string:Point];\n"
      "points['a'] = new Point { x : 1 };\n"
      "for k, p in points { p.x = 9; }\n"
      "print(points['a'].x);\n",
      OAK_NULL },
    /* A `mut` parameter hands the body writable elements. */
    { "record Point { x : number; }\n"
      "fn bump_all(mut points : Point[]) {\n"
      "  for p in points { p.x = p.x + 1; }\n"
      "}\n"
      "let points = [new Point { x : 1 }];\n"
      "bump_all(points);\n"
      "print(points[0].x);\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(ok_cases);
}
