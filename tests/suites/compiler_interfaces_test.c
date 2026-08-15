/*
 * Compiler: interface conformance and virtual dispatch.
 *
 * Records explicitly declare their interfaces, and the compiler validates the
 * full method contract as soon as all method declarations are registered.
 */

#include "oak_test_support.h"

OAK_TEST_SUITE(compiler_interfaces);

#define INTERFACE_SHAPE                                                        \
  "interface IShape {\n"                                                       \
  "  fn area() -> number;\n"                                                   \
  "  fn describe() -> string;\n"                                               \
  "}\n"

#define RECORD_CIRCLE                                                          \
  "record Circle implements IShape {\n"                                        \
  "  radius : number;\n"                                                       \
  "  fn area() -> number { return self.radius * self.radius; }\n"              \
  "  fn describe() -> string { return 'circle'; }\n"                           \
  "}\n"

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
      "record Renderer {\n"
      "  fn render(s: IShape) { print(s.area()); }\n"
      "}\n"
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
      "record Rect implements IShape {\n"
      "  w : number;\n"
      "  h : number;\n"
      "  fn area() -> number { return self.w * self.h; }\n"
      "  fn describe() -> string { return 'rect'; }\n"
      "}\n"
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

/* A declared interface is checked at the declaration, so the error arrives
 * whether or not the record is ever coerced -- and it names the method that
 * falls short rather than leaving the reader to diff two signatures. */
