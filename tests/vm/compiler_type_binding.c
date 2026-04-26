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
 * Type declaration — basic binding
 * ========================================================================= */

/* A record type declaration binds the name and can be used immediately. */
OAK_TEST_DECL(SimpleTypeBindingOk)
{
  return expect_ok("record Color { r : number; g : number; b : number; }\n"
                   "let c = new Color { r : 255, g : 128, b : 0 };\n"
                   "print(c.r);\n");
}

/* Struct with a single field still binds correctly. */
OAK_TEST_DECL(SingleFieldTypeBindingOk)
{
  return expect_ok("record Wrapper { value : number; }\n"
                   "let w = new Wrapper { value : 42 };\n"
                   "print(w.value);\n");
}

/* An empty record (no fields) is a valid type declaration. */
OAK_TEST_DECL(EmptyStructTypeBindingOk)
{
  return expect_ok("record Empty { }\n"
                   "let e = new Empty { };\n");
}

/* =========================================================================
 * Type declaration — duplicate name
 * ========================================================================= */

/* Declaring the same record name twice is a compile error. */
OAK_TEST_DECL(DuplicateTypeBindingFails)
{
  return expect_compile_error("record Point { x : number; }\n"
                              "record Point { y : number; }\n");
}

/* A record and a subsequent declaration with the same name also fails. */
OAK_TEST_DECL(DuplicateTypeBindingWithFieldsFails)
{
  return expect_compile_error(
      "record Vec { x : number; y : number; }\n"
      "record Vec { x : number; y : number; z : number; }\n");
}

/* =========================================================================
 * Struct literal — unknown type
 * ========================================================================= */

/* Using a record name that was never declared is a compile error. */
OAK_TEST_DECL(UnknownTypeInStructLiteralFails)
{
  return expect_compile_error("let p = new Ghost { x : 1 };\n");
}

/* A typo in the type name is rejected. */
OAK_TEST_DECL(MisspelledTypeInStructLiteralFails)
{
  return expect_compile_error("record Point { x : number; }\n"
                              "let p = new Ponit { x : 1 };\n");
}

/* =========================================================================
 * Type binding in function parameters
 * ========================================================================= */

/* A declared record type can be used as a function parameter type. */
OAK_TEST_DECL(TypeBindingInFnParamOk)
{
  return expect_ok("record Size { w : number; h : number; }\n"
                   "fn area(s : Size) -> number { return s.w * s.h; }\n"
                   "let sz = new Size { w : 4, h : 5 };\n"
                   "print(area(sz));\n");
}

/* Passing a record of the wrong type to a typed parameter is a compile
 * error even when both records have the same field layout. */
OAK_TEST_DECL(TypeBindingFnParamWrongTypeFails)
{
  return expect_compile_error(
      "record A { x : number; }\n"
      "record B { x : number; }\n"
      "fn take_a(v : A) -> number { return v.x; }\n"
      "let b = new B { x : 7 };\n"
      "take_a(b);\n");
}

/* =========================================================================
 * Type binding in return types
 * ========================================================================= */

/* A declared record type can be used as a function return type. */
OAK_TEST_DECL(TypeBindingInReturnTypeOk)
{
  return expect_ok("record Pair { a : number; b : number; }\n"
                   "fn make_pair(x : number, y : number) -> Pair {\n"
                   "  return new Pair { a : x, b : y };\n"
                   "}\n"
                   "let p = make_pair(3, 7);\n"
                   "print(p.a);\n");
}

/* =========================================================================
 * Type binding as field type (nested record)
 * ========================================================================= */

/* A record type may appear as the declared field type of another record. */
OAK_TEST_DECL(TypeBindingAsFieldTypeOk)
{
  return expect_ok("record Inner { z : number; }\n"
                   "record Outer { inner : Inner; }\n"
                   "let i = new Inner { z : 9 };\n"
                   "let o = new Outer { inner : i };\n"
                   "print(o.inner.z);\n");
}

