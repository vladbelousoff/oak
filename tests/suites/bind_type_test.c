/*
 * Native type binding: oak_bind_type, oak_bind_field, and value types.
 *
 * Merges what were two files (native record types, native value types). Both
 * describe C-owned data to the compiler; the difference is where instances
 * live. A RECORD type is refcounted behind a handle and exposes fields through
 * getters and setters. A VALUE type lives inline in the 8-byte oak_value_t,
 * which is why it can carry no fields at all and can only be reached through
 * methods.
 */

#include "oak_test_support.h"

#include "oak_type_id.h"
#include "oak_value.h"

#include <stdint.h>
#include <string.h>

OAK_TEST_SUITE(bind_type);

/* ------------------------------------------------------------------ */
/* Stubs                                                               */
/* ------------------------------------------------------------------ */

static int s_getter_calls;

static oak_value_t stub_getter(oak_value_t self, void* user_data)
{
  (void)self;
  (void)user_data;
  ++s_getter_calls;
  return OAK_VALUE_I32(0);
}

static void stub_setter(oak_value_t self, oak_value_t value, void* user_data)
{
  (void)self;
  (void)value;
  (void)user_data;
}

/* Registers a native record type with a `number` field named `field`. */
static oak_bind_type_t* bind_record_with_field(oak_compile_options_t* opts,
                                               const char* type_name,
                                               const char* field)
{
  oak_bind_type_t* t = oak_bind_type(opts, OAK_BIND_TYPE_RECORD, type_name);
  if (t)
    oak_bind_field(t,
                   &(oak_bind_field_t){
                       .name = field,
                       .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                       .getter = stub_getter,
                       .setter = null });
  return t;
}

/* ------------------------------------------------------------------ */
/* oak_bind_type                                                       */
/* ------------------------------------------------------------------ */

UTEST_F(bind_type, a_type_descriptor_is_created_and_registered)
{
  oak_compile_options_t opts;
  oak_bind_type_t* t;

  oak_compile_options_init(&opts, OAK_A);
  t = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "MyType");

  ASSERT_TRUE(t != null);
  EXPECT_STREQ("MyType", t->name);
  EXPECT_EQ(1u, oak_size(opts.native_types));

  oak_compile_options_free(&opts);
}

UTEST_F(bind_type, a_type_without_a_name_is_refused)
{
  oak_compile_options_t opts;

  oak_compile_options_init(&opts, OAK_A);
  EXPECT_TRUE(oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, null) == null);
  EXPECT_EQ(0u, oak_size(opts.native_types));
  oak_compile_options_free(&opts);
}

/* ------------------------------------------------------------------ */
/* oak_bind_field                                                      */
/* ------------------------------------------------------------------ */

/* Fields are assigned indices in registration order, which is the order the
 * compiler resolves them in -- so the order has to be preserved exactly. */
UTEST_F(bind_type, fields_are_recorded_in_registration_order)
{
  oak_compile_options_t opts;
  oak_bind_type_t* t;
  const oak_bind_field_t* fields;

  oak_compile_options_init(&opts, OAK_A);
  t = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "NTColor");
  ASSERT_TRUE(t != null);

  ASSERT_EQ(0,
            oak_bind_field(t,
                           &(oak_bind_field_t){
                               .name = "r",
                               .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .getter = stub_getter,
                               .setter = null }));
  ASSERT_EQ(0,
            oak_bind_field(t,
                           &(oak_bind_field_t){
                               .name = "g",
                               .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .getter = stub_getter,
                               .setter = stub_setter }));
  ASSERT_EQ(0,
            oak_bind_field(t,
                           &(oak_bind_field_t){
                               .name = "b",
                               .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .getter = stub_getter,
                               .setter = null }));

  ASSERT_EQ(3u, oak_size(t->fields));
  fields = OAK_CDATA(oak_bind_field_t, t->fields);
  EXPECT_STREQ("r", fields[0].name);
  EXPECT_STREQ("g", fields[1].name);
  EXPECT_STREQ("b", fields[2].name);
  EXPECT_EQ(OAK_TYPE_NUMBER, fields[0].type.id);
  EXPECT_TRUE(fields[0].getter == stub_getter);
  /* A field with no setter is read-only. */
  EXPECT_TRUE(fields[0].setter == null);
  EXPECT_TRUE(fields[1].setter == stub_setter);

  oak_compile_options_free(&opts);
}

