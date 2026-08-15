/*
 * Compiler: the const/mut binding model.
 *
 * The rule that ties this suite together: `mut` describes what may be written
 * through a reference, and immutability is not launderable. An immutable
 * reference cannot become a mutable one by being copied into a `let mut`,
 * passed to a `mut` parameter, stored in a mutable record's field, or pushed
 * into a mutable collection. Value types (numbers, bools) are exempt because
 * they are copied, not referenced.
 */

#include "oak_test_support.h"

OAK_TEST_SUITE(compiler_mut);

/* Copying out of an immutable refcounted source would alias it mutably. */
UTEST_F(compiler_mut, mut_binding_from_an_immutable_reference_is_rejected)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "let mut copy = p;\n",
      "cannot store immutable reference in mutable binding" },
    { "record Inner { z : number; }\n"
      "record Outer { inner : Inner; }\n"
      "let inner = new Inner { z : 7 };\n"
      "let outer = new Outer { inner : inner };\n"
      "let mut copy = outer.inner;\n",
      "cannot store immutable reference in mutable binding" },
    { "record A { x : number; }\n"
      "record B { a : A; }\n"
      "record C { b : B; }\n"
      "let a = new A { x : 1 };\n"
      "let b = new B { a : a };\n"
      "let c = new C { b : b };\n"
      "let mut copy = c.b;\n",
      "cannot store immutable reference in mutable binding" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* Scalars are copied rather than referenced, so their source's mutability is
 * irrelevant -- as are rvalues, which nothing else can be holding. */
UTEST_F(compiler_mut, mut_binding_from_a_copy_or_an_rvalue_is_fine)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "let p = new Point { x : 3, y : 4 };\n"
      "let mut x = p.x;\n"
      "x = 99;\n",
      OAK_NULL },
    { "let arr = [1, 2, 3];\n"
      "let mut x = arr[0];\n"
      "x = 42;\n",
      OAK_NULL },
    { "let mut x = 42;\n"
      "x = x + 1;\n",
      OAK_NULL },
    { "fn make_num() -> number { return 10; }\n"
      "let mut x = make_num();\n"
      "x = x * 2;\n",
      OAK_NULL },
    /* A mutable source may be bound mutably. */
    { "record Point { x : number; y : number; }\n"
      "let mut p = new Point { x : 1, y : 2 };\n"
      "let mut copy = p;\n",
      OAK_NULL },
    { "record Inner { z : number; }\n"
      "record Outer { inner : Inner; }\n"
      "let mut inner = new Inner { z : 7 };\n"
      "let mut outer = new Outer { inner : inner };\n"
      "let mut copy = outer.inner;\n",
      OAK_NULL },
    /* Narrowing to immutable is always allowed. */
    { "record Point { x : number; y : number; }\n"
      "let p = new Point { x : 3, y : 4 };\n"
      "let x = p.x;\n",
      OAK_NULL },
    { "record Point { x : number; }\n"
      "let mut a = new Point { x : 1 };\n"
      "let r = a;\n"
      "a.x = 99;\n"
      "print(r.x);\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

UTEST_F(compiler_mut, fields_of_an_immutable_record_cannot_be_assigned)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "p.x = 99;\n",
      "cannot assign to field 'x' of immutable record" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_mut, fields_of_a_mutable_record_can_be_assigned)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "let mut p = new Point { x : 1, y : 2 };\n"
      "p.x = 99;\n"
      "print(p.x);\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/*
 * The containers are the interesting case: an immutable reference must not
 * become writable by being stored somewhere mutable. Each of these would
 * otherwise be a hole in the model.
 */
UTEST_F(compiler_mut, immutable_references_cannot_be_stored_in_mutable_places)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; }\n"
      "let a = new Point { x : 1 };\n"
      "let mut b = new Point { x : 2 };\n"
      "b = a;\n",
      "cannot store immutable reference in mutable" },
    { "record Inner { v : number; }\n"
      "record Outer { inner : Inner; }\n"
      "let i = new Inner { v : 7 };\n"
      "let mut o = new Outer { inner : new Inner { v : 0 } };\n"
      "o.inner = i;\n",
      "cannot store immutable reference in mutable" },
    { "record Point { x : number; }\n"
      "let p = new Point { x : 1 };\n"
      "let mut points = new Point[];\n"
      "points.push(p);\n",
      "cannot store immutable reference in mutable" },
    { "record Point { x : number; }\n"
      "let p = new Point { x : 1 };\n"
      "let mut points = new [string:Point];\n"
      "points['p'] = p;\n",
      "cannot store immutable reference in mutable" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_mut, mut_parameters_require_a_mutable_argument)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "fn move_point(mut p : Point) -> number { return p.x; }\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "move_point(p);\n",
      "cannot pass an immutable value to a mutable parameter" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_mut, mut_parameters_accept_mutable_and_copied_arguments)
{
  static const oak_case_t cases[] = {
    { "record Point { x : number; y : number; }\n"
      "fn move_point(mut p : Point) -> number { return p.x; }\n"
      "let mut p = new Point { x : 1, y : 2 };\n"
      "move_point(p);\n",
      OAK_NULL },
    /* A number is copied into the parameter, so the caller's binding does not
     * need to be mutable. */
    { "fn double(mut x : number) -> number { x = x * 2; return x; }\n"
      "let n = 5;\n"
      "print(double(n));\n",
      OAK_NULL },
    /* `mut` governs writability, not ownership, so the same binding may be
     * passed to several parameters at once. */
    { "record Point { x : number; y : number; }\n"
      "fn add_y(mut a : Point, b : Point) { a.y = a.y + b.y; }\n"
      "let mut p = new Point { x : 1, y : 2 };\n"
      "add_y(p, p);\n",
      OAK_NULL },
    { "record Point { x : number; y : number; }\n"
      "fn swap(mut a : Point, mut b : Point) {\n"
      "  let tmp = a.x;\n"
      "  a.x = b.x;\n"
      "  b.x = tmp;\n"
      "}\n"
      "let mut p = new Point { x : 1, y : 2 };\n"
      "let mut q = new Point { x : 3, y : 4 };\n"
      "swap(p, q);\n",
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
      "let p = new Point { x : 1, y : 2 };\n"
      "p.shift(3, 4);\n",
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
      "let mut p = new Point { x : 1, y : 2 };\n"
      "print(p.shift(3, 4));\n",
      OAK_NULL },
    /* A read-only method is callable on an immutable receiver. */
    { "record Point { x : number; y : number;\n"
      "  fn sum() -> number { return self.x + self.y; }\n"
      "}\n"
      "let p = new Point { x : 3, y : 4 };\n"
      "print(p.sum());\n",
      OAK_NULL },
  };

  OAK_EXPECT_OK_CASES(cases);
}
