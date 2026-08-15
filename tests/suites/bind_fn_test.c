/*
 * Native function binding: oak_bind_fn_global and oak_bind_fn.
 *
 * Two halves. First, registration validation -- what the descriptor API
 * accepts and what it refuses. Second, what a registered binding does to
 * compilation and execution: arity and type checking at call sites, return
 * type inference, and the value actually reaching the VM.
 *
 * The native implementations here compute real answers rather than returning
 * a constant, so the runtime tests can assert the result instead of only that
 * nothing crashed.
 */

#include "oak_test_support.h"

#include "oak_type_id.h"
#include "oak_value.h"

#include <string.h>

OAK_TEST_SUITE(bind_fn);

static oak_fn_call_result_t native_add(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       const usize argc,
                                       oak_value_t* out_result)
{
  (void)call;
  *out_result = OAK_VALUE_I32(argc == 2 ? oak_as_i32(args[0]) +
                                              oak_as_i32(args[1])
                                        : 0);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t native_double(oak_native_call_t* call,
                                          const oak_value_t* args,
                                          const usize argc,
                                          oak_value_t* out_result)
{
  (void)call;
  *out_result = OAK_VALUE_I32(argc == 1 ? oak_as_i32(args[0]) * 2 : 0);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t native_answer(oak_native_call_t* call,
                                          const oak_value_t* args,
                                          const usize argc,
                                          oak_value_t* out_result)
{
  (void)call;
  (void)args;
  (void)argc;
  *out_result = OAK_VALUE_I32(42);
  return OAK_FN_CALL_OK;
}

/* Reports its own reason, with a formatted detail the VM could not have
 * produced on its own. */
static oak_fn_call_result_t native_explains(oak_native_call_t* call,
                                            const oak_value_t* args,
                                            const usize argc,
                                            oak_value_t* out_result)
{
  (void)args;
  (void)argc;
  (void)out_result;
  return oak_native_error(call, "tank empty at %d fathoms", 27);
}

/* Fails without saying why, so the VM's generic message still has to appear. */
static oak_fn_call_result_t native_silent(oak_native_call_t* call,
                                          const oak_value_t* args,
                                          const usize argc,
                                          oak_value_t* out_result)
{
  (void)call;
  (void)args;
  (void)argc;
  (void)out_result;
  return OAK_FN_CALL_RUNTIME_ERROR;
}

/* A void native leaves out_result untouched; the VM must supply none. */
static oak_fn_call_result_t native_void(oak_native_call_t* call,
                                        const oak_value_t* args,
                                        const usize argc,
                                        oak_value_t* out_result)
{
  (void)call;
  (void)args;
  (void)argc;
  (void)out_result;
  return OAK_FN_CALL_OK;
}

/* ---- attribute call hooks ------------------------------------------------
 *
 * on_call fires before every call to a declaration carrying the attribute, and
 * aborting it aborts the call. Nothing exercised this path before, in either
 * direction.
 */

static int s_hook_calls;
/* Copied, not aliased: fn_name points into the chunk, which the harness frees
 * before the assertions run. */
static char s_hook_last_fn[32];
static void* s_hook_ctx_user_data;
static int s_hook_should_abort;

static oak_fn_call_result_t counting_hook(oak_native_call_t* call,
                                          const char* fn_name,
                                          const oak_value_t* args,
                                          const usize argc,
                                          void* user_data)
{
  (void)args;
  (void)argc;
  (void)user_data;
  ++s_hook_calls;
  snprintf(s_hook_last_fn,
           sizeof(s_hook_last_fn),
           "%s",
           fn_name ? fn_name : "");
  s_hook_ctx_user_data = call->user_data;
  return s_hook_should_abort ? OAK_FN_CALL_RUNTIME_ERROR : OAK_FN_CALL_OK;
}

static int s_hook_marker;

/* Compiles and runs `src` with a "Guarded" attribute bound to counting_hook. */
static oak_run_result_t run_with_hook(oak_allocator_t* a, const char* src)
{
  oak_compile_options_t opts;
  oak_run_result_t r;

  oak_compile_options_init(&opts, a);
  oak_bind_attr(&opts,
                &(oak_bind_attr_t){ .name = "Guarded",
                                    .on_call = counting_hook,
                                    .user_data = &s_hook_marker });
  r = oak_test_source_opts(a, src, &opts);
  oak_compile_options_free(&opts);
  return r;
}

UTEST_F(bind_fn, an_attribute_hook_runs_before_each_call)
{
  s_hook_calls = 0;
  s_hook_should_abort = 0;
  s_hook_last_fn[0] = '\0';
  s_hook_ctx_user_data = null;

  const oak_run_result_t r =
      run_with_hook(OAK_A,
                    "@Guarded\n"
                    "fn watched(n : number) -> number { return n + 1; }\n"
                    "print(watched(1));\n"
                    "print(watched(2));\n");

  EXPECT_TRUE(r.compiled);
  OAK_EXPECT_ENUM(OAK_VM_OK, r.run);
  EXPECT_EQ(2, s_hook_calls);
  EXPECT_STREQ("watched", s_hook_last_fn);
  /* The binding's user_data reaches the hook on the call struct, as it does
   * for every other native callback; it used to arrive only as the trailing
   * parameter, with call->user_data left null. */
  EXPECT_TRUE(s_hook_ctx_user_data == &s_hook_marker);
  EXPECT_TRUE(oak_test_output_equals(r.out, "2\n3"));
  if (*utest_result != UTEST_TEST_PASSED)
    oak_test_explain(&r, "@Guarded fn watched ...");
}

UTEST_F(bind_fn, an_attribute_hook_can_abort_the_call)
{
  s_hook_calls = 0;
  s_hook_should_abort = 1;
  s_hook_last_fn[0] = '\0';

  const oak_run_result_t r =
      run_with_hook(OAK_A,
                    "@Guarded\n"
                    "fn watched(n : number) -> number { return n + 1; }\n"
                    "print(watched(1));\n");

  EXPECT_TRUE(r.compiled);
  OAK_EXPECT_ENUM(OAK_VM_RUNTIME_ERROR, r.run);
  EXPECT_EQ(1, s_hook_calls);
  EXPECT_TRUE(oak_test_contains(r.err, "attribute hook aborted"));
  /* The body never ran, so nothing was printed. */
  EXPECT_TRUE(oak_test_output_equals(r.out, ""));
  if (*utest_result != UTEST_TEST_PASSED)
    oak_test_explain(&r, "@Guarded fn watched ... (aborting)");

  s_hook_should_abort = 0;
}

/* An unattributed declaration is untouched, so the hook is attached per
 * declaration rather than installed globally. */
UTEST_F(bind_fn, an_unattributed_function_runs_no_hook)
{
  s_hook_calls = 0;
  s_hook_should_abort = 0;

  const oak_run_result_t r =
      run_with_hook(OAK_A,
                    "fn plain(n : number) -> number { return n + 1; }\n"
                    "print(plain(1));\n");

  EXPECT_TRUE(r.compiled);
  OAK_EXPECT_ENUM(OAK_VM_OK, r.run);
  EXPECT_EQ(0, s_hook_calls);
}

UTEST_F(bind_fn, a_global_function_is_recorded_with_its_descriptor)
{
  oak_compile_options_t opts;
  const oak_bind_global_fn_t* recorded;

  oak_compile_options_init(&opts, OAK_A);

  ASSERT_EQ(0,
            oak_bind_fn_global(
                &opts,
                &(oak_bind_global_fn_t){
                    .name = "my_global",
                    .impl = native_answer,
                    .param_count = 1,
                    .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) }));

  ASSERT_EQ(1u, oak_size(opts.native_global_fns));
  recorded = &OAK_CDATA(oak_bind_global_fn_t, opts.native_global_fns)[0];
  EXPECT_STREQ("my_global", recorded->name);
  EXPECT_EQ(1u, recorded->param_count);
  EXPECT_EQ(OAK_TYPE_NUMBER, recorded->return_type.id);
  OAK_EXPECT_ENUM(OAK_TYPE_KIND_SCALAR, recorded->return_type.kind);
  EXPECT_TRUE(recorded->impl == native_answer);

  oak_compile_options_free(&opts);
}

UTEST_F(bind_fn, an_instance_method_is_recorded_against_its_receiver)
{
  oak_compile_options_t opts;
  oak_bind_type_t* t;

  oak_compile_options_init(&opts, OAK_A);
  t = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "MyVec");
  ASSERT_TRUE(t != null);

  ASSERT_EQ(0,
            oak_bind_fn(&opts,
                        &(oak_bind_fn_t){
                            .kind = OAK_BIND_FN_INSTANCE_METHOD,
                            .receiver_type = t,
                            .name = "length",
                            .impl = native_answer,
                            .param_count = 0,
                            .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) }));

  ASSERT_EQ(1u, oak_size(opts.native_fns));
  EXPECT_TRUE(OAK_CDATA(oak_bind_fn_t, opts.native_fns)[0].receiver_type == t);
  EXPECT_EQ(0u, OAK_CDATA(oak_bind_fn_t, opts.native_fns)[0].param_count);

  oak_compile_options_free(&opts);
}

