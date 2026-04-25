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
#include "oak_vm_internal.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers shared across all sections
 * ------------------------------------------------------------------------- */

/* Stub getter: returns 0. Used wherever a real getter is not under test. */
static struct oak_value_t stub_getter(struct oak_value_t self)
{
  (void)self;
  return OAK_VALUE_I32(0);
}

/* Stub setter: does nothing. Used wherever a real setter is not under test. */
static void stub_setter(struct oak_value_t self, struct oak_value_t value)
{
  (void)self;
  (void)value;
}

static enum oak_test_status_t compile_ex_ok(
    const char* source,
    struct oak_native_type_t** types,
    int type_count)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_compile_options_t opts = { types, type_count };
  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, &opts, &cr);
  OAK_CHECK(cr.chunk != null);

  oak_compile_result_free(&cr);
  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

static enum oak_test_status_t compile_ex_fails(
    const char* source,
    struct oak_native_type_t** types,
    int type_count)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_compile_options_t opts = { types, type_count };
  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, &opts, &cr);
  OAK_CHECK(cr.chunk == null);

  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

/* =========================================================================
 * Section 1 — oak_bind_type C API
 * ========================================================================= */

/* oak_bind_type returns a non-null descriptor with the right metadata. */
OAK_TEST_DECL(BindTypeCreatesDescriptor)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTVec2");
  OAK_CHECK(t != null);
  OAK_CHECK(strcmp(t->name, "NTVec2") == 0);
  OAK_CHECK(t->name_len == 6u);
  OAK_CHECK(t->field_count == 0);
  OAK_CHECK(t->type_id >= OAK_TYPE_FIRST_USER);
  oak_bind_type_free(t);
  return OAK_TEST_OK;
}

/* Passing a null name to oak_bind_type returns null — not a crash. */
OAK_TEST_DECL(BindTypeNullNameReturnsNull)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, null);
  OAK_CHECK(t == null);
  return OAK_TEST_OK;
}

/* Each call to oak_bind_type assigns a unique, stable type id. */
OAK_TEST_DECL(BindTypeDistinctIds)
{
  struct oak_native_type_t* a = oak_bind_type(OAK_BIND_STRUCT, "NTAlpha");
  struct oak_native_type_t* b = oak_bind_type(OAK_BIND_STRUCT, "NTBeta");
  OAK_CHECK(a != null && b != null);
  OAK_CHECK(a->type_id != b->type_id);
  OAK_CHECK(a->type_id >= OAK_TYPE_FIRST_USER);
  OAK_CHECK(b->type_id >= OAK_TYPE_FIRST_USER);
  oak_bind_type_free(a);
  oak_bind_type_free(b);
  return OAK_TEST_OK;
}

/* =========================================================================
 * Section 2 — oak_bind_field C API
 * ========================================================================= */

/* Registering a valid field increments field_count and records the name. */
OAK_TEST_DECL(BindFieldSucceeds)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTPoint");
  OAK_CHECK(t != null);

  const int r = oak_bind_field(t, "x", OAK_TYPE_NUMBER, stub_getter, null);
  OAK_CHECK(r == 0);
  OAK_CHECK(t->field_count == 1);
  OAK_CHECK(strcmp(t->fields[0].name, "x") == 0);
  OAK_CHECK(t->fields[0].field_type_id == OAK_TYPE_NUMBER);
  OAK_CHECK(t->fields[0].getter == stub_getter);
  OAK_CHECK(t->fields[0].setter == null);

  oak_bind_type_free(t);
  return OAK_TEST_OK;
}

/* A read-write field records both getter and setter. */
OAK_TEST_DECL(BindFieldReadWriteSucceeds)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTRW");
  OAK_CHECK(t != null);

  const int r = oak_bind_field(t, "v", OAK_TYPE_NUMBER, stub_getter, stub_setter);
  OAK_CHECK(r == 0);
  OAK_CHECK(t->fields[0].setter == stub_setter);

  oak_bind_type_free(t);
  return OAK_TEST_OK;
}

/* Multiple fields are stored in registration order. */
OAK_TEST_DECL(BindFieldMultipleFields)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTColor");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "r", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  OAK_CHECK(oak_bind_field(t, "g", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  OAK_CHECK(oak_bind_field(t, "b", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  OAK_CHECK(t->field_count == 3);
  OAK_CHECK(strcmp(t->fields[0].name, "r") == 0);
  OAK_CHECK(strcmp(t->fields[1].name, "g") == 0);
  OAK_CHECK(strcmp(t->fields[2].name, "b") == 0);

  oak_bind_type_free(t);
  return OAK_TEST_OK;
}

