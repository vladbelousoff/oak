#include "oak_compiler.h"
#include "oak_count_of.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <string.h>

static enum oak_test_status_t expect_ok(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = { 0 };
  oak_compile(root, &cr);
  OAK_CHECK(cr.chunk != null);

  struct oak_vm_t vm;
  oak_vm_init(&vm);
  const enum oak_vm_result_t r = oak_vm_run(&vm, cr.chunk);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
  oak_parser_free(&result);
  oak_lexer_free(lexer);

  OAK_CHECK(r == OAK_VM_OK);
  return OAK_TEST_OK;
}

static enum oak_test_status_t expect_compile_error(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = { 0 };
  oak_compile(root, &cr);
  OAK_CHECK(cr.chunk == null);

  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

/* =========================================================================
 * Field access — basic correctness
 * ========================================================================= */

/* Reading a number field from a record compiles and runs. */
OAK_TEST_DECL(FieldAccessNumberOk)
{
  return expect_ok("type Point record { x : number; y : number; }\n"
                   "let p = new Point { x : 3, y : 4 };\n"
                   "print(p.x);\n"
                   "print(p.y);\n");
}

/* Reading a string field from a record compiles and runs. */
OAK_TEST_DECL(FieldAccessStringOk)
{
  return expect_ok("type Named record { label : string; }\n"
                   "let n = new Named { label : 'hello' };\n"
                   "print(n.label);\n");
}

/* A field whose type is another record can be accessed and its own fields
 * can be read in a chained expression. */
OAK_TEST_DECL(FieldAccessChainedOk)
{
  return expect_ok("type Inner record { v : number; }\n"
                   "type Outer record { inner : Inner; }\n"
                   "let i = new Inner { v : 7 };\n"
                   "let o = new Outer { inner : i };\n"
                   "print(o.inner.v);\n");
}

/* =========================================================================
 * Field access — unknown field
 * ========================================================================= */

/* Accessing a field that does not exist on the record is a compile error. */
OAK_TEST_DECL(FieldAccessUnknownFieldFails)
{
  return expect_compile_error("type Point record { x : number; }\n"
                              "let p = new Point { x : 1 };\n"
                              "print(p.z);\n");
}

/* Accessing a field with a misspelled name is a compile error. */
OAK_TEST_DECL(FieldAccessMisspelledFieldFails)
{
  return expect_compile_error("type Point record { x : number; y : number; }\n"
                              "let p = new Point { x : 1, y : 2 };\n"
                              "print(p.X);\n");
}

/* =========================================================================
 * Field access — type propagation into function arguments
 * ========================================================================= */

/* The static type of a number field access is number; it may be passed to
 * a function that expects a number. */
OAK_TEST_DECL(FieldTypeFlowsToNumberParamOk)
{
  return expect_ok("type Point record { x : number; y : number; }\n"
                   "fn double(n : number) -> number { return n * 2; }\n"
                   "let p = new Point { x : 5, y : 6 };\n"
                   "print(double(p.x));\n");
}

/* The static type of a number field is number; it must not be passed to a
 * function that expects a string. */
OAK_TEST_DECL(FieldTypeFlowsToWrongParamFails)
{
  return expect_compile_error(
      "type Point record { x : number; }\n"
      "fn greet(s : string) -> number { return 0; }\n"
      "let p = new Point { x : 1 };\n"
      "greet(p.x);\n");
}

/* The static type of a string field is string; passing it to a number
 * parameter is a compile error. */
OAK_TEST_DECL(StringFieldTypeToNumberParamFails)
{
  return expect_compile_error(
      "type Tag record { name : string; }\n"
      "fn add(a : number, b : number) -> number { return a + b; }\n"
      "let t = new Tag { name : 'x' };\n"
      "add(t.name, 1);\n");
}

/* A record field whose type is another record propagates correctly, so that
 * it can be passed to a function expecting that record type. */
OAK_TEST_DECL(StructFieldTypeFlowsToFnParamOk)
{
  return expect_ok("type Inner record { v : number; }\n"
                   "type Outer record { inner : Inner; }\n"
                   "fn read(i : Inner) -> number { return i.v; }\n"
                   "let i = new Inner { v : 3 };\n"
                   "let o = new Outer { inner : i };\n"
                   "print(read(o.inner));\n");
}

/* Passing a record-typed field to a function expecting a different record
 * is a compile error even when the records share the same field layout. */
OAK_TEST_DECL(StructFieldTypeToWrongFnParamFails)
{
  return expect_compile_error(
      "type A record { v : number; }\n"
      "type B record { v : number; }\n"
      "type Outer record { a : A; }\n"
      "fn take_b(x : B) -> number { return x.v; }\n"
      "let a = new A { v : 1 };\n"
      "let o = new Outer { a : a };\n"
      "take_b(o.a);\n");
}

/* =========================================================================
 * Field assignment — basic correctness
 * ========================================================================= */

/* Assigning a number to a number field on a mutable record compiles and
 * runs. */
OAK_TEST_DECL(FieldAssignNumberOk)
{
  return expect_ok("type Point record { x : number; y : number; }\n"
                   "let mut p = new Point { x : 1, y : 2 };\n"
                   "p.x = 10;\n"
                   "print(p.x);\n");
}

/* Assigning a string to a string field on a mutable record compiles and
 * runs. */
OAK_TEST_DECL(FieldAssignStringOk)
{
  return expect_ok("type Named record { label : string; }\n"
                   "let mut n = new Named { label : 'old' };\n"
                   "n.label = 'new';\n"
                   "print(n.label);\n");
}

/* Chained field assignment through a mutable record compiles and runs. */
OAK_TEST_DECL(FieldAssignChainedOk)
{
  return expect_ok("type Inner record { v : number; }\n"
                   "type Outer record { inner : Inner; }\n"
                   "let i = new Inner { v : 0 };\n"
                   "let mut o = new Outer { inner : i };\n"
                   "o.inner.v = 99;\n"
                   "print(o.inner.v);\n");
}

/* =========================================================================
 * Field assignment — type mismatch
 * ========================================================================= */

/* Assigning a string to a number field is a compile error. */
OAK_TEST_DECL(FieldAssignStringToNumberFails)
{
  return expect_compile_error("type Point record { x : number; }\n"
                              "let mut p = new Point { x : 1 };\n"
                              "p.x = 'bad';\n");
}

/* Assigning a number to a string field is a compile error. */
OAK_TEST_DECL(FieldAssignNumberToStringFails)
{
  return expect_compile_error("type Named record { label : string; }\n"
                              "let mut n = new Named { label : 'ok' };\n"
                              "n.label = 42;\n");
}

/* Assigning a record of the wrong type to a record-typed field is a
 * compile error. */
OAK_TEST_DECL(FieldAssignWrongStructTypeFails)
{
  return expect_compile_error(
      "type A record { v : number; }\n"
      "type B record { v : number; }\n"
      "type Container record { item : A; }\n"
      "let b = new B { v : 1 };\n"
      "let a = new A { v : 2 };\n"
      "let mut c = new Container { item : a };\n"
      "c.item = b;\n");
}

/* =========================================================================
 * Field access on non-record receiver
 * ========================================================================= */

/* Accessing a field on a plain number variable is a compile error. */
OAK_TEST_DECL(FieldAccessOnNumberFails)
{
  return expect_compile_error("let n = 42;\n"
                              "print(n.x);\n");
}

/* Accessing a field on a string variable is a compile error. */
OAK_TEST_DECL(FieldAccessOnStringFails)
{
  return expect_compile_error("let s = 'hello';\n"
                              "print(s.x);\n");
}

/* =========================================================================
 * Field assignment on non-record receiver
 * ========================================================================= */

/* Assigning through a field access on a plain number is a compile error. */
OAK_TEST_DECL(FieldAssignOnNonStructFails)
{
  return expect_compile_error("let mut n = 42;\n"
                              "n.x = 1;\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    /* field access — basic */
    OAK_TEST_ENTRY(FieldAccessNumberOk),
    OAK_TEST_ENTRY(FieldAccessStringOk),
    OAK_TEST_ENTRY(FieldAccessChainedOk),
    /* field access — unknown field */
    OAK_TEST_ENTRY(FieldAccessUnknownFieldFails),
    OAK_TEST_ENTRY(FieldAccessMisspelledFieldFails),
    /* field type propagation to fn args */
    OAK_TEST_ENTRY(FieldTypeFlowsToNumberParamOk),
    OAK_TEST_ENTRY(FieldTypeFlowsToWrongParamFails),
    OAK_TEST_ENTRY(StringFieldTypeToNumberParamFails),
    OAK_TEST_ENTRY(StructFieldTypeFlowsToFnParamOk),
    OAK_TEST_ENTRY(StructFieldTypeToWrongFnParamFails),
    /* field assignment — basic */
    OAK_TEST_ENTRY(FieldAssignNumberOk),
    OAK_TEST_ENTRY(FieldAssignStringOk),
    OAK_TEST_ENTRY(FieldAssignChainedOk),
    /* field assignment — type mismatch */
    OAK_TEST_ENTRY(FieldAssignStringToNumberFails),
    OAK_TEST_ENTRY(FieldAssignNumberToStringFails),
    OAK_TEST_ENTRY(FieldAssignWrongStructTypeFails),
    /* field access/assignment on non-record */
    OAK_TEST_ENTRY(FieldAccessOnNumberFails),
    OAK_TEST_ENTRY(FieldAccessOnStringFails),
    OAK_TEST_ENTRY(FieldAssignOnNonStructFails),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
