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
                       .setter = OAK_NULL });
  return t;
}

UTEST_F(bind_type, a_type_descriptor_is_created_and_registered)
{
  oak_compile_options_t opts;
  oak_bind_type_t* t;

  oak_compile_options_init(&opts, OAK_A);
  t = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "MyType");

  ASSERT_TRUE(t != OAK_NULL);
  EXPECT_STREQ("MyType", t->name);
  EXPECT_EQ(1u, oak_size(opts.native_types));

  oak_compile_options_free(&opts);
}

UTEST_F(bind_type, a_type_without_a_name_is_refused)
{
  oak_compile_options_t opts;

  oak_compile_options_init(&opts, OAK_A);
  EXPECT_TRUE(oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, OAK_NULL) == OAK_NULL);
  EXPECT_EQ(0u, oak_size(opts.native_types));
  oak_compile_options_free(&opts);
}

/* Fields are assigned indices in registration order, which is the order the
 * compiler resolves them in -- so the order has to be preserved exactly. */
UTEST_F(bind_type, fields_are_recorded_in_registration_order)
{
  oak_compile_options_t opts;
  oak_bind_type_t* t;
  const oak_bind_field_t* fields;

  oak_compile_options_init(&opts, OAK_A);
  t = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "NTColor");
  ASSERT_TRUE(t != OAK_NULL);

  ASSERT_EQ(0,
            oak_bind_field(t,
                           &(oak_bind_field_t){
                               .name = "r",
                               .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .getter = stub_getter,
                               .setter = OAK_NULL }));
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
                               .setter = OAK_NULL }));

  ASSERT_EQ(3u, oak_size(t->fields));
  fields = OAK_CDATA(oak_bind_field_t, t->fields);
  EXPECT_STREQ("r", fields[0].name);
  EXPECT_STREQ("g", fields[1].name);
  EXPECT_STREQ("b", fields[2].name);
  EXPECT_EQ(OAK_TYPE_NUMBER, fields[0].type.id);
  EXPECT_TRUE(fields[0].getter == stub_getter);
  /* A field with no setter is read-only. */
  EXPECT_TRUE(fields[0].setter == OAK_NULL);
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
  ASSERT_TRUE(t != OAK_NULL);

  /* No getter. */
  EXPECT_EQ(-1,
            oak_bind_field(t,
                           &(oak_bind_field_t){
                               .name = "x",
                               .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .getter = OAK_NULL,
                               .setter = OAK_NULL }));
  /* No name. */
  EXPECT_EQ(-1,
            oak_bind_field(t,
                           &(oak_bind_field_t){
                               .name = OAK_NULL,
                               .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .getter = stub_getter,
                               .setter = OAK_NULL }));
  /* No parameter block at all. */
  EXPECT_EQ(-1, oak_bind_field(t, OAK_NULL));

  EXPECT_EQ(0u, oak_size(t->fields));

  oak_compile_options_free(&opts);
}

UTEST_F(bind_type, a_duplicate_field_name_is_refused)
{
  oak_compile_options_t opts;
  oak_bind_type_t* t;

  oak_compile_options_init(&opts, OAK_A);
  t = bind_record_with_field(&opts, "NTDup", "x");
  ASSERT_TRUE(t != OAK_NULL);
  ASSERT_EQ(1u, oak_size(t->fields));

  EXPECT_EQ(-1,
            oak_bind_field(t,
                           &(oak_bind_field_t){
                               .name = "x",
                               .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .getter = stub_getter,
                               .setter = OAK_NULL }));
  EXPECT_EQ(1u, oak_size(t->fields));

  oak_compile_options_free(&opts);
}

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
                     .setter = OAK_NULL });
  oak_bind_field(t,
                 &(oak_bind_field_t){
                     .name = "y",
                     .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                     .getter = stub_getter,
                     .setter = OAK_NULL });

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
  for (i = 0; i < OAK_COUNT_OF(cases); ++i)
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
  for (i = 0; i < OAK_COUNT_OF(cases); ++i)
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

#define IVALUED_SRC(implements_clause)                                         \
  "interface IValued { fn value() -> number; }\n"                              \
  "record Bound" implements_clause " {\n"                                      \
  "  v : number;\n"                                                            \
  "  fn value() -> number { return self.v; }\n"                                \
  "}\n"                                                                        \
  "fn show(x : IValued) { print(x.value()); }\n"                               \
  "show(new Bound { v: 1 });\n"