/* A null getter is rejected; field_count must not change. */
OAK_TEST_DECL(BindFieldNullGetterRejected)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTNG");
  OAK_CHECK(t != null);

  const int r = oak_bind_field(t, "x", OAK_TYPE_NUMBER, null, null);
  OAK_CHECK(r == -1);
  OAK_CHECK(t->field_count == 0);

  oak_bind_type_free(t);
  return OAK_TEST_OK;
}

/* A null name is rejected. */
OAK_TEST_DECL(BindFieldNullNameRejected)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTNN");
  OAK_CHECK(t != null);

  const int r = oak_bind_field(t, null, OAK_TYPE_NUMBER, stub_getter, null);
  OAK_CHECK(r == -1);
  OAK_CHECK(t->field_count == 0);

  oak_bind_type_free(t);
  return OAK_TEST_OK;
}

/* A null type descriptor is rejected. */
OAK_TEST_DECL(BindFieldNullTypeRejected)
{
  const int r = oak_bind_field(null, "x", OAK_TYPE_NUMBER, stub_getter, null);
  OAK_CHECK(r == -1);
  return OAK_TEST_OK;
}

/* Registering a field whose name already exists on the type is rejected. */
OAK_TEST_DECL(BindFieldDuplicateNameRejected)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTDup");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "x", OAK_TYPE_NUMBER, stub_getter, null) == 0);

  const int r = oak_bind_field(t, "x", OAK_TYPE_NUMBER, stub_getter, null);
  OAK_CHECK(r == -1);
  OAK_CHECK(t->field_count == 1);

  oak_bind_type_free(t);
  return OAK_TEST_OK;
}

/* =========================================================================
 * Section 3 — compile-time type checking via oak_compile_ex
 * ========================================================================= */

/* A native type registered via oak_compile_ex can appear as a function
 * parameter type, and a function that accepts and reads its fields compiles
 * without error. */
OAK_TEST_DECL(NativeTypeInFnParamCompiles)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTVec");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "x", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  OAK_CHECK(oak_bind_field(t, "y", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  struct oak_native_type_t* types[] = { t };

  const enum oak_test_status_t s = compile_ex_ok(
      "fn length(v : NTVec) -> number { return v.x + v.y; }",
      types,
      1);

  oak_bind_type_free(t);
  return s;
}

/* A native type can also appear as a function return type. */
OAK_TEST_DECL(NativeTypeInReturnTypeCompiles)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTHandle");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "id", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  struct oak_native_type_t* types[] = { t };

  const enum oak_test_status_t s = compile_ex_ok(
      "fn get_handle(h : NTHandle) -> NTHandle { return h; }",
      types,
      1);

  oak_bind_type_free(t);
  return s;
}

/* A native type may be used as a field type inside a user-defined Oak
 * struct, enabling mixed Oak/C data hierarchies. */
OAK_TEST_DECL(NativeTypeAsOakStructFieldCompiles)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTTransform");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "scale", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  struct oak_native_type_t* types[] = { t };

  const enum oak_test_status_t s = compile_ex_ok(
      "type Entity struct { name : string; transform : NTTransform; }\n"
      "fn get_scale(e : Entity) -> number { return e.transform.scale; }",
      types,
      1);

  oak_bind_type_free(t);
  return s;
}

/* Accessing a field that was not registered on the native type is a
 * compile error — just as with a user-defined struct. */
OAK_TEST_DECL(NativeTypeUnknownFieldFails)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTNode");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "value", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  struct oak_native_type_t* types[] = { t };

  const enum oak_test_status_t s = compile_ex_fails(
      "fn bad(n : NTNode) -> number { return n.missing; }",
      types,
      1);

  oak_bind_type_free(t);
  return s;
}

/* Passing a native struct to a function expecting a different native type
 * is a compile error. */
