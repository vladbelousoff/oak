#include "oak_count_of.h"
#include "oak_test_pipeline.h"

#define TRAIT_SHAPE \
  "trait Shape {\n" \
  "  fn area(self) -> number;\n" \
  "  fn describe(self) -> string;\n" \
  "}\n"

#define RECORD_CIRCLE \
  "record Circle { radius : number; }\n" \
  "fn Circle.area(self) -> number { return self.radius * self.radius; }\n" \
  "fn Circle.describe(self) -> string { return 'circle'; }\n"

/* =========================================================================
 * Valid programs that must compile and run
 * ========================================================================= */

OAK_TEST_DECL(TraitConformanceOk)
{
  return expect_ok(
      TRAIT_SHAPE
      RECORD_CIRCLE
      "fn use_shape(s: Shape) { print(s.area()); }\n"
      "let c = new Circle { radius: 3 };\n"
      "use_shape(c);\n");
}

OAK_TEST_DECL(TraitVirtualCallOk)
{
  return expect_ok(
      TRAIT_SHAPE
      RECORD_CIRCLE
      "fn use_shape(s: Shape) { print(s.describe()); print(s.area()); }\n"
      "let c = new Circle { radius: 2 };\n"
      "use_shape(c);\n");
}

OAK_TEST_DECL(TraitPassAlreadyTraitObjectOk)
{
  return expect_ok(
      TRAIT_SHAPE
      RECORD_CIRCLE
      "fn inner(s: Shape) { print(s.area()); }\n"
      "fn outer(s: Shape) { inner(s); }\n"
      "let c = new Circle { radius: 1 };\n"
      "outer(c);\n");
}

OAK_TEST_DECL(TraitDefaultMethodBodyOk)
{
  return expect_ok(
      "trait Greet {\n"
      "  fn name(self) -> string;\n"
      "  fn greet(self) -> string { return 'hello ' + self.name(); }\n"
      "}\n"
      "record Person { n : string; }\n"
      "fn Person.name(self) -> string { return self.n; }\n"
      "fn Person.greet(self) -> string { return 'hello ' + self.n; }\n"
      "let p = new Person { n: 'world' };\n"
      "print(p.greet());\n");
}

/* =========================================================================
 * Conformance errors
 * ========================================================================= */

OAK_TEST_DECL(MissingTraitMethodRejected)
{
  return expect_compile_error(
      TRAIT_SHAPE
      "record Bare { x : number; }\n"
      "fn use_shape(s: Shape) { print(s.area()); }\n"
      "let b = new Bare { x: 1 };\n"
      "use_shape(b);\n");
}

OAK_TEST_DECL(WrongReturnTypeRejected)
{
  return expect_compile_error(
      "trait Shape { fn area(self) -> number; }\n"
      "record Bad { }\n"
      "fn Bad.area(self) -> string { return 'oops'; }\n"
      "fn use_shape(s: Shape) { print(s.area()); }\n"
      "let b = new Bad { };\n"
      "use_shape(b);\n");
}

OAK_TEST_DECL(WrongParamTypeRejected)
{
  return expect_compile_error(
      "trait Scalable { fn scale(self, factor: number) -> number; }\n"
      "record Bad { v : number; }\n"
      "fn Bad.scale(self, factor: string) -> number { return self.v; }\n"
      "fn use_it(s: Scalable) { print(s.scale(2)); }\n"
      "let b = new Bad { v: 1 };\n"
      "use_it(b);\n");
}

OAK_TEST_DECL(WrongArityRejected)
{
  return expect_compile_error(
      "trait Shape { fn area(self) -> number; }\n"
      "record Bad { }\n"
      "fn Bad.area(self, extra: number) -> number { return extra; }\n"
      "fn use_shape(s: Shape) { print(s.area()); }\n"
      "let b = new Bad { };\n"
      "use_shape(b);\n");
}

/* =========================================================================
 * Dispatch errors
 * ========================================================================= */

OAK_TEST_DECL(TraitMethodNotOnTraitRejected)
{
  return expect_compile_error(
      TRAIT_SHAPE
      RECORD_CIRCLE
      "fn use_shape(s: Shape) { print(s.missing()); }\n"
      "let c = new Circle { radius: 1 };\n"
      "use_shape(c);\n");
}