/*
 * A native record declares its interfaces twice -- through
 * oak_bind_type_implements and in the `implements` clause of the Oak
 * declaration that mirrors it. Neither side wins: they have to agree, exactly
 * as the field lists do.
 */
UTEST_F(bind_type, a_native_records_interfaces_must_match_its_binding)
{
  oak_compile_options_t opts;
  oak_bind_type_t* t;
  oak_run_result_t agreeing;
  oak_run_result_t binding_only;
  oak_run_result_t decl_only;

  oak_compile_options_init(&opts, OAK_A);
  t = bind_record_with_field(&opts, "Bound", "v");
  ASSERT_TRUE(t != OAK_NULL);
  EXPECT_EQ(0, oak_bind_type_implements(t, "IValued"));
  EXPECT_EQ(1u, oak_size(t->interface_names));

  agreeing = oak_test_source_opts(
      OAK_A, IVALUED_SRC(" implements IValued"),
      &opts);
  EXPECT_TRUE(agreeing.compiled);
  if (!agreeing.compiled)
    oak_test_explain(&agreeing, "native record implementing IValued");

  /* Bound in C, silent in Oak. */
  binding_only = oak_test_source_opts(
      OAK_A, IVALUED_SRC(""), &opts);
  EXPECT_FALSE(binding_only.compiled);
  EXPECT_TRUE(oak_test_contains(binding_only.diag,
                                "does not say 'implements IValued'"));

  /* Declared in Oak, silent in C. */
  {
    oak_compile_options_t bare;
    oak_compile_options_init(&bare, OAK_A);
    bind_record_with_field(&bare, "Bound", "v");
    decl_only = oak_test_source_opts(
        OAK_A, IVALUED_SRC(" implements IValued"),
        &bare);
    EXPECT_FALSE(decl_only.compiled);
    EXPECT_TRUE(oak_test_contains(decl_only.diag, "its binding does not"));
    oak_compile_options_free(&bare);
  }

  oak_compile_options_free(&opts);
}

/* One oak_compile_options_t compiles many programs, and only some of them will
 * declare the interface a binding names. Where it is absent the binding's
 * claim is simply inert -- it must not turn every other program that uses the
 * type into a compile error. */
UTEST_F(bind_type, a_bound_interface_is_inert_where_it_is_not_declared)
{
  oak_compile_options_t opts;
  oak_bind_type_t* t;
  oak_run_result_t unrelated;

  oak_compile_options_init(&opts, OAK_A);
  t = bind_record_with_field(&opts, "Bound", "v");
  ASSERT_TRUE(t != OAK_NULL);
  EXPECT_EQ(0, oak_bind_type_implements(t, "IValued"));

  unrelated = oak_test_source_opts(OAK_A, "print(1);\n", &opts);
  EXPECT_TRUE(unrelated.compiled);
  if (!unrelated.compiled)
    oak_test_explain(&unrelated, "print(1);");

  oak_compile_options_free(&opts);
}

#undef IVALUED_SRC

/* The same interface named twice on one side is a mistake, not a restatement,
 * and the rejection has to reach the embedder rather than sit in the -1 that
 * oak_bind_* calls routinely have discarded. */
UTEST_F(bind_type, an_interface_named_twice_is_refused)
{
  oak_compile_options_t opts;
  oak_bind_type_t* t;
  oak_run_result_t surfaced;

  oak_compile_options_init(&opts, OAK_A);
  t = bind_record_with_field(&opts, "Bound", "v");
  ASSERT_TRUE(t != OAK_NULL);

  EXPECT_EQ(0, oak_bind_type_implements(t, "IValued"));
  EXPECT_NE(0, oak_bind_type_implements(t, "IValued"));
  EXPECT_EQ(1u, oak_size(t->interface_names));
  EXPECT_NE(0, oak_bind_type_implements(t, OAK_NULL));
  EXPECT_NE(0, oak_bind_type_implements(t, ""));
  EXPECT_NE(0, oak_bind_type_implements(OAK_NULL, "IValued"));

  surfaced = oak_test_source_opts(
      OAK_A,
      "interface IValued { fn value() -> number; }\n"
      "record Bound implements IValued {\n"
      "  v : number;\n"
      "  fn value() -> number { return self.v; }\n"
      "}\n",
      &opts);
  EXPECT_FALSE(surfaced.compiled);
  EXPECT_TRUE(oak_test_contains(surfaced.diag,
                                "duplicate implemented interface 'IValued'"));

  oak_compile_options_free(&opts);
}

