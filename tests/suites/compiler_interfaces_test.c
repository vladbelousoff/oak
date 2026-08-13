/*
 * Compiler: interface conformance and virtual dispatch.
 *
 * Conformance is checked at the coercion site -- where a record value is
 * passed to something typed as the interface -- not at the record's
 * declaration. That is why every negative case below has to actually pass the
 * record somewhere before the error appears.
 */

#include "oak_test_support.h"

OAK_TEST_SUITE(compiler_interfaces);

#define INTERFACE_SHAPE                                                        \
  "interface IShape {\n"                                                       \
  "  fn area(self) -> number;\n"                                               \
  "  fn describe(self) -> string;\n"                                           \
  "}\n"

#define RECORD_CIRCLE                                                          \
  "record Circle { radius : number; }\n"                                       \
  "fn Circle.area(self) -> number { return self.radius * self.radius; }\n"     \
  "fn Circle.describe(self) -> string { return 'circle'; }\n"

/* ------------------------------------------------------------------ */
/* Conformance and dispatch that must work                             */
/* ------------------------------------------------------------------ */

UTEST_F(compiler_interfaces, conforming_records_coerce_and_dispatch)
{
  static const oak_case_t cases[] = {
    { INTERFACE_SHAPE RECORD_CIRCLE
      "fn use_shape(s: IShape) { print(s.area()); }\n"
      "let c = new Circle { radius: 3 };\n"
      "use_shape(c);\n",
      null },
    { INTERFACE_SHAPE RECORD_CIRCLE
      "fn use_shape(s: IShape) { print(s.describe()); print(s.area()); }\n"
      "let c = new Circle { radius: 2 };\n"
      "use_shape(c);\n",
      null },
    /* An already-coerced interface object passes straight through. */
    { INTERFACE_SHAPE RECORD_CIRCLE
      "fn inner(s: IShape) { print(s.area()); }\n"
      "fn outer(s: IShape) { inner(s); }\n"
      "let c = new Circle { radius: 1 };\n"
      "outer(c);\n",
      null },
    /* Coercion works for method arguments too, not just free functions. */
    { INTERFACE_SHAPE RECORD_CIRCLE
      "record Renderer;\n"
      "fn Renderer.render(self, s: IShape) { print(s.area()); }\n"
      "let r = new Renderer {};\n"
      "let c = new Circle { radius: 4 };\n"
      "r.render(c);\n",
      null },
  };

  OAK_EXPECT_OK_CASES(cases);
}

