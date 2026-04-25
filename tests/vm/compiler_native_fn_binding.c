#include "oak_bind.h"
#include "oak_compiler.h"
#include "oak_count_of.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_type_id.h"
#include "oak_value.h"
#include "oak_vm.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Compile helpers
 * ------------------------------------------------------------------------- */

static enum oak_test_status_t compile_ok(const char* source,
                                          struct oak_compile_options_t* opts)
{
  struct oak_lexer_result_t* lex = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t pr = { 0 };
  oak_parse(lex, OAK_NODE_PROGRAM, &pr);
  const struct oak_ast_node_t* root = oak_parser_root(&pr);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, opts, &cr);
  OAK_CHECK(cr.chunk != null);

  oak_compile_result_free(&cr);
  oak_parser_free(&pr);
  oak_lexer_free(lex);
  return OAK_TEST_OK;
}

static enum oak_test_status_t compile_fails(const char* source,
                                             struct oak_compile_options_t* opts)
{
  struct oak_lexer_result_t* lex = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t pr = { 0 };
  oak_parse(lex, OAK_NODE_PROGRAM, &pr);
  const struct oak_ast_node_t* root = oak_parser_root(&pr);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, opts, &cr);
  OAK_CHECK(cr.chunk == null);

  oak_parser_free(&pr);
  oak_lexer_free(lex);
  return OAK_TEST_OK;
}

/* Stub native implementations — just return 0/void. */
static enum oak_fn_call_result_t stub_fn(void* vm,
                                          const struct oak_value_t* args,
                                          int argc,
                                          struct oak_value_t* out_result)
{
  (void)vm;
  (void)args;
  (void)argc;
  *out_result = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t add_fn(void* vm,
                                         const struct oak_value_t* args,
                                         int argc,
                                         struct oak_value_t* out_result)
{
  (void)vm;
  (void)args;
  (void)argc;
  *out_result = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

static struct oak_value_t stub_getter(struct oak_value_t self)
{
  (void)self;
  return OAK_VALUE_I32(0);
}

static enum oak_test_status_t run_ok(const char* source,
                                      struct oak_compile_options_t* opts)
{
  struct oak_lexer_result_t* lex = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t pr = { 0 };
  oak_parse(lex, OAK_NODE_PROGRAM, &pr);
  const struct oak_ast_node_t* root = oak_parser_root(&pr);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, opts, &cr);
  OAK_CHECK(cr.chunk != null);

  struct oak_vm_t vm;
  oak_vm_init(&vm);
  const enum oak_vm_result_t r = oak_vm_run(&vm, cr.chunk);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
  oak_parser_free(&pr);
  oak_lexer_free(lex);

  OAK_CHECK(r == OAK_VM_OK);
  return OAK_TEST_OK;
}

/* =========================================================================
 * Section 1 — oak_bind_fn C API
 * ========================================================================= */

/* oak_bind_fn(&opts, &(oak_bind_fn_params_t){ kind, ... }) registers a global. */
OAK_TEST_DECL(BindFnGlobalRegisters)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  const int r = oak_bind_fn(
      &opts,
      &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                               .receiver_type_id = OAK_TYPE_VOID,
                               .name = "my_global",
                               .impl = stub_fn,
                               .arity = 1,
                               .return_type_id = OAK_TYPE_NUMBER,
                               .return_shape = OAK_BIND_RETURN_SCALAR });
  OAK_CHECK(r == 0);
  OAK_CHECK(opts.native_fn_count == 1);
  OAK_CHECK(strcmp(opts.native_fns[0].name, "my_global") == 0);
  OAK_CHECK(opts.native_fns[0].receiver_type_id == OAK_TYPE_VOID);
  OAK_CHECK(opts.native_fns[0].arity == 1);
  OAK_CHECK(opts.native_fns[0].return_type_id == OAK_TYPE_NUMBER);
  OAK_CHECK(opts.native_fns[0].return_shape == OAK_BIND_RETURN_SCALAR);
  OAK_CHECK(opts.native_fns[0].impl == stub_fn);

  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

