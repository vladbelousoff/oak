#include "oak_count_of.h"
#include "oak_test_pipeline.h"

/* =========================================================================
 * let mut — failure cases (refcounted types only)
 * ========================================================================= */

/* Binding a mutable variable to an immutable struct ident is rejected. */
OAK_TEST_DECL(ConstStructIdentMutBindingFails)
{
  return expect_compile_error("record Point { x : number; y : number; }\n"
                              "let p = new Point { x : 1, y : 2 };\n"
                              "let mut copy = p;\n");
}

/* Accessing a struct-typed field of an immutable receiver is rejected. */
OAK_TEST_DECL(ConstStructFieldMutBindingFails)
{
  return expect_compile_error("record Inner { z : number; }\n"
                              "record Outer { inner : Inner; }\n"
                              "let inner = new Inner { z : 7 };\n"
                              "let outer = new Outer { inner : inner };\n"
                              "let mut copy = outer.inner;\n");
}

/* Chained field access through an immutable receiver is rejected
 * when the final type is a record (refcounted). */
OAK_TEST_DECL(ConstNestedFieldMutBindingFails)
{
  return expect_compile_error("record A { x : number; }\n"
                              "record B { a : A; }\n"
                              "record C { b : B; }\n"
                              "let a = new A { x : 1 };\n"
                              "let b = new B { a : a };\n"
                              "let c = new C { b : b };\n"
                              "let mut copy = c.b;\n");
}

/* =========================================================================
 * let mut — success cases
 * ========================================================================= */

/* Number is a value type (no refcount): let mut from immutable source is OK. */
OAK_TEST_DECL(ConstNumberFieldMutBindingOk)
{
  return expect_ok("record Point { x : number; y : number; }\n"
                   "let p = new Point { x : 3, y : 4 };\n"
                   "let mut x = p.x;\n"
                   "x = 99;\n");
}

/* Number index element from immutable array: OK (value copy). */
OAK_TEST_DECL(ConstNumberIndexMutBindingOk)
{
  return expect_ok("let arr = [1, 2, 3];\n"
                   "let mut x = arr[0];\n"
                   "x = 42;\n");
}

/* Binding a mutable variable to a plain literal is always fine (rvalue). */
OAK_TEST_DECL(LiteralMutBindingOk)
{
  return expect_ok("let mut x = 42;\n"
                   "x = x + 1;\n");
}

/* Binding a mutable variable to a function return value is fine (rvalue). */
OAK_TEST_DECL(FnCallMutBindingOk)
{
  return expect_ok("fn make_num() -> number { return 10; }\n"
                   "let mut x = make_num();\n"
                   "x = x * 2;\n");
}

/* Source is mutable: always allowed. */
OAK_TEST_DECL(MutStructIdentMutBindingOk)
{
  return expect_ok("record Point { x : number; y : number; }\n"
                   "let mut p = new Point { x : 1, y : 2 };\n"
                   "let mut copy = p;\n");
}

/* Field of mutable struct is allowed when the source was also mutable. */
OAK_TEST_DECL(MutStructFieldMutBindingOk)
{
  return expect_ok("record Inner { z : number; }\n"
                   "record Outer { inner : Inner; }\n"
                   "let mut inner = new Inner { z : 7 };\n"
                   "let mut outer = new Outer { inner : inner };\n"
                   "let mut copy = outer.inner;\n");
}

/* Immutable binding from immutable source is always fine. */
OAK_TEST_DECL(ConstFieldConstBindingOk)
{
  return expect_ok("record Point { x : number; y : number; }\n"
                   "let p = new Point { x : 3, y : 4 };\n"
                   "let x = p.x;\n");
}

/* =========================================================================
 * Field assignment: immutable record receiver
 * ========================================================================= */

/* Assigning to a field of an immutable record is rejected. */
OAK_TEST_DECL(ConstRecordFieldAssignFails)
{
  return expect_compile_error("record Point { x : number; y : number; }\n"
                              "let p = new Point { x : 1, y : 2 };\n"
                              "p.x = 99;\n");
}

/* Assigning to a field of a mutable record is fine. */
OAK_TEST_DECL(MutRecordFieldAssignOk)
{
  return expect_ok("record Point { x : number; y : number; }\n"
                   "let mut p = new Point { x : 1, y : 2 };\n"
                   "p.x = 99;\n");
}

/* =========================================================================
 * Function parameter: mut with refcounted types
 * ========================================================================= */

