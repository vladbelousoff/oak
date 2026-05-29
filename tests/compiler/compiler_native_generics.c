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

#include <string.h>

/* -------------------------------------------------------------------------
 * Compile helpers
 * ------------------------------------------------------------------------- */

static enum oak_test_status_t compile_ok(const char* source,
                                         struct oak_compile_options_t* opts)
{
  struct oak_lexer_result_t* lex =
      oak_lexer_tokenize(source, strlen(source), oak_test_allocator());
  struct oak_parser_result_t pr = { 0 };
  oak_parse(lex, OAK_NODE_PROGRAM, &pr, oak_test_allocator());
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
  struct oak_lexer_result_t* lex =
      oak_lexer_tokenize(source, strlen(source), oak_test_allocator());
  struct oak_parser_result_t pr = { 0 };
  oak_parse(lex, OAK_NODE_PROGRAM, &pr, oak_test_allocator());
  const struct oak_ast_node_t* root = oak_parser_root(&pr);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, opts, &cr);
  OAK_CHECK(cr.chunk == null);

  oak_parser_free(&pr);
  oak_lexer_free(lex);
  return OAK_TEST_OK;
}

/* Native impl stubs — return a number/value; bodies are irrelevant to the
 * compile-time type checks under test. */
static enum oak_fn_call_result_t ret_zero(struct oak_native_ctx_t* ctx,
                                          const struct oak_value_t* args,
                                          int argc,
                                          struct oak_value_t* out_result)
{
  (void)ctx;
  (void)args;
  (void)argc;
  *out_result = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

static struct oak_value_t num_getter(struct oak_value_t self)
{
  (void)self;
  return OAK_VALUE_I32(0);
}

/* Register a generic global fn `identity<T>(x: T) -> T`. */
static void bind_identity(struct oak_compile_options_t* opts)
{
  static const char* params[] = { "T" };
  struct oak_bind_type_ref_t ptypes[] = { OAK_BIND_PARAM(0) };
  oak_bind_fn_global(opts,
                     &(struct oak_bind_global_fn_t){
                         .name = "identity",
                         .impl = ret_zero,
                         .arity = 1,
                         .return_type = OAK_BIND_PARAM(0),
                         .generic_params = params,
                         .generic_param_count = 1,
                         .param_types = ptypes,
                         .param_count = 1,
                     });
}

/* Register a generic global fn `same<T>(a: T, b: T) -> T`. */
static void bind_same(struct oak_compile_options_t* opts)
{
  static const char* params[] = { "T" };
  struct oak_bind_type_ref_t ptypes[] = { OAK_BIND_PARAM(0),
                                                       OAK_BIND_PARAM(0) };
  oak_bind_fn_global(opts,
                     &(struct oak_bind_global_fn_t){
                         .name = "same",
                         .impl = ret_zero,
                         .arity = 2,
                         .return_type = OAK_BIND_PARAM(0),
                         .generic_params = params,
                         .generic_param_count = 1,
                         .param_types = ptypes,
                         .param_count = 2,
                     });
}

/* Register `vals<V>(m: map<string, V>) -> V` (value type param) and
 * `same_key<K>(a: map<K, number>, b: map<K, number>) -> number` (key param). */
static void bind_map_fns(struct oak_compile_options_t* opts)
{
  static const char* tp[] = { "T" };
  struct oak_bind_type_ref_t vals_params[] = {
    { .kind = OAK_TYPE_KIND_MAP, .key_id = OAK_TYPE_STRING,
      .id = OAK_TYPE_PARAM_BASE + 0 }
  };
  struct oak_bind_type_ref_t same_key_params[] = {
    { .kind = OAK_TYPE_KIND_MAP, .key_id = OAK_TYPE_PARAM_BASE + 0,
      .id = OAK_TYPE_NUMBER },
    { .kind = OAK_TYPE_KIND_MAP, .key_id = OAK_TYPE_PARAM_BASE + 0,
      .id = OAK_TYPE_NUMBER }
  };
  oak_bind_fn_global(opts,
                     &(struct oak_bind_global_fn_t){
                         .name = "vals",
                         .impl = ret_zero,
                         .arity = 1,
                         .return_type = OAK_BIND_PARAM(0),
                         .generic_params = tp,
                         .generic_param_count = 1,
                         .param_types = vals_params,
                         .param_count = 1,
                     });
  oak_bind_fn_global(opts,
                     &(struct oak_bind_global_fn_t){
                         .name = "same_key",
                         .impl = ret_zero,
                         .arity = 2,
                         .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                         .generic_params = tp,
                         .generic_param_count = 1,
                         .param_types = same_key_params,
                         .param_count = 2,
                     });
}

/* Register `mix<T>(x: T, arr: T[]) -> T`: T binds from x, then must agree with
 * the array element type. */
static void bind_mix(struct oak_compile_options_t* opts)
{
  static const char* tp[] = { "T" };
  struct oak_bind_type_ref_t mix_params[] = {
    OAK_BIND_PARAM(0), OAK_BIND_PARAM_ARRAY(0)
  };
  oak_bind_fn_global(opts,
                     &(struct oak_bind_global_fn_t){
                         .name = "mix",
                         .impl = ret_zero,
                         .arity = 2,
                         .return_type = OAK_BIND_PARAM(0),
                         .generic_params = tp,
                         .generic_param_count = 1,
                         .param_types = mix_params,
                         .param_count = 2,
                     });
}

/* Register a non-generic global fn `need_number(n: number) -> number`. */
static void bind_need_number(struct oak_compile_options_t* opts)
{
  struct oak_bind_type_ref_t ptypes[] = {
    OAK_BIND_SCALAR(OAK_TYPE_NUMBER)
  };
  oak_bind_fn_global(opts,
                     &(struct oak_bind_global_fn_t){
                         .name = "need_number",
                         .impl = ret_zero,
                         .arity = 1,
                         .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                         .param_types = ptypes,
                         .param_count = 1,
                     });
}

/* =========================================================================
 * Generic native global functions — inference
 * ========================================================================= */

OAK_TEST_DECL(GenericNativeFnNumberOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_identity(&opts);
  const enum oak_test_status_t s = compile_ok("print(identity(42));\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

OAK_TEST_DECL(GenericNativeFnStringOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_identity(&opts);
  const enum oak_test_status_t s =
      compile_ok("print(identity('hello'));\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* identity(42) must infer to number: feeding it to need_number(number)
 * compiles, proving the decl-less return-type substitution works. */
OAK_TEST_DECL(GenericNativeFnReturnTypeInferredOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_identity(&opts);
  bind_need_number(&opts);
  const enum oak_test_status_t s =
      compile_ok("need_number(identity(42));\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* identity('x') infers to string, which need_number(number) must reject. */
OAK_TEST_DECL(GenericNativeFnReturnTypeMismatchFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_identity(&opts);
  bind_need_number(&opts);
  const enum oak_test_status_t s =
      compile_fails("need_number(identity('x'));\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* =========================================================================
 * Generic native functions — type-parameter consistency
 * ========================================================================= */

OAK_TEST_DECL(GenericNativeFnSameConsistentOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_same(&opts);
  const enum oak_test_status_t s =
      compile_ok("print(same(1, 2));\nprint(same('a', 'b'));\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

OAK_TEST_DECL(GenericNativeFnSameConflictFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_same(&opts);
  const enum oak_test_status_t s = compile_fails("same(1, 'x');\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* =========================================================================
 * Non-generic native functions — argument type checking (now enforced via
 * the binding's param_types)
 * ========================================================================= */

OAK_TEST_DECL(NonGenericNativeFnArgOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_need_number(&opts);
  const enum oak_test_status_t s = compile_ok("need_number(5);\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

OAK_TEST_DECL(NonGenericNativeFnWrongArgFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_need_number(&opts);
  const enum oak_test_status_t s =
      compile_fails("need_number('not a number');\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* =========================================================================
 * Generic native records and methods
 * ========================================================================= */

/* Register a record `Box` with generic instance methods and a global
 * `make_box() -> Box` constructor.  (Record fields cannot be type parameters,
 * but methods may be generic with type params inferred from their arguments.) */
static oak_type_id_t bind_box(struct oak_compile_options_t* opts)
{
  static const char* method_params[] = { "T" };
  struct oak_bind_type_t* box =
      oak_bind_type(opts, OAK_BIND_TYPE_RECORD, "Box");
  struct oak_bind_type_ref_t echo_params[] = { OAK_BIND_PARAM(0) };
  struct oak_bind_type_ref_t same2_params[] = { OAK_BIND_PARAM(0),
                                                            OAK_BIND_PARAM(0) };
  /* get(self) -> number : non-generic; a return-only T would be uninferable
   * (no receiver specialization) and is rejected at registration. */
  oak_bind_fn(opts,
              &(struct oak_bind_fn_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type_id = box->type_id,
                  .name = "get",
                  .impl = ret_zero,
                  .arity = 0,
                  .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
              });
  /* echo(self, x: T) -> T : T binds from the argument. */
  oak_bind_fn(opts,
              &(struct oak_bind_fn_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type_id = box->type_id,
                  .name = "echo",
                  .impl = ret_zero,
                  .arity = 1,
                  .return_type = OAK_BIND_PARAM(0),
                  .generic_params = method_params,
                  .generic_param_count = 1,
                  .param_types = echo_params,
                  .param_count = 1,
              });
  /* same2(self, a: T, b: T) -> T : both args must agree on T. */
  oak_bind_fn(opts,
              &(struct oak_bind_fn_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type_id = box->type_id,
                  .name = "same2",
                  .impl = ret_zero,
                  .arity = 2,
                  .return_type = OAK_BIND_PARAM(0),
                  .generic_params = method_params,
                  .generic_param_count = 1,
                  .param_types = same2_params,
                  .param_count = 2,
              });
  oak_bind_fn_global(opts,
                     &(struct oak_bind_global_fn_t){
                         .name = "make_box",
                         .impl = ret_zero,
                         .arity = 0,
                         .return_type = OAK_BIND_SCALAR(box->type_id),
                     });
  return box->type_id;
}

/* A native record can be used by name and its (concrete) method called. */
OAK_TEST_DECL(GenericNativeRecordMethodCompiles)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_box(&opts);
  const enum oak_test_status_t s =
      compile_ok("let b = make_box();\nprint(b.get());\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* A generic instance method's T return is substituted from its argument:
 * b.echo(5) infers number and is accepted by need_number(number). */
OAK_TEST_DECL(GenericNativeMethodReturnInferredOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_box(&opts);
  bind_need_number(&opts);
  const enum oak_test_status_t s = compile_ok(
      "let b = make_box();\nneed_number(b.echo(5));\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* b.echo('x') infers string, which need_number(number) must reject. */
OAK_TEST_DECL(GenericNativeMethodReturnMismatchFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_box(&opts);
  bind_need_number(&opts);
  const enum oak_test_status_t s = compile_fails(
      "let b = make_box();\nneed_number(b.echo('x'));\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* A generic instance method binds its type param consistently across args. */
OAK_TEST_DECL(GenericNativeMethodConsistentOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_box(&opts);
  const enum oak_test_status_t s =
      compile_ok("let b = make_box();\nprint(b.same2(1, 2));\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* Conflicting argument types for one type param must be rejected on a method
 * (previously this slipped through because methods skipped generic checking). */
OAK_TEST_DECL(GenericNativeMethodConflictFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_box(&opts);
  const enum oak_test_status_t s =
      compile_fails("let b = make_box();\nb.same2(1, 'x');\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* =========================================================================
 * Generic native functions — map key/value type parameters
 * ========================================================================= */

/* vals(map<string, number>) infers V=number, accepted by need_number. */
OAK_TEST_DECL(GenericNativeMapValueInferredOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_map_fns(&opts);
  bind_need_number(&opts);
  const enum oak_test_status_t s = compile_ok(
      "let m = [:] as [string:number];\nneed_number(vals(m));\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* vals(map<string, string>) infers V=string, which need_number must reject. */
OAK_TEST_DECL(GenericNativeMapValueMismatchFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_map_fns(&opts);
  bind_need_number(&opts);
  const enum oak_test_status_t s = compile_fails(
      "let m = [:] as [string:string];\nneed_number(vals(m));\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* same_key binds the map key type param consistently across both maps. */
OAK_TEST_DECL(GenericNativeMapKeyConsistentOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_map_fns(&opts);
  const enum oak_test_status_t s =
      compile_ok("let a = [:] as [number:number];\n"
                 "let b = [:] as [number:number];\n"
                 "print(same_key(a, b));\n",
                 &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* Conflicting map key types for one type param must be rejected. */
OAK_TEST_DECL(GenericNativeMapKeyConflictFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_map_fns(&opts);
  const enum oak_test_status_t s =
      compile_fails("let a = [:] as [string:number];\n"
                    "let b = [:] as [number:number];\n"
                    "same_key(a, b);\n",
                    &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* vals expects map<string, V>; the concrete string key must still be enforced
 * even though the value is a type parameter. */
OAK_TEST_DECL(GenericNativeMapConcreteKeyMismatchFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_map_fns(&opts);
  const enum oak_test_status_t s = compile_fails(
      "let m = [:] as [number:number];\nprint(vals(m));\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* same_key expects map<K, number>; the concrete number value must still be
 * enforced even though the key is a type parameter. */
OAK_TEST_DECL(GenericNativeMapConcreteValueMismatchFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_map_fns(&opts);
  const enum oak_test_status_t s =
      compile_fails("let a = [:] as [string:string];\n"
                    "let b = [:] as [string:string];\n"
                    "same_key(a, b);\n",
                    &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* mix(number, number[]) binds T=number consistently. */
OAK_TEST_DECL(GenericNativeMixConsistentOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_mix(&opts);
  const enum oak_test_status_t s = compile_ok(
      "let arr = [1, 2, 3];\nprint(mix(7, arr));\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* mix(<map>, number[]) must fail: T is bound to a map by the first argument and
 * cannot also be the scalar element type of a T[] argument, even though the
 * map's value id and the array element id are both `number`. */
OAK_TEST_DECL(GenericNativeMixArrayShapeConflictFails)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_mix(&opts);
  const enum oak_test_status_t s = compile_fails(
      "let m = [:] as [string:number];\nlet arr = [1, 2, 3];\nmix(m, arr);\n",
      &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* =========================================================================
 * Invalid generic descriptors are rejected by the binding API
 * ========================================================================= */

OAK_TEST_DECL(BindRejectsNullGenericParams)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  const int r = oak_bind_fn_global(
      &opts,
      &(struct oak_bind_global_fn_t){ .name = "bad",
                                      .impl = ret_zero,
                                      .arity = 0,
                                      .return_type = OAK_BIND_PARAM(0),
                                      .generic_params = null,
                                      .generic_param_count = 1 });
  OAK_CHECK(r == -1);
  OAK_CHECK(opts.native_global_fns.count == 0);
  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(BindRejectsTooManyGenericParams)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  static const char* many[] = { "A", "B", "C", "D", "E",
                                "F", "G", "H", "I" };
  const int r = oak_bind_fn_global(
      &opts,
      &(struct oak_bind_global_fn_t){ .name = "bad",
                                      .impl = ret_zero,
                                      .arity = 0,
                                      .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                                      .generic_params = many,
                                      .generic_param_count = 9 });
  OAK_CHECK(r == -1);
  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(BindRejectsOutOfRangeParamRef)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  static const char* tp[] = { "T" };
  struct oak_bind_type_ref_t pt[] = { OAK_BIND_PARAM(5) };
  const int r = oak_bind_fn_global(
      &opts,
      &(struct oak_bind_global_fn_t){ .name = "bad",
                                      .impl = ret_zero,
                                      .arity = 1,
                                      .return_type = OAK_BIND_PARAM(0),
                                      .generic_params = tp,
                                      .generic_param_count = 1,
                                      .param_types = pt,
                                      .param_count = 1 });
  OAK_CHECK(r == -1);
  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(BindRejectsParamRefWithoutGenerics)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  const int r = oak_bind_fn_global(
      &opts,
      &(struct oak_bind_global_fn_t){ .name = "bad",
                                      .impl = ret_zero,
                                      .arity = 0,
                                      .return_type = OAK_BIND_PARAM(0) });
  OAK_CHECK(r == -1);
  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

/* A return-only T (not present in any parameter) can never be inferred and is
 * rejected so it cannot leak a wildcard type into callers. */
OAK_TEST_DECL(BindRejectsUninferableReturnParam)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  static const char* tp[] = { "T" };
  const int r = oak_bind_fn_global(
      &opts,
      &(struct oak_bind_global_fn_t){ .name = "make",
                                      .impl = ret_zero,
                                      .arity = 0,
                                      .return_type = OAK_BIND_PARAM(0),
                                      .generic_params = tp,
                                      .generic_param_count = 1 });
  OAK_CHECK(r == -1);
  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

/* A generic binding with parameters but no param_types would skip all argument
 * type checking, so it must be rejected at registration. */
OAK_TEST_DECL(BindRejectsGenericWithoutParamTypes)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  static const char* tp[] = { "T" };
  const int r = oak_bind_fn_global(
      &opts,
      &(struct oak_bind_global_fn_t){ .name = "bad",
                                      .impl = ret_zero,
                                      .arity = 1,
                                      .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                                      .generic_params = tp,
                                      .generic_param_count = 1 });
  OAK_CHECK(r == -1);
  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

/* Generic methods on a module-scoped receiver type are rejected, because the
 * module export ABI cannot represent generic method signatures. */
OAK_TEST_DECL(BindRejectsGenericMethodOnModuleType)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  static const char* tp[] = { "T" };
  struct oak_bind_type_ref_t pt[] = { OAK_BIND_PARAM(0) };
  struct oak_bind_type_t* t =
      oak_bind_type_in_module(&opts, "mymod", OAK_BIND_TYPE_RECORD, "Holder");
  const int r = oak_bind_fn(
      &opts,
      &(struct oak_bind_fn_t){ .kind = OAK_BIND_FN_INSTANCE_METHOD,
                               .receiver_type_id = t->type_id,
                               .name = "echo",
                               .impl = ret_zero,
                               .arity = 1,
                               .return_type = OAK_BIND_PARAM(0),
                               .generic_params = tp,
                               .generic_param_count = 1,
                               .param_types = pt,
                               .param_count = 1 });
  OAK_CHECK(r == -1);
  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

/* A field type param on a non-generic record is rejected. */
OAK_TEST_DECL(BindRejectsFieldParamOnNonGenericRecord)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  struct oak_bind_type_t* rec =
      oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "Plain");
  const int r = oak_bind_field(
      rec,
      &(struct oak_bind_field_t){ .name = "x",
                                  .type = OAK_BIND_PARAM(0),
                                  .getter = num_getter });
  OAK_CHECK(r == -1);
  OAK_CHECK(rec->field_count == 0);
  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(GenericNativeFnNumberOk),
    OAK_TEST_ENTRY(GenericNativeFnStringOk),
    OAK_TEST_ENTRY(GenericNativeFnReturnTypeInferredOk),
    OAK_TEST_ENTRY(GenericNativeFnReturnTypeMismatchFails),
    OAK_TEST_ENTRY(GenericNativeFnSameConsistentOk),
    OAK_TEST_ENTRY(GenericNativeFnSameConflictFails),
    OAK_TEST_ENTRY(NonGenericNativeFnArgOk),
    OAK_TEST_ENTRY(NonGenericNativeFnWrongArgFails),
    OAK_TEST_ENTRY(GenericNativeRecordMethodCompiles),
    OAK_TEST_ENTRY(GenericNativeMethodReturnInferredOk),
    OAK_TEST_ENTRY(GenericNativeMethodReturnMismatchFails),
    OAK_TEST_ENTRY(GenericNativeMethodConsistentOk),
    OAK_TEST_ENTRY(GenericNativeMethodConflictFails),
    OAK_TEST_ENTRY(GenericNativeMapValueInferredOk),
    OAK_TEST_ENTRY(GenericNativeMapValueMismatchFails),
    OAK_TEST_ENTRY(GenericNativeMapKeyConsistentOk),
    OAK_TEST_ENTRY(GenericNativeMapKeyConflictFails),
    OAK_TEST_ENTRY(GenericNativeMapConcreteKeyMismatchFails),
    OAK_TEST_ENTRY(GenericNativeMapConcreteValueMismatchFails),
    OAK_TEST_ENTRY(GenericNativeMixConsistentOk),
    OAK_TEST_ENTRY(GenericNativeMixArrayShapeConflictFails),
    OAK_TEST_ENTRY(BindRejectsNullGenericParams),
    OAK_TEST_ENTRY(BindRejectsTooManyGenericParams),
    OAK_TEST_ENTRY(BindRejectsOutOfRangeParamRef),
    OAK_TEST_ENTRY(BindRejectsParamRefWithoutGenerics),
    OAK_TEST_ENTRY(BindRejectsUninferableReturnParam),
    OAK_TEST_ENTRY(BindRejectsGenericWithoutParamTypes),
    OAK_TEST_ENTRY(BindRejectsGenericMethodOnModuleType),
    OAK_TEST_ENTRY(BindRejectsFieldParamOnNonGenericRecord),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