/*
 * A field bound with OAK_BIND_NATIVE or OAK_BIND_ENUM names its type through a
 * descriptor rather than a type id, so the id is only known once registration
 * has run. The cross-check against an Oak `record` declaration used to lower
 * the binding side by copying `id` straight across, which reads OAK_TYPE_VOID
 * for both forms -- so a declaration that agreed perfectly was rejected as a
 * mismatch, and neither form could appear in a stub. Both sides now go through
 * oak_lower_bind_ref.
 */
UTEST_F(bind_type, descriptor_typed_fields_match_their_record_declaration)
{
  oak_compile_options_t opts;
  oak_bind_type_t* inner;
  oak_bind_type_t* owner;
  oak_bind_enum_t* colour;
  oak_run_result_t matching;
  oak_run_result_t wrong_type;

  oak_compile_options_init(&opts, OAK_A);
  inner = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "Inner");
  ASSERT_TRUE(inner != OAK_NULL);
  colour = oak_bind_enum(&opts, "Tint");
  ASSERT_TRUE(colour != OAK_NULL);
  EXPECT_EQ(0, oak_bind_enum_variant(colour, "Red", 0));

  owner = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "Owner");
  ASSERT_TRUE(owner != OAK_NULL);
  EXPECT_EQ(0,
            oak_bind_field(owner,
                           &(oak_bind_field_t){
                               .name = "inner",
                               .type = OAK_BIND_NATIVE(inner),
                               .getter = stub_getter,
                               .setter = OAK_NULL }));
  EXPECT_EQ(0,
            oak_bind_field(owner,
                           &(oak_bind_field_t){
                               .name = "tint",
                               .type = OAK_BIND_ENUM(colour),
                               .getter = stub_getter,
                               .setter = OAK_NULL }));

  matching = oak_test_source_opts(
      OAK_A, "record Owner { inner : Inner; tint : Tint; }\n", &opts);
  EXPECT_TRUE(matching.compiled);
  if (!matching.compiled)
    oak_test_explain(&matching, "record Owner { inner : Inner; tint : Tint; }");

  /* The check still has teeth: naming the wrong type is still a mismatch. */
  wrong_type = oak_test_source_opts(
      OAK_A, "record Owner { inner : number; tint : Tint; }\n", &opts);
  EXPECT_FALSE(wrong_type.compiled);
  EXPECT_TRUE(oak_test_contains(wrong_type.diag, "does not match"));

  oak_compile_options_free(&opts);
}

/*
 * oak_arg_self checks that the receiver really is the bound type before it
 * hands back the instance pointer. oak_native_instance only asserts "some
 * native record" and then reinterprets whatever C struct is behind it, so a
 * receiver of the wrong bound type used to be read as the wrong struct.
 *
 * Oak call sites are type-checked at compile time, so C is the only way to
 * reach this: oak_vm_call takes arbitrary values.
 */