/* OAK_BIND_FN_INSTANCE_METHOD on a type registers an instance method. */
OAK_TEST_DECL(BindFnMethodRegisters)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  struct oak_native_type_t* t = oak_bind_type(&opts, OAK_BIND_RECORD, "MyVec");
  OAK_CHECK(t != null);

  const int r = oak_bind_fn(
      &opts,
      &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_INSTANCE_METHOD,
                               .receiver_type_id = t->type_id,
                               .name = "length",
                               .impl = stub_fn,
                               .arity = 0,
                               .return_type_id = OAK_TYPE_NUMBER,
                               .return_shape = OAK_BIND_RETURN_SCALAR });
  OAK_CHECK(r == 0);
  OAK_CHECK(opts.native_fn_count == 1);
  OAK_CHECK(opts.native_fns[0].receiver_type_id == t->type_id);
  OAK_CHECK(opts.native_fns[0].arity == 0);

  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

/* Null name is rejected. */
OAK_TEST_DECL(BindFnNullNameRejected)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  const int r = oak_bind_fn(
      &opts,
      &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                               .receiver_type_id = OAK_TYPE_VOID,
                               .name = null,
                               .impl = stub_fn,
                               .arity = 0,
                               .return_type_id = OAK_TYPE_NUMBER,
                               .return_shape = OAK_BIND_RETURN_SCALAR });
  OAK_CHECK(r == -1);
  OAK_CHECK(opts.native_fn_count == 0);

  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

/* Null impl is rejected. */
OAK_TEST_DECL(BindFnNullImplRejected)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  const int r = oak_bind_fn(
      &opts,
      &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                               .receiver_type_id = OAK_TYPE_VOID,
                               .name = "foo",
                               .impl = null,
                               .arity = 0,
                               .return_type_id = OAK_TYPE_NUMBER,
                               .return_shape = OAK_BIND_RETURN_SCALAR });
  OAK_CHECK(r == -1);
  OAK_CHECK(opts.native_fn_count == 0);

  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

/* Negative arity is rejected. */
OAK_TEST_DECL(BindFnNegativeArityRejected)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  const int r = oak_bind_fn(
      &opts,
      &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                               .receiver_type_id = OAK_TYPE_VOID,
                               .name = "foo",
                               .impl = stub_fn,
                               .arity = -1,
                               .return_type_id = OAK_TYPE_NUMBER,
                               .return_shape = OAK_BIND_RETURN_SCALAR });
  OAK_CHECK(r == -1);

  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