UTEST_F(compiler_interfaces, interface_arrays_hold_mixed_implementations)
{
  static const oak_case_t cases[] = {
    { INTERFACE_SHAPE RECORD_CIRCLE
      "record Rect { w : number; h : number; }\n"
      "fn Rect.area(self) -> number { return self.w * self.h; }\n"
      "fn Rect.describe(self) -> string { return 'rect'; }\n"
      "let mut shapes = new IShape[];\n"
      "shapes.push(new Circle { radius: 3 });\n"
      "shapes.push(new Rect { w: 2, h: 5 });\n"
      "let mut total = 0;\n"
      "for s in shapes { total += s.area(); }\n"
      "print(total);\n",
      null },
    { INTERFACE_SHAPE RECORD_CIRCLE
      "let mut shapes = new IShape[];\n"
      "shapes.push(new Circle { radius: 4 });\n"
      "print(shapes[0].area());\n",
      null },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/* ------------------------------------------------------------------ */
/* Conformance failures                                                */
/* ------------------------------------------------------------------ */

UTEST_F(compiler_interfaces, non_conforming_records_are_rejected_at_coercion)
{
  static const oak_case_t cases[] = {
    /* No methods at all. */
    { INTERFACE_SHAPE
      "record Bare { x : number; }\n"
      "fn use_shape(s: IShape) { print(s.area()); }\n"
      "let b = new Bare { x: 1 };\n"
      "use_shape(b);\n",
      "does not implement interface 'IShape'" },
    /* Right name, wrong return type. */
    { "interface IShape { fn area(self) -> number; }\n"
      "record Bad;\n"
      "fn Bad.area(self) -> string { return 'oops'; }\n"
      "fn use_shape(s: IShape) { print(s.area()); }\n"
      "let b = new Bad {};\n"
      "use_shape(b);\n",
      "does not implement interface 'IShape'" },
    /* Right name, wrong parameter type. */
    { "interface IScalable { fn scale(self, factor: number) -> number; }\n"
      "record Bad { v : number; }\n"
      "fn Bad.scale(self, factor: string) -> number { return self.v; }\n"
      "fn use_it(s: IScalable) { print(s.scale(2)); }\n"
      "let b = new Bad { v: 1 };\n"
      "use_it(b);\n",
      "does not implement interface 'IScalable'" },
    /* Right name, wrong arity. */
    { "interface IShape { fn area(self) -> number; }\n"
      "record Bad;\n"
      "fn Bad.area(self, extra: number) -> number { return extra; }\n"
      "fn use_shape(s: IShape) { print(s.area()); }\n"
      "let b = new Bad {};\n"
      "use_shape(b);\n",
      "does not implement interface 'IShape'" },
    /* Pushing into an interface array is a coercion site as well. */
    { INTERFACE_SHAPE
      "record Plain { x : number; }\n"
      "let mut shapes = new IShape[];\n"
      "shapes.push(new Plain { x: 1 });\n",
      "does not implement interface 'IShape'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/*
 * A default body supplies an implementation for callers of the interface, but
 * it does NOT make a record conform -- the record must still declare the
 * method itself. Pinned deliberately: if defaults are ever auto-filled into
 * vtables this test fails, and that should be a conscious decision rather
 * than a silent change.
 */
UTEST_F(compiler_interfaces, a_default_body_does_not_satisfy_conformance)
{
  static const oak_case_t missing[] = {
    { "interface IGreet {\n"
      "  fn name(self) -> string;\n"
      "  fn greet(self) -> string { return 'hi ' + self.name(); }\n"
      "}\n"
      "record Person { n : string; }\n"
      "fn Person.name(self) -> string { return self.n; }\n"
      /* greet() deliberately not implemented */
      "fn use_greet(g : IGreet) { print(g.greet()); }\n"
      "let p = new Person { n: 'ada' };\n"
      "use_greet(p);\n",
      "does not implement interface 'IGreet'" },
  };

  static const oak_case_t implemented[] = {
    { "interface IGreet {\n"
      "  fn name(self) -> string;\n"
      "  fn greet(self) -> string { return 'hello ' + self.name(); }\n"
      "}\n"
      "record Person { n : string; }\n"
      "fn Person.name(self) -> string { return self.n; }\n"
      "fn Person.greet(self) -> string { return 'hello ' + self.n; }\n"
      "let p = new Person { n: 'world' };\n"
      "print(p.greet());\n",
      null },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(missing);
  OAK_EXPECT_OK_CASES(implemented);
}

/* ------------------------------------------------------------------ */
/* Virtual call checking                                               */
/* ------------------------------------------------------------------ */

/* Calls through an interface are checked against the interface's declaration,
 * not against whatever the concrete record happens to offer. */
UTEST_F(compiler_interfaces, virtual_calls_are_checked_against_the_interface)
{
  static const oak_case_t cases[] = {
    { INTERFACE_SHAPE RECORD_CIRCLE
      "fn use_shape(s: IShape) { print(s.missing()); }\n"
      "let c = new Circle { radius: 1 };\n"
      "use_shape(c);\n",
      "interface 'IShape' has no method 'missing'" },
    { "interface IScalable { fn scale(self, factor: number) -> number; }\n"
      "record Box { size : number; }\n"
      "fn Box.scale(self, factor: number) -> number { return self.size * factor; }\n"
      "fn use_it(s: IScalable) { print(s.scale('bad')); }\n"
      "let b = new Box { size: 5 };\n"
      "use_it(b);\n",
      "expected type 'number'" },
    { "interface IScalable { fn scale(self, factor: number) -> number; }\n"
      "record Box { size : number; }\n"
      "fn Box.scale(self, factor: number) -> number { return self.size * factor; }\n"
      "fn use_it(s: IScalable) { print(s.scale()); }\n"
      "let b = new Box { size: 5 };\n"
      "use_it(b);\n",
      "expects 1 arguments, got 0" },
    { "interface IScalable { fn scale(self, factor: number) -> number; }\n"
      "record Box { size : number; }\n"
      "fn Box.scale(self, factor: number) -> number { return self.size * factor; }\n"
      "fn use_it(s: IScalable) { print(s.scale(1, 2)); }\n"
      "let b = new Box { size: 5 };\n"
      "use_it(b);\n",
      "expects 1 arguments, got 2" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* ------------------------------------------------------------------ */
/* Declaration rules                                                   */
/* ------------------------------------------------------------------ */

UTEST_F(compiler_interfaces, interface_declarations_are_validated)
{
  static const oak_case_t cases[] = {
    { "interface IShape { fn area(self) -> number; }\n"
      "interface IShape { fn area(self) -> number; }\n",
      "duplicate interface 'IShape'" },
    /* Interface names carry an `I` prefix by language rule, not convention. */
    { "interface Shape { fn area(self) -> number; }\n",
      "interface names must start with 'I'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}