OAK_TEST_DECL(VirtualCallWrongArgTypeRejected)
{
  return expect_compile_error(
      "trait Scalable { fn scale(self, factor: number) -> number; }\n"
      "record Box { size : number; }\n"
      "fn Box.scale(self, factor: number) -> number { return self.size * factor; }\n"
      "fn use_it(s: Scalable) { print(s.scale('bad')); }\n"
      "let b = new Box { size: 5 };\n"
      "use_it(b);\n");
}

OAK_TEST_DECL(VirtualCallTooFewArgsRejected)
{
  return expect_compile_error(
      "trait Scalable { fn scale(self, factor: number) -> number; }\n"
      "record Box { size : number; }\n"
      "fn Box.scale(self, factor: number) -> number { return self.size * factor; }\n"
      "fn use_it(s: Scalable) { print(s.scale()); }\n"
      "let b = new Box { size: 5 };\n"
      "use_it(b);\n");
}

OAK_TEST_DECL(VirtualCallTooManyArgsRejected)
{
  return expect_compile_error(
      "trait Scalable { fn scale(self, factor: number) -> number; }\n"
      "record Box { size : number; }\n"
      "fn Box.scale(self, factor: number) -> number { return self.size * factor; }\n"
      "fn use_it(s: Scalable) { print(s.scale(1, 2)); }\n"
      "let b = new Box { size: 5 };\n"
      "use_it(b);\n");
}

/* =========================================================================
 * Duplicate / malformed declarations
 * ========================================================================= */

OAK_TEST_DECL(DuplicateTraitRejected)
{
  return expect_compile_error(
      "trait Shape { fn area(self) -> number; }\n"
      "trait Shape { fn area(self) -> number; }\n");
}

/* =========================================================================
 * Trait arrays
 * ========================================================================= */

OAK_TEST_DECL(TraitArrayPushAndIterateOk)
{
  return expect_ok(
      TRAIT_SHAPE
      RECORD_CIRCLE
      "record Rect { w : number; h : number; }\n"
      "fn Rect.area(self) -> number { return self.w * self.h; }\n"
      "fn Rect.describe(self) -> string { return 'rect'; }\n"
      "let mut shapes = [] as Shape[];\n"
      "shapes.push(new Circle { radius: 3 });\n"
      "shapes.push(new Rect { w: 2, h: 5 });\n"
      "let mut total = 0;\n"
      "for s in shapes { total += s.area(); }\n"
      "print(total);\n");
}

OAK_TEST_DECL(TraitArrayIndexOk)
{
  return expect_ok(
      TRAIT_SHAPE
      RECORD_CIRCLE
      "let mut shapes = [] as Shape[];\n"
      "shapes.push(new Circle { radius: 4 });\n"
      "print(shapes[0].area());\n");
}

OAK_TEST_DECL(TraitArrayPushNonConformingRejected)
{
  return expect_compile_error(
      TRAIT_SHAPE
      "record Plain { x : number; }\n"
      "let mut shapes = [] as Shape[];\n"
      "shapes.push(new Plain { x: 1 });\n");
}

OAK_TEST_DECL(MethodCoercionInMethodCallOk)
{
  return expect_ok(
      TRAIT_SHAPE
      RECORD_CIRCLE
      "record Renderer { }\n"
      "fn Renderer.render(self, s: Shape) { print(s.area()); }\n"
      "let r = new Renderer { };\n"
      "let c = new Circle { radius: 4 };\n"
      "r.render(c);\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(TraitConformanceOk),
    OAK_TEST_ENTRY(TraitVirtualCallOk),
    OAK_TEST_ENTRY(TraitPassAlreadyTraitObjectOk),
    OAK_TEST_ENTRY(TraitDefaultMethodBodyOk),
    OAK_TEST_ENTRY(MissingTraitMethodRejected),
    OAK_TEST_ENTRY(WrongReturnTypeRejected),
    OAK_TEST_ENTRY(WrongParamTypeRejected),
    OAK_TEST_ENTRY(WrongArityRejected),
    OAK_TEST_ENTRY(TraitMethodNotOnTraitRejected),
    OAK_TEST_ENTRY(VirtualCallWrongArgTypeRejected),
    OAK_TEST_ENTRY(VirtualCallTooFewArgsRejected),
    OAK_TEST_ENTRY(VirtualCallTooManyArgsRejected),
    OAK_TEST_ENTRY(DuplicateTraitRejected),
    OAK_TEST_ENTRY(TraitArrayPushAndIterateOk),
    OAK_TEST_ENTRY(TraitArrayIndexOk),
    OAK_TEST_ENTRY(TraitArrayPushNonConformingRejected),
    OAK_TEST_ENTRY(MethodCoercionInMethodCallOk),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