/* A field the compiler could not read, name, or type would be unusable, so
 * each of those is refused rather than half-registered. */
UTEST_F(bind_type, malformed_fields_are_refused)
{
  oak_compile_options_t opts;
  oak_bind_type_t* t;

  oak_compile_options_init(&opts, OAK_A);
  t = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "NTBad");
  ASSERT_TRUE(t != null);

  /* No getter. */
  EXPECT_EQ(-1,
            oak_bind_field(t,
                           &(oak_bind_field_t){
                               .name = "x",
                               .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .getter = null,
                               .setter = null }));
  /* No name. */
  EXPECT_EQ(-1,
            oak_bind_field(t,
                           &(oak_bind_field_t){
                               .name = null,
                               .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .getter = stub_getter,
                               .setter = null }));
  /* No parameter block at all. */
  EXPECT_EQ(-1, oak_bind_field(t, null));

  EXPECT_EQ(0u, oak_size(t->fields));

  oak_compile_options_free(&opts);
}

UTEST_F(bind_type, a_duplicate_field_name_is_refused)
{
  oak_compile_options_t opts;
  oak_bind_type_t* t;

  oak_compile_options_init(&opts, OAK_A);
  t = bind_record_with_field(&opts, "NTDup", "x");
  ASSERT_TRUE(t != null);
  ASSERT_EQ(1u, oak_size(t->fields));

  EXPECT_EQ(-1,
            oak_bind_field(t,
                           &(oak_bind_field_t){
                               .name = "x",
                               .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .getter = stub_getter,
                               .setter = null }));
  EXPECT_EQ(1u, oak_size(t->fields));

  oak_compile_options_free(&opts);
}

/* ------------------------------------------------------------------ */
/* Native record types in Oak source                                   */
/* ------------------------------------------------------------------ */

/* Compiles `src` against a native record `NTVec { x, y }`. */
static oak_run_result_t compile_with_ntvec(oak_allocator_t* a,
                                           const char* src)
{
  oak_compile_options_t opts;
  oak_bind_type_t* t;
  oak_run_result_t r;

  oak_compile_options_init(&opts, a);
  t = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "NTVec");
  oak_bind_field(t,
                 &(oak_bind_field_t){
                     .name = "x",
                     .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                     .getter = stub_getter,
                     .setter = null });
  oak_bind_field(t,
                 &(oak_bind_field_t){
                     .name = "y",
                     .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                     .getter = stub_getter,
                     .setter = null });

  r = oak_test_source_opts(a, src, &opts);
  oak_compile_options_free(&opts);
  return r;
}

/* A bound type behaves like a declared one everywhere a type may appear. */
UTEST_F(bind_type, a_native_type_can_be_used_like_a_declared_one)
{
  static const char* const cases[] = {
    "fn length(v : NTVec) -> number { return v.x + v.y; }\n",
    "fn identity(v : NTVec) -> NTVec { return v; }\n",
    /* As the field type of an Oak record, so C and Oak data can nest.
     * Parenthesised so the two lines read as one program rather than as two
     * accidentally-adjacent array elements. */
    ("record Entity { name : string; transform : NTVec; }\n"
     "fn get_x(e : Entity) -> number { return e.transform.x; }\n"),
  };

  usize i;
  for (i = 0; i < oak_count_of(cases); ++i)
  {
    const oak_run_result_t r = compile_with_ntvec(OAK_A, cases[i]);
    if (!r.compiled)
    {
      UTEST_PRINTF("  row %u failed to compile\n", (unsigned)i);
      oak_test_explain(&r, cases[i]);
      *utest_result = UTEST_TEST_FAILURE;
    }
  }
}

/* Bound types get the same checking as declared ones -- unknown fields,
 * mismatched arguments, and no structural equivalence with Oak records. */