/* Passing an immutable struct to a mut struct param is rejected. */
OAK_TEST_DECL(MutRefParamFromImmutableFails)
{
  return expect_compile_error(
      "record Point { x : number; y : number; }\n"
      "fn move_point(mut p : Point) -> number { return p.x; }\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "move_point(p);\n");
}

/* Passing a mutable struct to a mut struct param is fine. */
OAK_TEST_DECL(MutRefParamFromMutableOk)
{
  return expect_ok("record Point { x : number; y : number; }\n"
                   "fn move_point(mut p : Point) -> number { return p.x; }\n"
                   "let mut p = new Point { x : 1, y : 2 };\n"
                   "move_point(p);\n");
}

/* Passing an immutable number to a mut number param is fine (value copy). */
OAK_TEST_DECL(MutValueParamFromImmutableOk)
{
  return expect_ok(
      "fn double(mut x : number) -> number { x = x * 2; return x; }\n"
      "let n = 5;\n"
      "double(n);\n");
}

/* =========================================================================
 * Method call: mut self
 * ========================================================================= */

/* Calling a mut-self method on an immutable receiver is rejected. */
OAK_TEST_DECL(MutSelfImmutableReceiverFails)
{
  return expect_compile_error(
      "record Point { x : number; y : number; }\n"
      "fn Point.shift(mut self, dx : number, dy : number) -> number {\n"
      "  self.x = self.x + dx;\n"
      "  self.y = self.y + dy;\n"
      "  return self.x + self.y;\n"
      "}\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "p.shift(3, 4);\n");
}

/* Calling a mut-self method on a mutable receiver is fine. */
OAK_TEST_DECL(MutSelfMutableReceiverOk)
{
  return expect_ok(
      "record Point { x : number; y : number; }\n"
      "fn Point.shift(mut self, dx : number, dy : number) -> number {\n"
      "  self.x = self.x + dx;\n"
      "  self.y = self.y + dy;\n"
      "  return self.x + self.y;\n"
      "}\n"
      "let mut p = new Point { x : 1, y : 2 };\n"
      "p.shift(3, 4);\n");
}

/* Calling a non-mut-self method on an immutable receiver is fine. */
OAK_TEST_DECL(ConstSelfImmutableReceiverOk)
{
  return expect_ok("record Point { x : number; y : number; }\n"
                   "fn Point.sum(self) -> number { return self.x + self.y; }\n"
                   "let p = new Point { x : 3, y : 4 };\n"
                   "p.sum();\n");
}

/* =========================================================================
 * Borrow checker — move semantics
 * ========================================================================= */

/* `let mut b = mut_a` moves a; reading a after the move is rejected. */
OAK_TEST_DECL(BorrowUseAfterMoveFails)
{
  return expect_compile_error("record Point { x : number; }\n"
                              "let mut a = new Point { x : 1 };\n"
                              "let mut b = a;\n"
                              "print(a.x);\n");
}

/* After a move, even writing to the source binding is rejected. */
OAK_TEST_DECL(BorrowAssignAfterMoveFails)
{
  return expect_compile_error("record Point { x : number; }\n"
                              "let mut a = new Point { x : 1 };\n"
                              "let mut b = a;\n"
                              "a = new Point { x : 5 };\n");
}

/* Storing an exclusive binding into a record field MOVES it. */
OAK_TEST_DECL(BorrowMoveIntoRecordFieldFails)
{
  return expect_compile_error("record Inner { v : number; }\n"
                              "record Outer { inner : Inner; }\n"
                              "let mut i = new Inner { v : 7 };\n"
                              "let mut o = new Outer { inner : i };\n"
                              "print(i.v);\n");
}

/* Moves are tracked per binding even when the source is `mut self`. */
OAK_TEST_DECL(BorrowMoveSelfFails)
{
  return expect_compile_error("record Point { x : number; y : number; }\n"
                              "fn Point.weird(mut self) -> number {\n"
                              "  let mut other = self;\n"
                              "  return self.x;\n"
                              "}\n"
                              "let mut p = new Point { x : 1, y : 2 };\n"
                              "p.weird();\n");
}

/* =========================================================================
 * Borrow checker — shared reborrow (freeze)
 * ========================================================================= */

/* A shared reborrow `let r = mut_a` freezes mut_a for the lifetime of r;
 * reassigning mut_a while frozen is rejected. */
OAK_TEST_DECL(BorrowAssignWhileFrozenFails)
{
  return expect_compile_error("record Point { x : number; }\n"
                              "let mut a = new Point { x : 1 };\n"
                              "let r = a;\n"
                              "a = new Point { x : 5 };\n"
                              "print(r.x);\n");
}

/* Mutating a field of a frozen binding is also rejected. */
OAK_TEST_DECL(BorrowFieldAssignWhileFrozenFails)
{
  return expect_compile_error("record Point { x : number; }\n"
                              "let mut a = new Point { x : 1 };\n"
                              "let r = a;\n"
                              "a.x = 99;\n"
                              "print(r.x);\n");
}

/* Once the freezing binding goes out of scope, the source becomes
 * exclusive again and may be mutated. */
OAK_TEST_DECL(BorrowReborrowReleasedOnScopeExitOk)
{
  return expect_ok("record Point { x : number; }\n"
                   "let mut a = new Point { x : 1 };\n"
                   "if true {\n"
                   "  let r = a;\n"
                   "  print(r.x);\n"
                   "}\n"
                   "a.x = 99;\n");
}

/* =========================================================================
 * Borrow checker — call-site argument aliasing
 * ========================================================================= */

/* Passing the same binding twice to a function where at least one parameter
 * is `mut` would alias inside the callee — rejected at the call site. */
OAK_TEST_DECL(BorrowAliasedMutArgsFails)
{
  return expect_compile_error("record Point { x : number; y : number; }\n"
                              "fn swap(mut a : Point, mut b : Point) {\n"
                              "  let tmp = a.x;\n"
                              "  a.x = b.x;\n"
                              "  b.x = tmp;\n"
                              "}\n"
                              "let mut p = new Point { x : 1, y : 2 };\n"
                              "swap(p, p);\n");
}

/* The same source as both a `mut` and a non-`mut` arg is also rejected
 * (one exclusive + one shared inside the callee). */
OAK_TEST_DECL(BorrowMutAndSharedArgsFails)
{
  return expect_compile_error(
      "record Point { x : number; }\n"
      "fn touch(mut a : Point, b : Point) -> number { return a.x + b.x; }\n"
      "let mut p = new Point { x : 1 };\n"
      "touch(p, p);\n");
}

/* Passing two different bindings to two `mut` parameters is fine. */
OAK_TEST_DECL(BorrowDistinctMutArgsOk)
{
  return expect_ok("record Point { x : number; y : number; }\n"
                   "fn swap(mut a : Point, mut b : Point) {\n"
                   "  let tmp = a.x;\n"
                   "  a.x = b.x;\n"
                   "  b.x = tmp;\n"
                   "}\n"
                   "let mut p = new Point { x : 1, y : 2 };\n"
                   "let mut q = new Point { x : 3, y : 4 };\n"
                   "swap(p, q);\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    /* let mut — failures */
    OAK_TEST_ENTRY(ConstStructIdentMutBindingFails),
    OAK_TEST_ENTRY(ConstStructFieldMutBindingFails),
    OAK_TEST_ENTRY(ConstNestedFieldMutBindingFails),
    /* let mut — successes */
    OAK_TEST_ENTRY(ConstNumberFieldMutBindingOk),
    OAK_TEST_ENTRY(ConstNumberIndexMutBindingOk),
    OAK_TEST_ENTRY(LiteralMutBindingOk),
    OAK_TEST_ENTRY(FnCallMutBindingOk),
    OAK_TEST_ENTRY(MutStructIdentMutBindingOk),
    OAK_TEST_ENTRY(MutStructFieldMutBindingOk),
    OAK_TEST_ENTRY(ConstFieldConstBindingOk),
    /* field assignment — immutable receiver */
    OAK_TEST_ENTRY(ConstRecordFieldAssignFails),
    OAK_TEST_ENTRY(MutRecordFieldAssignOk),
    /* function parameter — mut ref */
    OAK_TEST_ENTRY(MutRefParamFromImmutableFails),
    OAK_TEST_ENTRY(MutRefParamFromMutableOk),
    OAK_TEST_ENTRY(MutValueParamFromImmutableOk),
    /* method call — mut self */
    OAK_TEST_ENTRY(MutSelfImmutableReceiverFails),
    OAK_TEST_ENTRY(MutSelfMutableReceiverOk),
    OAK_TEST_ENTRY(ConstSelfImmutableReceiverOk),
    /* borrow checker — moves */
    OAK_TEST_ENTRY(BorrowUseAfterMoveFails),
    OAK_TEST_ENTRY(BorrowAssignAfterMoveFails),
    OAK_TEST_ENTRY(BorrowMoveIntoRecordFieldFails),
    OAK_TEST_ENTRY(BorrowMoveSelfFails),
    /* borrow checker — shared reborrow (freeze) */
    OAK_TEST_ENTRY(BorrowAssignWhileFrozenFails),
    OAK_TEST_ENTRY(BorrowFieldAssignWhileFrozenFails),
    OAK_TEST_ENTRY(BorrowReborrowReleasedOnScopeExitOk),
    /* borrow checker — call-site aliasing */
    OAK_TEST_ENTRY(BorrowAliasedMutArgsFails),
    OAK_TEST_ENTRY(BorrowMutAndSharedArgsFails),
    OAK_TEST_ENTRY(BorrowDistinctMutArgsOk),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