/* Malformed descriptors are refused outright, leaving nothing registered --
 * a half-registered binding would fail much later and much less clearly. */
UTEST_F(bind_fn, malformed_descriptors_are_refused)
{
  oak_compile_options_t opts;

  oak_compile_options_init(&opts, OAK_A);

  /* No name. */
  EXPECT_EQ(-1,
            oak_bind_fn_global(
                &opts,
                &(oak_bind_global_fn_t){
                    .name = null,
                    .impl = native_answer,
                    .param_count = 0,
                    .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) }));
  /* No implementation. */
  EXPECT_EQ(-1,
            oak_bind_fn_global(
                &opts,
                &(oak_bind_global_fn_t){
                    .name = "no_impl",
                    .impl = null,
                    .param_count = 0,
                    .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) }));
  /* An arity a call could not encode. Bytecode carries the argument count in
   * one byte, so anything past OAK_MAX_ARITY would wrap when the call is
   * emitted rather than fail. An arity mistakenly written as a negative int
   * lands here too, since the field is unsigned. */
  EXPECT_EQ(-1,
            oak_bind_fn_global(
                &opts,
                &(oak_bind_global_fn_t){
                    .name = "too_many",
                    .impl = native_answer,
                    .param_count = OAK_MAX_ARITY + 1u,
                    .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) }));
  EXPECT_EQ(-1,
            oak_bind_fn_global(
                &opts,
                &(oak_bind_global_fn_t){
                    .name = "wrapped",
                    .impl = native_answer,
                    .param_count = (usize)-1,
                    .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) }));

  EXPECT_EQ(0u, oak_size(opts.native_global_fns));

  /* The ceiling itself is accepted, so the check is a bound and not an
   * off-by-one that quietly costs a parameter. */
  EXPECT_EQ(0,
            oak_bind_fn_global(
                &opts,
                &(oak_bind_global_fn_t){
                    .name = "at_the_limit",
                    .impl = native_answer,
                    .param_count = OAK_MAX_ARITY,
                    .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) }));
  EXPECT_EQ(1u, oak_size(opts.native_global_fns));

  oak_compile_options_free(&opts);
}

