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

#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Compile / run helpers
 * ------------------------------------------------------------------------- */

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

static enum oak_test_status_t run_ok(const char* source,
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

  struct oak_vm_t vm;
  oak_vm_init(&vm, oak_test_allocator());
  const enum oak_vm_result_t r = oak_vm_run(&vm, cr.chunk);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
  oak_parser_free(&pr);
  oak_lexer_free(lex);

  OAK_CHECK(r == OAK_VM_OK);
  return OAK_TEST_OK;
}

/* A "handle" value type carries a small integer payload inline. */
static enum oak_fn_call_result_t make_handle_impl(struct oak_native_ctx_t* ctx,
                                                 const struct oak_value_t* args,
                                                 int argc,
                                                 struct oak_value_t* out_result)
{
  (void)ctx;
  (void)args;
  (void)argc;
  *out_result = oak_native_value_new((void*)(intptr_t)42);
  return OAK_FN_CALL_OK;
}

/* Handle.id() recovers the inline payload and returns it as a number. */
static enum oak_fn_call_result_t handle_id_impl(struct oak_native_ctx_t* ctx,
                                               const struct oak_value_t* args,
                                               int argc,
                                               struct oak_value_t* out_result)
{
  (void)ctx;
  (void)argc;
  const intptr_t payload = (intptr_t)oak_native_value(args[0]);
  *out_result = OAK_VALUE_I32((i32)payload);
  return OAK_FN_CALL_OK;
}


/* Register the Handle value type, its id() method, and a make_handle()
 * constructor. */
static void bind_handle(struct oak_compile_options_t* opts)
{
  struct oak_bind_type_t* h =
      oak_bind_type(opts, OAK_BIND_TYPE_VALUE, "Handle");
  oak_bind_fn(opts,
              &(struct oak_bind_fn_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type = h,
                  .name = "id",
                  .impl = handle_id_impl,
                  .arity = 0,
                  .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
              });
  oak_bind_fn_global(opts,
                     &(struct oak_bind_global_fn_t){
                         .name = "make_handle",
                         .impl = make_handle_impl,
                         .arity = 0,
                         .return_type = OAK_BIND_NATIVE(h),
                     });
}

/* =========================================================================
 * Inline value types
 * ========================================================================= */

/* A value type can be produced by a native fn and used through a method. */
OAK_TEST_DECL(ValueTypeMethodRunsOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_handle(&opts);
  const enum oak_test_status_t s =
      run_ok("let h = make_handle();\nprint(h.id());\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* Reassigning / copying a value-type local is a plain bitwise copy: no
 * refcount churn, so this must run without leaks or double-frees. */
OAK_TEST_DECL(ValueTypeCopyRunsOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_handle(&opts);
  const enum oak_test_status_t s =
      run_ok("let a = make_handle();\n"
             "let b = a;\n"
             "print(b.id());\n",
             &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* oak_bind_field is rejected on a value type — they have no data fields. */
OAK_TEST_DECL(ValueTypeRejectsFields)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  struct oak_bind_type_t* h =
      oak_bind_type(&opts, OAK_BIND_TYPE_VALUE, "Handle");
  const int r = oak_bind_field(
      h,
      &(struct oak_bind_field_t){ .name = "x",
                                  .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                                  .getter = null });
  OAK_CHECK(r == -1);
  OAK_CHECK(oak_dynarr_count(h->fields) == 0);
  oak_compile_options_free(&opts);
  return OAK_TEST_OK;
}

/* Inline value equality is payload identity. */
OAK_TEST_DECL(ValueTypeEqualityIsPayloadIdentity)
{
  const struct oak_value_t a = oak_native_value_new((void*)(intptr_t)7);
  const struct oak_value_t b = oak_native_value_new((void*)(intptr_t)7);
  const struct oak_value_t c = oak_native_value_new((void*)(intptr_t)9);
  OAK_CHECK(oak_value_equal(a, b));
  OAK_CHECK(!oak_value_equal(a, c));
  OAK_CHECK(oak_is_native_value(a));
  OAK_CHECK((intptr_t)oak_native_value(a) == 7);
  return OAK_TEST_OK;
}

/* A value type is non-refcounted, so `weak` cannot apply to it.  This must be
 * rejected at compile time, not deferred to a runtime WEAKEN failure. */
OAK_TEST_DECL(ValueTypeWeakRejected)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_handle(&opts);
  const enum oak_test_status_t s =
      compile_fails("record R { h: Handle weak; }\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* Register a second, unrelated value type alongside Handle. */
static void bind_token(struct oak_compile_options_t* opts)
{
  struct oak_bind_type_t* t =
      oak_bind_type(opts, OAK_BIND_TYPE_VALUE, "Token");
  oak_bind_fn_global(opts,
                     &(struct oak_bind_global_fn_t){
                         .name = "make_token",
                         .impl = make_handle_impl,
                         .arity = 0,
                         .return_type = OAK_BIND_NATIVE(t),
                     });
}

/* Comparing two different value types must be rejected even though they share
 * OAK_TAG_NATIVE and could carry identical payloads at runtime. */
OAK_TEST_DECL(ValueTypeCrossTypeEqualityRejected)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_handle(&opts);
  bind_token(&opts);
  const enum oak_test_status_t s = compile_fails(
      "let h = make_handle();\nlet t = make_token();\nprint(h == t);\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* Comparing two values of the same value type is allowed. */
OAK_TEST_DECL(ValueTypeSameTypeEqualityOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_handle(&opts);
  const enum oak_test_status_t s = run_ok(
      "let a = make_handle();\nlet b = make_handle();\nprint(a == b);\n",
      &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* A value type cannot be constructed with a record literal — that would create
 * a heap OAK_TAG_OBJ record, violating the inline invariant. */
OAK_TEST_DECL(ValueTypeRecordLiteralRejected)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_handle(&opts);
  const enum oak_test_status_t s =
      compile_fails("let h = new Handle {};\n", &opts);
  oak_compile_options_free(&opts);
  return s;
}

/* A value type used in a condition is truthy (like a heap object/record),
 * not silently false. */
OAK_TEST_DECL(ValueTypeTruthyInConditionRunsOk)
{
  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, oak_test_allocator());
  bind_handle(&opts);
  const enum oak_test_status_t s =
      run_ok("let h = make_handle();\n"
             "if h { print(h.id()); } else { print(-1); }\n",
             &opts);
  oak_compile_options_free(&opts);
  return s;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(ValueTypeMethodRunsOk),
    OAK_TEST_ENTRY(ValueTypeCopyRunsOk),
    OAK_TEST_ENTRY(ValueTypeRejectsFields),
    OAK_TEST_ENTRY(ValueTypeEqualityIsPayloadIdentity),
    OAK_TEST_ENTRY(ValueTypeWeakRejected),
    OAK_TEST_ENTRY(ValueTypeCrossTypeEqualityRejected),
    OAK_TEST_ENTRY(ValueTypeSameTypeEqualityOk),
    OAK_TEST_ENTRY(ValueTypeRecordLiteralRejected),
    OAK_TEST_ENTRY(ValueTypeTruthyInConditionRunsOk),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