UTEST_F(bind_type, native_types_are_checked_like_declared_ones)
{
  static const oak_case_t cases[] = {
    { "fn read(v : NTVec) -> number { return v.missing; }\n", "missing" },
    { "fn take(v : NTVec) -> number { return v.x; }\n"
      "take('nope');\n",
      "expected type" },
    /* An Oak record with the same field layout is still a different type. */
    { "record Plain { x : number; y : number; }\n"
      "fn take(v : NTVec) -> number { return v.x; }\n"
      "let p = new Plain { x : 1, y : 2 };\n"
      "take(p);\n",
      "expected type" },
  };

  usize i;
  for (i = 0; i < oak_count_of(cases); ++i)
  {
    const oak_run_result_t r = compile_with_ntvec(OAK_A, cases[i].src);
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

/*
 * An Oak `record` whose name matches a bound native type is not a collision --
 * it is the source-level declaration of that native record, and the two sides
 * must agree. A matching declaration compiles; one that disagrees about the
 * fields is rejected rather than silently preferring either side.
 */
UTEST_F(bind_type, a_record_declaration_must_match_its_native_binding)
{
  oak_compile_options_t opts;
  oak_run_result_t matching;
  oak_run_result_t extra_field;
  oak_run_result_t wrong_name;

  oak_compile_options_init(&opts, OAK_A);
  bind_record_with_field(&opts, "Bound", "v");

  matching =
      oak_test_source_opts(OAK_A, "record Bound { v : number; }\n", &opts);
  EXPECT_TRUE(matching.compiled);
  if (!matching.compiled)
    oak_test_explain(&matching, "record Bound { v : number; }");

  extra_field = oak_test_source_opts(
      OAK_A, "record Bound { v : number; extra : number; }\n", &opts);
  EXPECT_FALSE(extra_field.compiled);
  EXPECT_TRUE(oak_test_contains(extra_field.diag, "binding has"));

  wrong_name =
      oak_test_source_opts(OAK_A, "record Bound { other : number; }\n", &opts);
  EXPECT_FALSE(wrong_name.compiled);

  oak_compile_options_free(&opts);
}

/* A native type with no bound fields cannot be declared with any. */
UTEST_F(bind_type, a_fieldless_native_type_rejects_a_record_body)
{
  oak_compile_options_t opts;
  oak_run_result_t r;

  oak_compile_options_init(&opts, OAK_A);
  ASSERT_TRUE(oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "Shared") != null);

  r = oak_test_source_opts(OAK_A, "record Shared { x : number; }\n", &opts);
  EXPECT_FALSE(r.compiled);

  oak_compile_options_free(&opts);
}

/* Registering the same native type name twice is ambiguous at every use. */
UTEST_F(bind_type, registering_the_same_type_name_twice_fails_to_compile)
{
  oak_compile_options_t opts;
  oak_run_result_t r;

  oak_compile_options_init(&opts, OAK_A);
  bind_record_with_field(&opts, "NTTwice", "v");
  bind_record_with_field(&opts, "NTTwice", "v");

  r = oak_test_source_opts(
      OAK_A, "fn f(t : NTTwice) -> number { return t.v; }\n", &opts);
  EXPECT_FALSE(r.compiled);
  EXPECT_TRUE(oak_test_contains(r.diag, "conflicts"));

  oak_compile_options_free(&opts);
}

/* A field with no setter is read-only, and the mutability model rejects the
 * write before it can ever reach the (absent) setter. */
UTEST_F(bind_type, assigning_to_a_read_only_native_field_is_rejected)
{
  const oak_run_result_t r = compile_with_ntvec(
      OAK_A, "fn write(v : NTVec) -> number { v.x = 1; return v.x; }\n");

  EXPECT_FALSE(r.compiled);
}

/* ------------------------------------------------------------------ */
/* Value types                                                         */
/* ------------------------------------------------------------------ */

/*
 * A "Handle" value type: the payload lives inline in the value word, so
 * instances are produced by a native factory and read back through a method.
 */
static oak_fn_call_result_t make_handle(oak_native_ctx_t* ctx,
                                        const oak_value_t* args,
                                        int argc,
                                        oak_value_t* out_result)
{
  (void)ctx;
  (void)args;
  (void)argc;
  *out_result = oak_native_value_new((void*)(intptr_t)42);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t handle_id(oak_native_ctx_t* ctx,
                                      const oak_value_t* args,
                                      int argc,
                                      oak_value_t* out_result)
{
  const intptr_t payload = (intptr_t)oak_native_value(args[0]);
  (void)ctx;
  (void)argc;
  *out_result = OAK_VALUE_I32((i32)payload);
  return OAK_FN_CALL_OK;
}

static oak_bind_type_t* bind_handle(oak_compile_options_t* opts)
{
  oak_bind_type_t* h = oak_bind_type(opts, OAK_BIND_TYPE_VALUE, "Handle");

  oak_bind_fn(opts,
              &(oak_bind_fn_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type = h,
                  .name = "id",
                  .impl = handle_id,
                  .arity = 0,
                  .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) });
  oak_bind_fn_global(opts,
                     &(oak_bind_global_fn_t){
                         .name = "make_handle",
                         .impl = make_handle,
                         .arity = 0,
                         .return_type = OAK_BIND_NATIVE(h) });
  return h;
}

static oak_run_result_t run_with_handle(oak_allocator_t* a, const char* src)
{
  oak_compile_options_t opts;
  oak_run_result_t r;

  oak_compile_options_init(&opts, a);
  bind_handle(&opts);
  r = oak_test_source_opts(a, src, &opts);
  oak_compile_options_free(&opts);
  return r;
}

/* The payload survives the round trip through the value word. */
UTEST_F(bind_type, a_value_type_carries_its_payload_inline)
{
  static const struct
  {
    const char* src;
    const char* want;
  } cases[] = {
    { "let h = make_handle();\nprint(h.id());\n", "42" },
    /* Copying a value type copies the payload; both stay usable. */
    { "let h = make_handle();\nlet c = h;\nprint(c.id());\nprint(h.id());\n",
      "42\n42" },
    /* Equality is payload identity. */
    { "let a = make_handle();\nlet b = make_handle();\nprint(a == b);\n",
      "true" },
    /* A value type is truthy in a condition. */
    { "let h = make_handle();\nif h { print('yes'); } else { print('no'); }\n",
      "yes" },
  };

  usize i;
  for (i = 0; i < oak_count_of(cases); ++i)
  {
    const oak_run_result_t r = run_with_handle(OAK_A, cases[i].src);
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
 * A value type has no heap object behind it, so it can hold no fields, cannot
 * be weakly referenced, and cannot be built with a record literal. Each of
 * these is refused at the point of definition or use.
 */
UTEST_F(bind_type, a_value_type_has_no_fields_and_no_identity_to_reference)
{
  oak_compile_options_t opts;
  oak_bind_type_t* h;

  oak_compile_options_init(&opts, OAK_A);
  h = bind_handle(&opts);
  ASSERT_TRUE(h != null);

  /* oak_bind_field refuses a VALUE type outright. */
  EXPECT_EQ(-1,
            oak_bind_field(h,
                           &(oak_bind_field_t){
                               .name = "x",
                               .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .getter = stub_getter,
                               .setter = null }));

  oak_compile_options_free(&opts);
}

UTEST_F(bind_type, value_types_reject_weak_literals_and_arithmetic)
{
  static const char* const cases[] = {
    /* No refcount, so nothing to weakly reference. */
    "record Holder { h : Handle weak; }\n",
    /* No fields, so no literal form. */
    "let h = new Handle { };\n",
    /* Not a number. */
    "let h = make_handle();\nlet x = h + 1;\n",
  };

  usize i;
  for (i = 0; i < oak_count_of(cases); ++i)
  {
    const oak_run_result_t r = run_with_handle(OAK_A, cases[i]);
    if (r.compiled)
    {
      UTEST_PRINTF("  row %u: expected rejection\n", (unsigned)i);
      oak_test_explain(&r, cases[i]);
      *utest_result = UTEST_TEST_FAILURE;
    }
  }
}

/* Two different value types are not comparable, even though both are just
 * payload words -- the type is part of the comparison. */
UTEST_F(bind_type, value_types_of_different_types_are_not_comparable)
{
  oak_compile_options_t opts;
  oak_bind_type_t* other;
  oak_run_result_t r;

  oak_compile_options_init(&opts, OAK_A);
  bind_handle(&opts);
  other = oak_bind_type(&opts, OAK_BIND_TYPE_VALUE, "Token");
  oak_bind_fn_global(&opts,
                     &(oak_bind_global_fn_t){
                         .name = "make_token",
                         .impl = make_handle,
                         .arity = 0,
                         .return_type = OAK_BIND_NATIVE(other) });

  r = oak_test_source_opts(OAK_A,
                           "let h = make_handle();\n"
                           "let t = make_token();\n"
                           "let same = h == t;\n",
                           &opts);
  EXPECT_FALSE(r.compiled);

  oak_compile_options_free(&opts);
}