/* Instance methods add implicit self to the VM arity, so the user-visible
 * ceiling is one lower than a global or static method. */
UTEST_F(bind_fn, an_instance_method_reserves_one_slot_for_self)
{
  oak_compile_options_t opts;
  oak_bind_type_t* t;

  oak_compile_options_init(&opts, OAK_A);
  t = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "Recv");
  ASSERT_TRUE(t != null);

  EXPECT_EQ(-1,
            oak_bind_fn(&opts,
                        &(oak_bind_fn_t){
                            .kind = OAK_BIND_FN_INSTANCE_METHOD,
                            .receiver_type = t,
                            .name = "too_many",
                            .impl = native_answer,
                            .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                            .param_count = OAK_MAX_ARITY }));
  EXPECT_EQ(0u, oak_size(opts.native_fns));

  EXPECT_EQ(0,
            oak_bind_fn(&opts,
                        &(oak_bind_fn_t){
                            .kind = OAK_BIND_FN_INSTANCE_METHOD,
                            .receiver_type = t,
                            .name = "at_the_limit",
                            .impl = native_answer,
                            .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                            .param_count = OAK_MAX_ARITY - 1u }));
  EXPECT_EQ(1u, oak_size(opts.native_fns));

  oak_compile_options_free(&opts);
}

