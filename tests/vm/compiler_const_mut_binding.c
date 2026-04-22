#include "oak_compiler.h"
#include "oak_count_of.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <string.h>

/* Returns OAK_TEST_OK when the source compiles and runs without error. */
static enum oak_test_status_t expect_ok(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK(root != null);

  struct oak_chunk_t* chunk = oak_compile(root);
  OAK_CHECK(chunk != null);

  struct oak_vm_t vm;
  oak_vm_init(&vm);
  const enum oak_vm_result_t r = oak_vm_run(&vm, chunk);
  oak_vm_free(&vm);
  oak_chunk_free(chunk);
  oak_parser_free(result);
  oak_lexer_free(lexer);

  OAK_CHECK(r == OAK_VM_OK);
  return OAK_TEST_OK;
}

/* Returns OAK_TEST_OK when the source fails to compile (chunk == null). */
static enum oak_test_status_t expect_compile_error(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK(root != null);

  struct oak_chunk_t* chunk = oak_compile(root);
  OAK_CHECK(chunk == null);

  oak_parser_free(result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

/* =========================================================================
 * let mut — failure cases (refcounted types only)
 * ========================================================================= */

/* Binding a mutable variable to an immutable struct ident is rejected. */
OAK_TEST_DECL(ConstStructIdentMutBindingFails)
{
  return expect_compile_error(
      "type Point struct { x : number; y : number; }\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "let mut copy = p;\n");
}

/* Accessing a struct-typed field of an immutable receiver is rejected. */
OAK_TEST_DECL(ConstStructFieldMutBindingFails)
{
  return expect_compile_error(
      "type Inner struct { z : number; }\n"
      "type Outer struct { inner : Inner; }\n"
      "let inner = new Inner { z : 7 };\n"
      "let outer = new Outer { inner : inner };\n"
      "let mut copy = outer.inner;\n");
}

/* Chained field access through an immutable receiver is rejected
 * when the final type is a struct (refcounted). */
OAK_TEST_DECL(ConstNestedFieldMutBindingFails)
{
  return expect_compile_error(
      "type A struct { x : number; }\n"
      "type B struct { a : A; }\n"
      "type C struct { b : B; }\n"
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
  return expect_ok(
      "type Point struct { x : number; y : number; }\n"
      "let p = new Point { x : 3, y : 4 };\n"
      "let mut x = p.x;\n"
      "x = 99;\n");
}

/* Number index element from immutable array: OK (value copy). */
OAK_TEST_DECL(ConstNumberIndexMutBindingOk)
{
  return expect_ok(
      "let arr = [1, 2, 3];\n"
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
  return expect_ok(
      "type Point struct { x : number; y : number; }\n"
      "let mut p = new Point { x : 1, y : 2 };\n"
      "let mut copy = p;\n");
}

/* Field of mutable struct: allowed. */
OAK_TEST_DECL(MutStructFieldMutBindingOk)
{
  return expect_ok(
      "type Inner struct { z : number; }\n"
      "type Outer struct { inner : Inner; }\n"
      "let inner = new Inner { z : 7 };\n"
      "let mut outer = new Outer { inner : inner };\n"
      "let mut copy = outer.inner;\n");
}

/* Immutable binding from immutable source is always fine. */
OAK_TEST_DECL(ConstFieldConstBindingOk)
{
  return expect_ok(
      "type Point struct { x : number; y : number; }\n"
      "let p = new Point { x : 3, y : 4 };\n"
      "let x = p.x;\n");
}

/* =========================================================================
 * Function parameter: mut with refcounted types
 * ========================================================================= */

/* Passing an immutable struct to a mut struct param is rejected. */
OAK_TEST_DECL(MutRefParamFromImmutableFails)
{
  return expect_compile_error(
      "type Point struct { x : number; y : number; }\n"
      "fn move_point(mut p : Point) -> number { return p.x; }\n"
      "let p = new Point { x : 1, y : 2 };\n"
      "move_point(p);\n");
}

/* Passing a mutable struct to a mut struct param is fine. */
OAK_TEST_DECL(MutRefParamFromMutableOk)
{
  return expect_ok(
      "type Point struct { x : number; y : number; }\n"
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
      "type Point struct { x : number; y : number; }\n"
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
      "type Point struct { x : number; y : number; }\n"
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
  return expect_ok(
      "type Point struct { x : number; y : number; }\n"
      "fn Point.sum(self) -> number { return self.x + self.y; }\n"
      "let p = new Point { x : 3, y : 4 };\n"
      "p.sum();\n");
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
    /* function parameter — mut ref */
    OAK_TEST_ENTRY(MutRefParamFromImmutableFails),
    OAK_TEST_ENTRY(MutRefParamFromMutableOk),
    OAK_TEST_ENTRY(MutValueParamFromImmutableOk),
    /* method call — mut self */
    OAK_TEST_ENTRY(MutSelfImmutableReceiverFails),
    OAK_TEST_ENTRY(MutSelfMutableReceiverOk),
    OAK_TEST_ENTRY(ConstSelfImmutableReceiverOk),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