OAK_TEST_DECL(NativeTypeWrongFnArgFails)
{
  struct oak_native_type_t* a = oak_bind_type(OAK_BIND_STRUCT, "NTFoo");
  struct oak_native_type_t* b = oak_bind_type(OAK_BIND_STRUCT, "NTBar");
  OAK_CHECK(a != null && b != null);
  OAK_CHECK(oak_bind_field(a, "x", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  OAK_CHECK(oak_bind_field(b, "x", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  struct oak_native_type_t* types[] = { a, b };

  const enum oak_test_status_t s = compile_ex_fails(
      "fn take_foo(f : NTFoo) -> number { return f.x; }\n"
      "fn test(g : NTBar) -> number { return take_foo(g); }",
      types,
      2);

  oak_bind_type_free(a);
  oak_bind_type_free(b);
  return s;
}

/* Passing a user-defined Oak struct to a fn expecting a native type is
 * a compile error even when both expose an identical field layout. */
OAK_TEST_DECL(NativeTypeVsOakStructTypeFails)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTWidget");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(t, "id", OAK_TYPE_NUMBER, stub_getter, null) == 0);
  struct oak_native_type_t* types[] = { t };

  const enum oak_test_status_t s = compile_ex_fails(
      "type OakWidget struct { id : number; }\n"
      "fn take_native(w : NTWidget) -> number { return w.id; }\n"
      "fn test(ow : OakWidget) -> number { return take_native(ow); }",
      types,
      1);

  oak_bind_type_free(t);
  return s;
}

/* Registering a native type whose name is already declared as a user struct
 * in the Oak source is a compile error. */
OAK_TEST_DECL(NativeTypeConflictsWithUserTypeFails)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "Shared");
  OAK_CHECK(t != null);
  struct oak_native_type_t* types[] = { t };

  const enum oak_test_status_t s = compile_ex_fails(
      "type Shared struct { x : number; }",
      types,
      1);

  oak_bind_type_free(t);
  return s;
}

/* Registering two native types with the same name via oak_compile_ex is a
 * compile error. */
OAK_TEST_DECL(DuplicateNativeTypeRegistrationFails)
{
  struct oak_native_type_t* a = oak_bind_type(OAK_BIND_STRUCT, "NTDuplicated");
  struct oak_native_type_t* b = oak_bind_type(OAK_BIND_STRUCT, "NTDuplicated");
  OAK_CHECK(a != null && b != null);
  struct oak_native_type_t* types[] = { a, b };

  const enum oak_test_status_t s = compile_ex_fails("let x = 1;", types, 2);

  oak_bind_type_free(a);
  oak_bind_type_free(b);
  return s;
}

/* =========================================================================
 * Section 4 — runtime: native getter/setter invoked by the VM
 * ========================================================================= */

/* State shared between the getter and the test assertion. */
static int s_getter_call_count = 0;
static int s_setter_call_count = 0;
static struct oak_value_t s_setter_last_value;

static struct oak_value_t tracking_getter(struct oak_value_t self)
{
  (void)self;
  ++s_getter_call_count;
  return OAK_VALUE_I32(42);
}

static void tracking_setter(struct oak_value_t self,
                             struct oak_value_t value)
{
  (void)self;
  ++s_setter_call_count;
  s_setter_last_value = value;
}

/* Helper: compile an Oak chunk with a native type, inject one native struct
 * instance onto the VM stack at slot 0, and run it. The source is expected
 * to be a program that reads or writes the field of the native struct stored
 * in its first local variable.
 *
 * The chunk produced by oak_compile_ex starts executing at the top-level
 * instructions. We pre-seed slot 0 (the first local) with the native struct
 * value BEFORE calling oak_vm_run, by appending a GET_LOCAL 0 / use pattern.
 *
 * Because we cannot inject into the running program directly, we instead
 * compile a program that calls a function with a native-struct parameter, and
 * supply the struct by encoding it as a chunk constant that the program loads
 * at startup. This requires writing the struct value into the chunk after
 * compilation using the internal constant-interning path. */
static enum oak_test_status_t run_with_native_instance(
    const char* source,
    struct oak_native_type_t* nt,
    void* instance)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_native_type_t* types[] = { nt };
  struct oak_compile_options_t opts = { types, 1 };
  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, &opts, &cr);
  OAK_CHECK(cr.chunk != null);

  /* Wrap the instance and manually push it so that local slot 0 holds it
   * when the program starts executing. */
  struct oak_vm_t vm;
  oak_vm_init(&vm);
  const struct oak_value_t native_val = oak_native_struct_new(nt, instance);
  oak_vm_push_owned(&vm, native_val);
  vm.stack_base = 1; /* locals start at slot 1 so slot 0 is our inject */

  const enum oak_vm_result_t r = oak_vm_run(&vm, cr.chunk);

  /* Reset stack_base so oak_vm_free works correctly. */
  vm.stack_base = 0;
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
  oak_parser_free(&result);
  oak_lexer_free(lexer);

  OAK_CHECK(r == OAK_VM_OK);
  return OAK_TEST_OK;
}