UTEST_F(bind_fn, several_functions_can_be_registered)
{
  oak_compile_options_t opts;
  int i;

  oak_compile_options_init(&opts, OAK_A);

  /* oak_bind_fn_global() shallow-copies the descriptor, so the option list
   * ends up holding these pointers, not copies of the text. The storage has to
   * outlive every use of `opts` -- a buffer scoped to the loop body would
   * leave all eight entries pointing at a dead stack frame. */
  static char names[8][16];

  for (i = 0; i < 8; ++i)
  {
    snprintf(names[i], sizeof(names[i]), "fn_%d", i);
    EXPECT_EQ(0,
              oak_bind_fn_global(
                  &opts,
                  &(oak_bind_global_fn_t){
                      .name = names[i],
                      .impl = native_answer,
                      .param_count = 0,
                      .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) }));
  }
  EXPECT_EQ(8u, oak_size(opts.native_global_fns));

  oak_compile_options_free(&opts);
}

/* Registers `native_add(a, b) -> number` and runs `src` against it. */
static oak_run_result_t run_with_add(oak_allocator_t* a, const char* src)
{
  oak_compile_options_t opts;
  oak_run_result_t r;

  oak_compile_options_init(&opts, a);
  oak_bind_fn_global(&opts,
                     &(oak_bind_global_fn_t){
                         .name = "native_add",
                         .impl = native_add,
                         .param_count = 2,
                         .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) });
  oak_bind_fn_global(&opts,
                     &(oak_bind_global_fn_t){
                         .name = "native_double",
                         .impl = native_double,
                         .param_count = 1,
                         .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) });
  oak_bind_fn_global(&opts,
                     &(oak_bind_global_fn_t){
                         .name = "native_answer",
                         .impl = native_answer,
                         .param_count = 0,
                         .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) });
  oak_bind_fn_global(&opts,
                     &(oak_bind_global_fn_t){
                         .name = "native_void",
                         .impl = native_void,
                         .param_count = 0,
                         .return_type = OAK_BIND_SCALAR(OAK_TYPE_VOID) });
  oak_bind_fn_global(&opts,
                     &(oak_bind_global_fn_t){
                         .name = "native_explains",
                         .impl = native_explains,
                         .param_count = 0,
                         .return_type = OAK_BIND_SCALAR(OAK_TYPE_VOID) });
  oak_bind_fn_global(&opts,
                     &(oak_bind_global_fn_t){
                         .name = "native_silent",
                         .impl = native_silent,
                         .param_count = 0,
                         .return_type = OAK_BIND_SCALAR(OAK_TYPE_VOID) });
  r = oak_test_source_opts(a, src, &opts);
  oak_compile_options_free(&opts);
  return r;
}

/* A bound function is callable exactly like an Oak one, and its result is the
 * value the C implementation produced. */
UTEST_F(bind_fn, bound_functions_run_and_return_their_value)
{
  static const struct
  {
    const char* src;
    const char* want;
  } cases[] = {
    { "print(native_add(10, 32));\n", "42" },
    { "print(native_double(21));\n", "42" },
    { "print(native_answer());\n", "42" },
    /* Composition with Oak code on both sides. */
    { "fn twice(n : number) -> number { return native_double(n); }\n"
      "print(native_add(twice(10), 22));\n",
      "42" },
  };

  usize i;
  for (i = 0; i < OAK_COUNT_OF(cases); ++i)
  {
    const oak_run_result_t r = run_with_add(OAK_A, cases[i].src);
    if (!r.compiled || r.run != OAK_VM_OK ||
        !oak_test_output_equals(r.out, cases[i].want))
    {
      UTEST_PRINTF("  row %u: want '%s'\n", (unsigned)i, cases[i].want);
      oak_test_explain(&r, cases[i].src);
      *utest_result = UTEST_TEST_FAILURE;
    }
  }
}

/*
 * A failing native says why. Before oak_native_error every failure surfaced as
 * "native function '<name>' failed" no matter what went wrong, which is the
 * one thing an embedder most needs to distinguish. The message carries the
 * binding's name too, so the detail does not arrive anonymously.
 */