/* Providing a record of the wrong type for a record-typed field is rejected. */
OAK_TEST_DECL(TypeBindingWrongStructFieldTypeFails)
{
  return expect_compile_error(
      "record Inner { z : number; }\n"
      "record Other { z : number; }\n"
      "record Outer { inner : Inner; }\n"
      "let o2 = new Other { z : 1 };\n"
      "let bad = new Outer { inner : o2 };\n");
}

/* Three levels of nesting all bind correctly at compile time. */
OAK_TEST_DECL(TypeBindingDeepNestingOk)
{
  return expect_ok("record A { x : number; }\n"
                   "record B { a : A; }\n"
                   "record C { b : B; }\n"
                   "let a = new A { x : 1 };\n"
                   "let b = new B { a : a };\n"
                   "let c = new C { b : b };\n"
                   "print(c.b.a.x);\n");
}

/* =========================================================================
 * Struct literal — field initialiser type mismatch
 * ========================================================================= */

/* Providing a string where a number field is expected is a compile error. */
OAK_TEST_DECL(StructLiteralWrongPrimitiveTypeFails)
{
  return expect_compile_error("record Point { x : number; y : number; }\n"
                              "let p = new Point { x : 'bad', y : 1 };\n");
}

/* Providing a number where a string field is expected is a compile error. */
OAK_TEST_DECL(StructLiteralNumberForStringFieldFails)
{
  return expect_compile_error("record Label { text : string; }\n"
                              "let l = new Label { text : 99 };\n");
}

/* =========================================================================
 * Struct literal — unknown / duplicate field in the literal
 * ========================================================================= */

/* Providing a field that is not declared on the record is a compile error. */
OAK_TEST_DECL(StructLiteralUnknownFieldFails)
{
  return expect_compile_error("record Point { x : number; }\n"
                              "let p = new Point { x : 1, z : 2 };\n");
}

/* Supplying the same field twice in a record literal is a compile error. */
OAK_TEST_DECL(StructLiteralDuplicateFieldFails)
{
  return expect_compile_error("record Point { x : number; y : number; }\n"
                              "let p = new Point { x : 1, x : 2, y : 3 };\n");
}

/* =========================================================================
 * Struct declaration — duplicate field name
 * ========================================================================= */

/* Declaring two fields with the same name in a record is a compile error. */
OAK_TEST_DECL(DuplicateFieldDeclFails)
{
  return expect_compile_error(
      "record Bad { x : number; x : string; }\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    /* basic type binding */
    OAK_TEST_ENTRY(SimpleTypeBindingOk),
    OAK_TEST_ENTRY(SingleFieldTypeBindingOk),
    OAK_TEST_ENTRY(EmptyStructTypeBindingOk),
    /* duplicate name */
    OAK_TEST_ENTRY(DuplicateTypeBindingFails),
    OAK_TEST_ENTRY(DuplicateTypeBindingWithFieldsFails),
    /* unknown record in literal */
    OAK_TEST_ENTRY(UnknownTypeInStructLiteralFails),
    OAK_TEST_ENTRY(MisspelledTypeInStructLiteralFails),
    /* type binding in fn params / returns */
    OAK_TEST_ENTRY(TypeBindingInFnParamOk),
    OAK_TEST_ENTRY(TypeBindingFnParamWrongTypeFails),
    OAK_TEST_ENTRY(TypeBindingInReturnTypeOk),
    /* type binding as field type */
    OAK_TEST_ENTRY(TypeBindingAsFieldTypeOk),
    OAK_TEST_ENTRY(TypeBindingWrongStructFieldTypeFails),
    OAK_TEST_ENTRY(TypeBindingDeepNestingOk),
    /* record literal value type mismatch */
    OAK_TEST_ENTRY(StructLiteralWrongPrimitiveTypeFails),
    OAK_TEST_ENTRY(StructLiteralNumberForStringFieldFails),
    /* record literal unknown / duplicate field */
    OAK_TEST_ENTRY(StructLiteralUnknownFieldFails),
    OAK_TEST_ENTRY(StructLiteralDuplicateFieldFails),
    /* record decl duplicate field */
    OAK_TEST_ENTRY(DuplicateFieldDeclFails),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