static oak_fn_call_result_t stub_method(oak_native_call_t* call,
                                        const oak_value_t* args,
                                        const usize argc,
                                        oak_value_t* out_result)
{
  (void)call;
  (void)args;
  (void)argc;
  *out_result = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

static int s_wrong_self_instance_seen;

static oak_fn_call_result_t reads_own_instance(oak_native_call_t* call,
                                               const oak_value_t* args,
                                               const usize argc,
                                               oak_value_t* out_result)
{
  void* instance;
  if (!oak_arg_self(call, args, argc, &instance))
    return OAK_FN_CALL_RUNTIME_ERROR;
  s_wrong_self_instance_seen = 1;
  *out_result = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

UTEST_F(bind_type, a_receiver_of_the_wrong_native_type_is_refused)
{
  oak_compile_options_t opts;
  oak_bind_type_t* mine;
  oak_bind_type_t* theirs;
  oak_vm_t vm;
  oak_value_t right;
  oak_value_t wrong;
  oak_value_t result = OAK_VALUE_NONE;
  oak_native_call_t call;
  int mine_instance = 7;
  int theirs_instance = 9;

  oak_compile_options_init(&opts, OAK_A);
  mine = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "Mine");
  theirs = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "Theirs");
  ASSERT_TRUE(mine != OAK_NULL && theirs != OAK_NULL);

  /* The receiver check compares descriptors by identity, so nothing here needs
   * compiling -- only a VM to own the records and carry the error. */
  oak_vm_init(&vm, OAK_A);
  right = oak_vm_native_record_new(&vm, mine, &mine_instance);
  wrong = oak_vm_native_record_new(&vm, theirs, &theirs_instance);

  call.vm = &vm;
  call.allocator = OAK_A;
  call.user_data = OAK_NULL;
  call.fn_name = "peek";
  call.self_type = mine;

  s_wrong_self_instance_seen = 0;
  OAK_EXPECT_ENUM(OAK_FN_CALL_RUNTIME_ERROR,
                  reads_own_instance(&call, &wrong, 1, &result));
  EXPECT_EQ(0, s_wrong_self_instance_seen);
  EXPECT_TRUE(oak_vm_last_error(&vm) != OAK_NULL);

  /* The right receiver still goes through, so the check is not a blanket no. */
  OAK_EXPECT_ENUM(OAK_FN_CALL_OK,
                  reads_own_instance(&call, &right, 1, &result));
  EXPECT_EQ(1, s_wrong_self_instance_seen);

  oak_obj_decref(oak_value_obj_resolve(right));
  oak_obj_decref(oak_value_obj_resolve(wrong));
  oak_vm_free(&vm);
  oak_compile_options_free(&opts);
}

/*
 * A type bound into a module is reachable only through that module. It used to
 * be registered in the global namespace as well, so oak_stdlib_register made a
 * bare `File` -- and every one of its methods -- visible in every program,
 * alongside the `io.File` that was intended. The enum and global-function
 * passes already filtered on module_name; the type and method passes did not.
 */
UTEST_F(bind_type, a_module_scoped_type_is_not_also_a_global)
{
  oak_compile_options_t opts;
  oak_bind_type_t* scoped;
  oak_bind_type_t* global;
  oak_run_result_t bare;
  oak_run_result_t unscoped;

  oak_compile_options_init(&opts, OAK_A);
  scoped = oak_bind_type_in_module(&opts, "gear", OAK_BIND_TYPE_RECORD, "Cog");
  ASSERT_TRUE(scoped != OAK_NULL);
  EXPECT_EQ(0,
            oak_bind_fn(&opts,
                        &(oak_bind_fn_t){
                            .kind = OAK_BIND_FN_INSTANCE_METHOD,
                            .receiver_type = scoped,
                            .name = "teeth",
                            .impl = stub_method,
                            .param_count = 0,
                            .return_type =
                                OAK_BIND_SCALAR(OAK_TYPE_NUMBER) }));

  /* A bare type name alone proves nothing: Oak interns any name written in a
   * type position, so `c : Cog` compiles either way. What leaked was the
   * binding itself -- the record and its methods -- so call one. */
  bare = oak_test_source_opts(
      OAK_A, "fn f(c : Cog) -> number { return c.teeth(); }\n", &opts);
  EXPECT_FALSE(bare.compiled);
  if (bare.compiled)
    oak_test_explain(&bare, "fn f(c : Cog) -> number { return c.teeth(); }");

  /* A type bound with no module is still global, so this is a filter on
   * module_name and not a blanket refusal. */
  global = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "Sprocket");
  ASSERT_TRUE(global != OAK_NULL);
  EXPECT_EQ(0,
            oak_bind_fn(&opts,
                        &(oak_bind_fn_t){
                            .kind = OAK_BIND_FN_INSTANCE_METHOD,
                            .receiver_type = global,
                            .name = "teeth",
                            .impl = stub_method,
                            .param_count = 0,
                            .return_type =
                                OAK_BIND_SCALAR(OAK_TYPE_NUMBER) }));
  unscoped = oak_test_source_opts(
      OAK_A, "fn f(s : Sprocket) -> number { return s.teeth(); }\n", &opts);
  EXPECT_TRUE(unscoped.compiled);
  if (!unscoped.compiled)
    oak_test_explain(&unscoped,
                     "fn f(s : Sprocket) -> number { return s.teeth(); }");

  oak_compile_options_free(&opts);
}