UTEST_F(bind_fn, a_failing_native_reports_its_own_reason)
{
  const oak_run_result_t r = run_with_add(OAK_A, "native_explains();\n");

  EXPECT_TRUE(r.compiled);
  OAK_EXPECT_ENUM(OAK_VM_RUNTIME_ERROR, r.run);
  EXPECT_TRUE(oak_test_contains(r.err, "tank empty at 27 fathoms"));
  EXPECT_TRUE(oak_test_contains(r.err, "native_explains"));
  /* The generic message must not also appear -- it would bury the real one. */
  EXPECT_FALSE(oak_test_contains(r.err, "failed"));
  if (*utest_result != UTEST_TEST_PASSED)
    oak_test_explain(&r, "native_explains();");
}

/* A native that fails without calling oak_native_error still reports: the
 * generic message is the fallback, not the only option. */
UTEST_F(bind_fn, a_silent_failure_still_reports_generically)
{
  const oak_run_result_t r = run_with_add(OAK_A, "native_silent();\n");

  EXPECT_TRUE(r.compiled);
  OAK_EXPECT_ENUM(OAK_VM_RUNTIME_ERROR, r.run);
  EXPECT_TRUE(oak_test_contains(r.err, "native function 'native_silent' failed"));
  if (*utest_result != UTEST_TEST_PASSED)
    oak_test_explain(&r, "native_silent();");
}

/* A void native may leave out_result untouched; the VM must fill in none
 * rather than passing the uninitialized slot along. */
UTEST_F(bind_fn, a_void_native_needs_no_out_value)
{
  const oak_run_result_t r =
      run_with_add(OAK_A, "for i from 0 to 1000 { native_void(); }\n");

  EXPECT_TRUE(r.compiled);
  OAK_EXPECT_ENUM(OAK_VM_OK, r.run);
}

/* Bound functions take part in the same compile-time checks as Oak ones. */
UTEST_F(bind_fn, call_sites_are_arity_and_type_checked)
{
  static const oak_case_t cases[] = {
    { "let x = native_add(1);\n", "expects 2 arguments, got 1" },
    { "let x = native_add(1, 2, 3);\n", "expects 2 arguments, got 3" },
    { "let x = native_answer(1);\n", "expects 0 arguments, got 1" },
    /* The declared return type flows into the enclosing call. */
    { "fn takes_string(s : string) -> string { return s; }\n"
      "let x = takes_string(native_answer());\n",
      "expected type 'string'" },
  };

  usize i;
  for (i = 0; i < OAK_COUNT_OF(cases); ++i)
  {
    const oak_run_result_t r = run_with_add(OAK_A, cases[i].src);
    if (r.compiled)
    {
      UTEST_PRINTF("  row %u: expected a compile error\n", (unsigned)i);
      oak_test_explain(&r, cases[i].src);
      *utest_result = UTEST_TEST_FAILURE;
    }
    else if (!oak_test_contains(r.diag, cases[i].want))
    {
      UTEST_PRINTF("  row %u: want substring '%s'\n",
                   (unsigned)i,
                   cases[i].want);
      oak_test_explain(&r, cases[i].src);
      *utest_result = UTEST_TEST_FAILURE;
    }
  }
}

/* The declared return type is what the compiler infers at the call site. */
UTEST_F(bind_fn, the_declared_return_type_is_inferred)
{
  static const oak_case_t cases[] = {
    { "fn takes_number(n : number) -> number { return n; }\n"
      "print(takes_number(native_answer()));\n",
      null },
  };

  usize i;
  for (i = 0; i < OAK_COUNT_OF(cases); ++i)
  {
    const oak_run_result_t r = run_with_add(OAK_A, cases[i].src);
    EXPECT_TRUE(r.compiled);
    OAK_EXPECT_ENUM(OAK_VM_OK, r.run);
  }
}