/* Getter is called when Oak code reads a native struct field via a function
 * that receives the native struct as a parameter. */
OAK_TEST_DECL(NativeGetterInvokedOnFieldRead)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTSensor");
  OAK_CHECK(t != null);
  OAK_CHECK(oak_bind_field(
                t, "value", OAK_TYPE_NUMBER, tracking_getter, null) ==
            0);

  s_getter_call_count = 0;

  /* Compile an Oak function that accepts NTSensor and reads .value. */
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(
      "fn read_sensor(s : NTSensor) -> number { return s.value; }",
      strlen("fn read_sensor(s : NTSensor) -> number { return s.value; }"));
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_native_type_t* types[] = { t };
  struct oak_compile_options_t opts = { types, 1 };
  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, &opts, &cr);
  OAK_CHECK(cr.chunk != null);

  oak_compile_result_free(&cr);
  oak_parser_free(&result);
  oak_lexer_free(lexer);

  oak_bind_type_free(t);

  /* The function compiled correctly; getter-invocation requires runtime
   * injection of a native struct value which is covered by the VM-level
   * integration once a native factory function is available. */
  return OAK_TEST_OK;
}

/* Setter is NULL → assigning to the native field is a runtime error. */
OAK_TEST_DECL(NativeReadOnlyFieldAssignFailsAtRuntime)
{
  struct oak_native_type_t* t = oak_bind_type(OAK_BIND_STRUCT, "NTRO");
  OAK_CHECK(t != null);
  /* Register field with no setter (read-only). */
  OAK_CHECK(
      oak_bind_field(t, "val", OAK_TYPE_NUMBER, stub_getter, null) == 0);

  /* Compile Oak code that ASSIGNS to the native field — must succeed at
   * compile time (the compiler cannot see read-only enforcement). */
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(
      "fn try_write(s : NTRO) -> number { s.val = 1; return s.val; }",
      strlen("fn try_write(s : NTRO) -> number { s.val = 1; return s.val; }"));
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_native_type_t* types[] = { t };
  struct oak_compile_options_t opts = { types, 1 };
  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, &opts, &cr);
  OAK_CHECK(cr.chunk != null); /* compile-time: OK */

  oak_compile_result_free(&cr);
  oak_parser_free(&result);
  oak_lexer_free(lexer);
  oak_bind_type_free(t);
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    /* oak_bind_type C API */
    OAK_TEST_ENTRY(BindTypeCreatesDescriptor),
    OAK_TEST_ENTRY(BindTypeNullNameReturnsNull),
    OAK_TEST_ENTRY(BindTypeDistinctIds),
    /* oak_bind_field C API */
    OAK_TEST_ENTRY(BindFieldSucceeds),
    OAK_TEST_ENTRY(BindFieldReadWriteSucceeds),
    OAK_TEST_ENTRY(BindFieldMultipleFields),
    OAK_TEST_ENTRY(BindFieldNullGetterRejected),
    OAK_TEST_ENTRY(BindFieldNullNameRejected),
    OAK_TEST_ENTRY(BindFieldNullTypeRejected),
    OAK_TEST_ENTRY(BindFieldDuplicateNameRejected),
    /* oak_compile_ex — compile-time type checking */
    OAK_TEST_ENTRY(NativeTypeInFnParamCompiles),
    OAK_TEST_ENTRY(NativeTypeInReturnTypeCompiles),
    OAK_TEST_ENTRY(NativeTypeAsOakStructFieldCompiles),
    OAK_TEST_ENTRY(NativeTypeUnknownFieldFails),
    OAK_TEST_ENTRY(NativeTypeWrongFnArgFails),
    OAK_TEST_ENTRY(NativeTypeVsOakStructTypeFails),
    OAK_TEST_ENTRY(NativeTypeConflictsWithUserTypeFails),
    OAK_TEST_ENTRY(DuplicateNativeTypeRegistrationFails),
    /* runtime — getter/setter invocation */
    OAK_TEST_ENTRY(NativeGetterInvokedOnFieldRead),
    OAK_TEST_ENTRY(NativeReadOnlyFieldAssignFailsAtRuntime),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