/* Multiple functions can be registered. */
OAK_TEST_DECL(BindFnMultipleRegistrations)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  OAK_CHECK(oak_bind_fn(
                &opts,
                &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                         .receiver_type_id = OAK_TYPE_VOID,
                                         .name = "a",
                                         .impl = stub_fn,
                                         .arity = 0,
                                         .return_type_id = OAK_TYPE_VOID,
                                         .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);
  OAK_CHECK(oak_bind_fn(
                &opts,
                &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                         .receiver_type_id = OAK_TYPE_VOID,
                                         .name = "b",
                                         .impl = stub_fn,
                                         .arity = 1,
                                         .return_type_id = OAK_TYPE_NUMBER,
                                         .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);
  OAK_CHECK(oak_bind_fn(
                &opts,
                &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                         .receiver_type_id = OAK_TYPE_VOID,
                                         .name = "c",
                                         .impl = stub_fn,
                                         .arity = 2,
                                         .return_type_id = OAK_TYPE_BOOL,
                                         .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);
  OAK_CHECK(opts.native_fn_count == 3);

  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

/* =========================================================================
 * Section 2 — compile-time: global native function
 * ========================================================================= */

/* A native global function can be called from Oak code. */
OAK_TEST_DECL(GlobalNativeFnCallCompiles)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                    .receiver_type_id = OAK_TYPE_VOID,
                                    .name = "native_add",
                                    .impl = add_fn,
                                    .arity = 2,
                                    .return_type_id = OAK_TYPE_NUMBER,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s =
      compile_ok("let x = native_add(1, 2);", &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* Calling a native global function with too few arguments is a compile error. */
OAK_TEST_DECL(GlobalNativeFnWrongArgCountFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                    .receiver_type_id = OAK_TYPE_VOID,
                                    .name = "native_add2",
                                    .impl = add_fn,
                                    .arity = 2,
                                    .return_type_id = OAK_TYPE_NUMBER,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s =
      compile_fails("let x = native_add2(1);", &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* Duplicate global native function registration is a compile error. */
OAK_TEST_DECL(GlobalNativeFnDuplicateFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                    .receiver_type_id = OAK_TYPE_VOID,
                                    .name = "dup_fn",
                                    .impl = stub_fn,
                                    .arity = 0,
                                    .return_type_id = OAK_TYPE_VOID,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);
  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                    .receiver_type_id = OAK_TYPE_VOID,
                                    .name = "dup_fn",
                                    .impl = stub_fn,
                                    .arity = 0,
                                    .return_type_id = OAK_TYPE_VOID,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  /* Both are in opts; the compiler should detect the duplicate. */
  const enum oak_test_status_t s = compile_fails("let x = 1;", &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* Return type of a native global function is inferred by the compiler.
 * Assigning the result to a variable and passing it to a function expecting
 * the declared return type compiles without error. */
OAK_TEST_DECL(GlobalNativeFnReturnTypeInferred)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                    .receiver_type_id = OAK_TYPE_VOID,
                                    .name = "get_num",
                                    .impl = stub_fn,
                                    .arity = 0,
                                    .return_type_id = OAK_TYPE_NUMBER,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s =
      compile_ok("fn takes_number(n : number) -> number { return n; }\n"
                 "let x = takes_number(get_num());",
                 &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* Passing the result of a native function where the wrong type is expected
 * is a compile error (requires return type inference). */
OAK_TEST_DECL(GlobalNativeFnReturnTypeWrongArgFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                    .receiver_type_id = OAK_TYPE_VOID,
                                    .name = "get_num2",
                                    .impl = stub_fn,
                                    .arity = 0,
                                    .return_type_id = OAK_TYPE_NUMBER,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s =
      compile_fails("fn takes_string(s : string) -> string { return s; }\n"
                    "let x = takes_string(get_num2());",
                    &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* =========================================================================
 * Section 3 — compile-time: native record method
 * ========================================================================= */

/* A native method can be called on a native record instance. */
OAK_TEST_DECL(NativeMethodCallCompiles)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  struct oak_native_type_t* t =
      oak_bind_type(&opts, OAK_BIND_RECORD, "NTVec3");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "x", OAK_TYPE_NUMBER, stub_getter, null) == 0);

  /* Method with no user args, returns a number. */
  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_INSTANCE_METHOD,
                                    .receiver_type_id = t->type_id,
                                    .name = "len",
                                    .impl = stub_fn,
                                    .arity = 0,
                                    .return_type_id = OAK_TYPE_NUMBER,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s =
      compile_ok("fn test(v : NTVec3) -> number { return v.len(); }", &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* Calling a native method with wrong argument count is a compile error. */
OAK_TEST_DECL(NativeMethodWrongArgCountFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  struct oak_native_type_t* t =
      oak_bind_type(&opts, OAK_BIND_RECORD, "NTVec4");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "x", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_INSTANCE_METHOD,
                                    .receiver_type_id = t->type_id,
                                    .name = "scale",
                                    .impl = stub_fn,
                                    .arity = 1,
                                    .return_type_id = OAK_TYPE_VOID,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s = compile_fails(
      "fn test(v : NTVec4) { v.scale(1, 2); }", &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* The return type of a native method is inferred for type checking. */
OAK_TEST_DECL(NativeMethodReturnTypeInferred)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  struct oak_native_type_t* t =
      oak_bind_type(&opts, OAK_BIND_RECORD, "NTShape");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "id", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_INSTANCE_METHOD,
                                    .receiver_type_id = t->type_id,
                                    .name = "area",
                                    .impl = stub_fn,
                                    .arity = 0,
                                    .return_type_id = OAK_TYPE_NUMBER,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  /* area() returns a number; passing it to a fn that expects number is OK. */
  const enum oak_test_status_t s =
      compile_ok("fn takes_num(n : number) -> number { return n; }\n"
                 "fn test(s : NTShape) -> number { return takes_num(s.area()); }",
                 &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* Passing native method result to fn expecting wrong type is a compile error. */
OAK_TEST_DECL(NativeMethodReturnTypeWrongFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  struct oak_native_type_t* t =
      oak_bind_type(&opts, OAK_BIND_RECORD, "NTCircle");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "r", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_INSTANCE_METHOD,
                                    .receiver_type_id = t->type_id,
                                    .name = "perimeter",
                                    .impl = stub_fn,
                                    .arity = 0,
                                    .return_type_id = OAK_TYPE_NUMBER,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s = compile_fails(
      "fn takes_string(s : string) -> string { return s; }\n"
      "fn test(c : NTCircle) -> string { return takes_string(c.perimeter()); }",
      &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* Calling an undefined method on a native record is a compile error. */
OAK_TEST_DECL(NativeMethodUnknownFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  struct oak_native_type_t* t =
      oak_bind_type(&opts, OAK_BIND_RECORD, "NTWidget2");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "id", OAK_TYPE_NUMBER, stub_getter, null) == 0);

  const enum oak_test_status_t s =
      compile_fails("fn test(w : NTWidget2) { w.nonexistent(); }", &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* A native record can have both a global function and a method registered
 * in the same opts. */
OAK_TEST_DECL(GlobalFnAndMethodBothCompile)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  struct oak_native_type_t* t =
      oak_bind_type(&opts, OAK_BIND_RECORD, "NTRect");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "w", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  OAK_CHECK(oak_bind_field(t, "h", OAK_TYPE_NUMBER, stub_getter, null) == 0);

  /* Global factory function */
  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                    .receiver_type_id = OAK_TYPE_VOID,
                                    .name = "make_unit_rect",
                                    .impl = stub_fn,
                                    .arity = 0,
                                    .return_type_id = t->type_id,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);
  /* Method on the record */
  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_INSTANCE_METHOD,
                                    .receiver_type_id = t->type_id,
                                    .name = "area",
                                    .impl = stub_fn,
                                    .arity = 0,
                                    .return_type_id = OAK_TYPE_NUMBER,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s = compile_ok(
      "fn test() -> number {\n"
      "  let r = make_unit_rect();\n"
      "  return r.area();\n"
      "}",
      &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* =========================================================================
 * Section 4 — single arity: too many arguments are rejected
 * ========================================================================= */

/* Calling a global native fn with more args than declared is a compile error. */
OAK_TEST_DECL(GlobalNativeFnTooManyArgsFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                    .receiver_type_id = OAK_TYPE_VOID,
                                    .name = "one_arg",
                                    .impl = stub_fn,
                                    .arity = 1,
                                    .return_type_id = OAK_TYPE_VOID,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s =
      compile_fails("one_arg(1, 2);", &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* Calling a native method with more args than declared is a compile error. */
OAK_TEST_DECL(NativeMethodTooManyArgsFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  struct oak_native_type_t* t =
      oak_bind_type(&opts, OAK_BIND_RECORD, "NTBox");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "v", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  /* scale expects exactly 1 user arg */
  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_INSTANCE_METHOD,
                                    .receiver_type_id = t->type_id,
                                    .name = "scale",
                                    .impl = stub_fn,
                                    .arity = 1,
                                    .return_type_id = OAK_TYPE_VOID,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s = compile_fails(
      "fn test(b : NTBox) { b.scale(1, 2); }", &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* A zero-arity native fn called with zero args compiles and runs. */
OAK_TEST_DECL(ZeroArityNativeFnCallOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                    .receiver_type_id = OAK_TYPE_VOID,
                                    .name = "noop",
                                    .impl = stub_fn,
                                    .arity = 0,
                                    .return_type_id = OAK_TYPE_VOID,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s = run_ok("noop();", &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* Calling a zero-arity native fn with one argument is a compile error. */
OAK_TEST_DECL(ZeroArityNativeFnWithArgFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                    .receiver_type_id = OAK_TYPE_VOID,
                                    .name = "noop2",
                                    .impl = stub_fn,
                                    .arity = 0,
                                    .return_type_id = OAK_TYPE_VOID,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s = compile_fails("noop2(1);", &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* A native method declared with arity=2 compiles when called with 2 args. */
OAK_TEST_DECL(NativeMethodMultiArgCompiles)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  struct oak_native_type_t* t =
      oak_bind_type(&opts, OAK_BIND_RECORD, "NTMat2");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "v", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_INSTANCE_METHOD,
                                    .receiver_type_id = t->type_id,
                                    .name = "set",
                                    .impl = stub_fn,
                                    .arity = 2,
                                    .return_type_id = OAK_TYPE_VOID,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s =
      compile_ok("fn test(m : NTMat2) { m.set(1, 2); }", &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* =========================================================================
 * Section 5 — VM execution: native functions actually run
 * ========================================================================= */

/* A native fn with arity=1 executes successfully at runtime. */
OAK_TEST_DECL(NativeFnRunsOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                    .receiver_type_id = OAK_TYPE_VOID,
                                    .name = "double_it",
                                    .impl = add_fn,
                                    .arity = 1,
                                    .return_type_id = OAK_TYPE_NUMBER,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s =
      run_ok("let x = double_it(3);", &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* A native fn with arity=2 executes successfully at runtime. */
OAK_TEST_DECL(NativeFnTwoArgsRunsOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);

  OAK_CHECK(
      oak_bind_fn(
          &opts,
          &(oak_bind_fn_params_t){ .kind = OAK_BIND_FN_GLOBAL,
                                    .receiver_type_id = OAK_TYPE_VOID,
                                    .name = "native_sum",
                                    .impl = add_fn,
                                    .arity = 2,
                                    .return_type_id = OAK_TYPE_NUMBER,
                                    .return_shape = OAK_BIND_RETURN_SCALAR }) == 0);

  const enum oak_test_status_t s =
      run_ok("let x = native_sum(10, 20);", &opts);

  oak_compile_options_free(&opts);
  return s;
}

/* =========================================================================
 * Section 6 — oak_native_fn_format: single arity in output
 * ========================================================================= */

/* The formatted representation of a native fn uses "arity=N" (not a range). */
OAK_TEST_DECL(NativeFnFormatSingleArity)
{
  struct oak_obj_native_fn_t* fn = oak_native_fn_new(stub_fn, 3, "my_fn");
  OAK_CHECK(fn != null);

  char buf[128];
  oak_native_fn_format(buf, sizeof(buf), fn);

  /* Must contain "arity=3" and must NOT contain "arity=3..". */
  OAK_CHECK(strstr(buf, "arity=3") != null);
  OAK_CHECK(strstr(buf, "arity=3..") == null);

  oak_value_decref(OAK_VALUE_OBJ(&fn->obj));
  return OAK_TEST_OK;
}

/* Anonymous native fn (no name) also uses single-arity format. */
OAK_TEST_DECL(NativeFnFormatAnonymousSingleArity)
{
  struct oak_obj_native_fn_t* fn = oak_native_fn_new(stub_fn, 0, null);
  OAK_CHECK(fn != null);

  char buf[128];
  oak_native_fn_format(buf, sizeof(buf), fn);

  OAK_CHECK(strstr(buf, "arity=0") != null);
  OAK_CHECK(strstr(buf, "arity=0..") == null);

  oak_value_decref(OAK_VALUE_OBJ(&fn->obj));
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    /* oak_bind_fn C API */
    OAK_TEST_ENTRY(BindFnGlobalRegisters),
    OAK_TEST_ENTRY(BindFnMethodRegisters),
    OAK_TEST_ENTRY(BindFnNullNameRejected),
    OAK_TEST_ENTRY(BindFnNullImplRejected),
    OAK_TEST_ENTRY(BindFnNegativeArityRejected),
    OAK_TEST_ENTRY(BindFnMultipleRegistrations),
    /* global native functions */
    OAK_TEST_ENTRY(GlobalNativeFnCallCompiles),
    OAK_TEST_ENTRY(GlobalNativeFnWrongArgCountFails),
    OAK_TEST_ENTRY(GlobalNativeFnDuplicateFails),
    OAK_TEST_ENTRY(GlobalNativeFnReturnTypeInferred),
    OAK_TEST_ENTRY(GlobalNativeFnReturnTypeWrongArgFails),
    /* native record methods */
    OAK_TEST_ENTRY(NativeMethodCallCompiles),
    OAK_TEST_ENTRY(NativeMethodWrongArgCountFails),
    OAK_TEST_ENTRY(NativeMethodReturnTypeInferred),
    OAK_TEST_ENTRY(NativeMethodReturnTypeWrongFails),
    OAK_TEST_ENTRY(NativeMethodUnknownFails),
    OAK_TEST_ENTRY(GlobalFnAndMethodBothCompile),
    /* single arity: too many args rejected */
    OAK_TEST_ENTRY(GlobalNativeFnTooManyArgsFails),
    OAK_TEST_ENTRY(NativeMethodTooManyArgsFails),
    OAK_TEST_ENTRY(ZeroArityNativeFnCallOk),
    OAK_TEST_ENTRY(ZeroArityNativeFnWithArgFails),
    OAK_TEST_ENTRY(NativeMethodMultiArgCompiles),
    /* VM execution */
    OAK_TEST_ENTRY(NativeFnRunsOk),
    OAK_TEST_ENTRY(NativeFnTwoArgsRunsOk),
    /* oak_native_fn_format: single arity in output */
    OAK_TEST_ENTRY(NativeFnFormatSingleArity),
    OAK_TEST_ENTRY(NativeFnFormatAnonymousSingleArity),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