/* A native type with no bound fields cannot be declared with any. */
UTEST_F(bind_type, a_fieldless_native_type_rejects_a_record_body)
{
  oak_compile_options_t opts;
  oak_run_result_t r;

  oak_compile_options_init(&opts, OAK_A);
  ASSERT_TRUE(oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "Shared") != OAK_NULL);

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

/*
 * A "Handle" value type: the payload lives inline in the value word, so
 * instances are produced by a native factory and read back through a method.
 */
static oak_fn_call_result_t make_handle(oak_native_call_t* call,
                                        const oak_value_t* args,
                                        const usize argc,
                                        oak_value_t* out_result)
{
  (void)call;
  (void)args;
  (void)argc;
  *out_result = oak_native_value_new((void*)(intptr_t)42);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t handle_id(oak_native_call_t* call,
                                      const oak_value_t* args,
                                      const usize argc,
                                      oak_value_t* out_result)
{
  const intptr_t payload = (intptr_t)oak_native_value(args[0]);
  (void)call;
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
                  .param_count = 0,
                  .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER) });
  oak_bind_fn_global(opts,
                     &(oak_bind_global_fn_t){
                         .name = "make_handle",
                         .impl = make_handle,
                         .param_count = 0,
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
  for (i = 0; i < OAK_COUNT_OF(cases); ++i)
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
  ASSERT_TRUE(h != OAK_NULL);

  /* oak_bind_field refuses a VALUE type outright. */
  EXPECT_EQ(-1,
            oak_bind_field(h,
                           &(oak_bind_field_t){
                               .name = "x",
                               .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .getter = stub_getter,
                               .setter = OAK_NULL }));

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
  for (i = 0; i < OAK_COUNT_OF(cases); ++i)
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
                         .param_count = 0,
                         .return_type = OAK_BIND_NATIVE(other) });

  r = oak_test_source_opts(OAK_A,
                           "let h = make_handle();\n"
                           "let t = make_token();\n"
                           "let same = h == t;\n",
                           &opts);
  EXPECT_FALSE(r.compiled);

  oak_compile_options_free(&opts);
}

/*
 * A native field declared weak takes part in the same acyclicity analysis as a
 * declared one. Before OAK_BIND_WEAK a binding had no way to say this, so the
 * escape hatch the language gives Oak code was unreachable from C -- while
 * CLAUDE.md required native bindings to uphold the invariant "using weak
 * values".
 */
UTEST_F(bind_type, a_native_field_can_be_declared_weak)
{
  oak_compile_options_t opts;
  oak_bind_type_t* node;
  const oak_bind_field_t* fields;

  oak_compile_options_init(&opts, OAK_A);
  node = oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "Node");
  ASSERT_TRUE(node != OAK_NULL);
  EXPECT_EQ(0,
            oak_bind_field(node,
                           &(oak_bind_field_t){
                               .name = "parent",
                               .type = OAK_BIND_WEAK(OAK_BIND_NATIVE(node)),
                               .getter = stub_getter,
                               .setter = OAK_NULL }));
  EXPECT_EQ(0,
            oak_bind_field(node,
                           &(oak_bind_field_t){
                               .name = "tag",
                               .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .getter = stub_getter,
                               .setter = OAK_NULL }));

  fields = OAK_CDATA(oak_bind_field_t, node->fields);
  EXPECT_EQ(1, fields[0].type.is_weak);
  /* Weakness is opt-in, not a property of native refs in general. */
  EXPECT_EQ(0, fields[1].type.is_weak);
  /* A self-referential weak field is not a cycle, so this compiles. */
  {
    const oak_run_result_t r = oak_test_source_opts(
        OAK_A, "fn f(n : Node) -> number { return n.tag; }\n", &opts);
    EXPECT_TRUE(r.compiled);
    if (!r.compiled)
      oak_test_explain(&r, "fn f(n : Node) -> number { return n.tag; }");
  }

  oak_compile_options_free(&opts);
}