UTEST_F(compiler_interfaces, declared_interfaces_are_validated)
{
  static const oak_case_t cases[] = {
    /* No methods at all, and never used: declaring the interface is enough. */
    { INTERFACE_SHAPE
      "record Bare implements IShape { x : number; }\n",
      "record 'Bare' does not implement interface 'IShape': no method 'area'" },
    /* Right name, wrong return type. */
    { "interface IShape { fn area() -> number; }\n"
      "record Bad implements IShape {\n"
      "  fn area() -> string { return 'oops'; }\n"
      "}\n",
      "method 'area' returns 'string', interface declares 'number'" },
    /* Right name, wrong parameter type. */
    { "interface IScalable { fn scale(factor: number) -> number; }\n"
      "record Bad implements IScalable {\n"
      "  v : number;\n"
      "  fn scale(factor: string) -> number { return self.v; }\n"
      "}\n",
      "method 'scale' takes 'string' for parameter 1, "
      "interface declares 'number'" },
    /* Right name, wrong arity. */
    { "interface IShape { fn area() -> number; }\n"
      "record Bad implements IShape {\n"
      "  fn area(extra: number) -> number { return extra; }\n"
      "}\n",
      "method 'area' takes 1 parameter, interface declares 0" },
    { "interface IShape { fn area(scale: number) -> number; }\n"
      "record Bad implements IShape {\n"
      "  fn area() -> number { return 1; }\n"
      "}\n",
      "method 'area' takes 0 parameters, interface declares 1" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* Conformance is declared, never inferred. A record with every method the
 * interface asks for is still not an implementation until it says so, and the
 * coercion site is where that shows up -- so the error points the reader at
 * the clause to add. */
UTEST_F(compiler_interfaces, a_structural_match_is_not_an_implementation)
{
  static const oak_case_t cases[] = {
    { INTERFACE_SHAPE
      "record Circle {\n"
      "  radius : number;\n"
      "  fn area() -> number { return self.radius; }\n"
      "  fn describe() -> string { return 'circle'; }\n"
      "}\n"
      "fn use_shape(s: IShape) { print(s.area()); }\n"
      "let c = new Circle { radius: 3 };\n"
      "use_shape(c);\n",
      "type 'Circle' does not implement interface 'IShape'; "
      "add 'implements IShape'" },
    /* Pushing into an interface array is a coercion site as well. */
    { INTERFACE_SHAPE
      "record Circle {\n"
      "  radius : number;\n"
      "  fn area() -> number { return self.radius; }\n"
      "  fn describe() -> string { return 'circle'; }\n"
      "}\n"
      "let mut shapes = new IShape[];\n"
      "shapes.push(new Circle { radius: 1 });\n",
      "add 'implements IShape'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* A mutable receiver is part of the contract: a caller holding an IShape has
 * only the interface's word for whether calling area() can mutate the
 * receiver, so the two declarations have to agree in both directions. */
UTEST_F(compiler_interfaces, a_mutable_receiver_is_part_of_the_contract)
{
  static const oak_case_t cases[] = {
    { "interface ICounter { fn bump() -> number; }\n"
      "record Counter implements ICounter {\n"
      "  n : number;\n"
      "  fn mut bump() -> number { self.n += 1; return self.n; }\n"
      "}\n",
      "method 'bump' is declared 'fn mut', interface declares 'fn'" },
    { "interface ICounter { fn mut bump() -> number; }\n"
      "record Counter implements ICounter {\n"
      "  n : number;\n"
      "  fn bump() -> number { return self.n; }\n"
      "}\n",
      "method 'bump' is declared 'fn', interface declares 'fn mut'" },
  };

  static const oak_case_t agreeing[] = {
    { "interface ICounter { fn mut bump() -> number; }\n"
      "record Counter implements ICounter {\n"
      "  n : number;\n"
      "  fn mut bump() -> number { self.n += 1; return self.n; }\n"
      "}\n"
      "fn run(mut c : ICounter) { print(c.bump()); }\n"
      "let mut c = new Counter { n: 1 };\n"
      "run(c);\n",
      null },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
  OAK_EXPECT_OK_CASES(agreeing);
}

/* An interface value is a record instance behind a vtable, so a member with no
 * receiver has nothing to dispatch on. Refusing `fn static` here is also what
 * lets every oak_interface_method_t assume a receiver. */
UTEST_F(compiler_interfaces, interface_members_cannot_be_static)
{
  static const oak_case_t cases[] = {
    { "interface IMaker { fn static make() -> number; }\n",
      "interface member 'make' cannot be 'static'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* One record, several interfaces: each coercion picks its own vtable. */
UTEST_F(compiler_interfaces, a_record_may_implement_several_interfaces)
{
  static const oak_case_t cases[] = {
    { "interface IArea { fn area() -> number; }\n"
      "interface ILabel { fn label() -> string; }\n"
      "record Tile implements IArea, ILabel {\n"
      "  side : number;\n"
      "  fn area() -> number { return self.side * self.side; }\n"
      "  fn label() -> string { return 'tile'; }\n"
      "}\n"
      "fn show_area(a : IArea) { print(a.area()); }\n"
      "fn show_label(l : ILabel) { print(l.label()); }\n"
      "let t = new Tile { side: 3 };\n"
      "show_area(t);\n"
      "show_label(t);\n",
      "9\ntile" },
  };

  OAK_EXPECT_OK_CASES(cases);
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
      "  fn name() -> string;\n"
      "  fn greet() -> string { return 'hi ' + self.name(); }\n"
      "}\n"
      "record Person implements IGreet {\n"
      "  n : string;\n"
      "  fn name() -> string { return self.n; }\n"
      /* greet() deliberately not implemented */
      "}\n"
      "fn use_greet(g : IGreet) { print(g.greet()); }\n"
      "let p = new Person { n: 'ada' };\n"
      "use_greet(p);\n",
      "does not implement interface 'IGreet'" },
  };

  static const oak_case_t implemented[] = {
    { "interface IGreet {\n"
      "  fn name() -> string;\n"
      "  fn greet() -> string { return 'hello ' + self.name(); }\n"
      "}\n"
      "record Person implements IGreet {\n"
      "  n : string;\n"
      "  fn name() -> string { return self.n; }\n"
      "  fn greet() -> string { return 'hello ' + self.n; }\n"
      "}\n"
      "let p = new Person { n: 'world' };\n"
      "print(p.greet());\n",
      null },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(missing);
  OAK_EXPECT_OK_CASES(implemented);
}

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
    { "interface IScalable { fn scale(factor: number) -> number; }\n"
      "record Box implements IScalable {\n"
      "  size : number;\n"
      "  fn scale(factor: number) -> number { return self.size * factor; }\n"
      "}\n"
      "fn use_it(s: IScalable) { print(s.scale('bad')); }\n"
      "let b = new Box { size: 5 };\n"
      "use_it(b);\n",
      "expected type 'number'" },
    { "interface IScalable { fn scale(factor: number) -> number; }\n"
      "record Box implements IScalable {\n"
      "  size : number;\n"
      "  fn scale(factor: number) -> number { return self.size * factor; }\n"
      "}\n"
      "fn use_it(s: IScalable) { print(s.scale()); }\n"
      "let b = new Box { size: 5 };\n"
      "use_it(b);\n",
      "expects 1 arguments, got 0" },
    { "interface IScalable { fn scale(factor: number) -> number; }\n"
      "record Box implements IScalable {\n"
      "  size : number;\n"
      "  fn scale(factor: number) -> number { return self.size * factor; }\n"
      "}\n"
      "fn use_it(s: IScalable) { print(s.scale(1, 2)); }\n"
      "let b = new Box { size: 5 };\n"
      "use_it(b);\n",
      "expects 1 arguments, got 2" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_interfaces, interface_declarations_are_validated)
{
  static const oak_case_t cases[] = {
    { "interface IShape { fn area() -> number; }\n"
      "interface IShape { fn area() -> number; }\n",
      "duplicate interface 'IShape'" },
    /* Interface names carry an `I` prefix by language rule, not convention. */
    { "interface Shape { fn area() -> number; }\n",
      "interface names must start with 'I'" },
    { "record Point implements IMissing { x : number; }\n",
      "declares unknown interface 'IMissing'" },
    { "interface IShape { fn area() -> number; }\n"
      "record Point implements IShape, IShape { x : number; }\n",
      "duplicate interface 'IShape'" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}