/* Two bindings under one name would make the call site ambiguous. */
UTEST_F(bind_fn, a_duplicate_global_name_fails_to_compile)
{
  oak_compile_options_t opts;
  oak_run_result_t r;

  oak_compile_options_init(&opts, OAK_A);
  oak_bind_fn_global(&opts,
                     &(oak_bind_global_fn_t){
                         .name = "dup",
                         .impl = native_answer,
                         .param_count = 0,
                         .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) });
  oak_bind_fn_global(&opts,
                     &(oak_bind_global_fn_t){
                         .name = "dup",
                         .impl = native_answer,
                         .param_count = 0,
                         .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) });

  r = oak_test_source_opts(OAK_A, "let x = dup();\n", &opts);
  EXPECT_FALSE(r.compiled);
  EXPECT_TRUE(oak_test_contains(r.diag, "duplicate native function"));

  oak_compile_options_free(&opts);
}

/* A native function has exactly one arity, so its printed form says
 * "arity=N" -- never a range, which an earlier variadic design produced. */
UTEST_F(bind_fn, a_native_function_formats_with_a_single_arity)
{
  oak_obj_native_fn_t* fn =
      oak_native_fn_new(OAK_A, native_answer, 3, "my_fn", null);
  char buf[128];

  ASSERT_TRUE(fn != null);
  oak_native_fn_format(buf, sizeof(buf), fn);

  EXPECT_TRUE(strstr(buf, "arity=3") != null);
  EXPECT_TRUE(strstr(buf, "arity=3..") == null);
  EXPECT_TRUE(strstr(buf, "my_fn") != null);

  oak_obj_decref((oak_obj_t*)fn);
}

/* An unnamed native still formats without reading a null name. */
UTEST_F(bind_fn, an_anonymous_native_function_formats)
{
  oak_obj_native_fn_t* fn =
      oak_native_fn_new(OAK_A, native_answer, 1, null, null);
  char buf[128];

  ASSERT_TRUE(fn != null);
  oak_native_fn_format(buf, sizeof(buf), fn);
  EXPECT_TRUE(strstr(buf, "arity=1") != null);

  oak_obj_decref((oak_obj_t*)fn);
}

/*
 * A parameter can be typed as a native enum.
 *
 * Registering the enum and then declaring the parameter as OAK_TYPE_NUMBER
 * makes the compiler reject `f(Colour.Red)` -- the argument's type is the enum,
 * not `number`. OAK_BIND_ENUM names the enum itself, which is the only way to
 * express such a signature; without it a binding taking an enum has to leave
 * the parameter out of param_types and lose call-site checking entirely.
 */
static oak_run_result_t run_with_enum_param(oak_allocator_t* a,
                                            const char* src,
                                            const int use_enum_ref)
{
  oak_compile_options_t opts;
  oak_run_result_t r;

  oak_compile_options_init(&opts, a);
  oak_bind_enum_t* colour = oak_bind_enum(&opts, "Colour");
  oak_bind_enum_variant(colour, "Red", 0);
  oak_bind_enum_variant(colour, "Green", 1);

  const oak_bind_type_ref_t params[] = {
    use_enum_ref ? OAK_BIND_ENUM(colour) : OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
  };
  oak_bind_fn_global(&opts,
                     &(oak_bind_global_fn_t){
                         .name = "takes_colour",
                         .impl = native_double,
                         .param_count = 1,
                         .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                         .param_types = params });
  r = oak_test_source_opts(a, src, &opts);
  oak_compile_options_free(&opts);
  return r;
}

UTEST_F(bind_fn, a_parameter_can_be_typed_as_a_native_enum)
{
  static const char* const src = "let x = takes_colour(Colour.Green);\n";

  /* Declared as the enum: the variant is accepted and reaches the native fn,
   * which doubles its ordinal (Green == 1). */
  const oak_run_result_t ok = run_with_enum_param(OAK_A, src, 1);
  EXPECT_TRUE(ok.compiled);
  if (!ok.compiled)
    oak_test_explain(&ok, src);
  OAK_EXPECT_ENUM(OAK_VM_OK, ok.run);

  /* Declared as a plain number, the same call fails to compile. This is the
   * regression OAK_BIND_ENUM exists to fix. */
  const oak_run_result_t bad = run_with_enum_param(OAK_A, src, 0);
  EXPECT_FALSE(bad.compiled);
  if (bad.compiled)
    oak_test_explain(&bad, src);
  else
    EXPECT_TRUE(oak_test_contains(bad.diag, "expected type 'number'"));
}
