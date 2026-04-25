#pragma once

#include "oak_compiler.h"
#include "oak_parser.h"
#include "oak_type_id.h"
#include "oak_types.h"
#include "oak_value.h"

/* Maximum number of fields a native type may expose to Oak. */
#define OAK_MAX_NATIVE_FIELDS 32

/* ---------- Kind discriminant ---------- */

enum oak_bind_type_kind_t
{
  OAK_BIND_RECORD,
};

/* Where a native function is bound in Oak (see oak_bind_fn). */
enum oak_bind_fn_kind_t
{
  OAK_BIND_FN_GLOBAL,
  OAK_BIND_FN_INSTANCE_METHOD,
  OAK_BIND_FN_STATIC_METHOD,
};

/* Return type shape for native bindings.  OAK_BIND_RETURN_ARRAY means the
 * function returns a typed array T[] where the element type T is
 * return_type_id (e.g. native record id).  Scalar/void return uses
 * OAK_BIND_RETURN_SCALAR. */
enum oak_bind_return_shape_t
{
  OAK_BIND_RETURN_SCALAR = 0,
  OAK_BIND_RETURN_ARRAY,
};

/* ---------- Getter / setter callback types ---------- */

/* Returns the field value for the native record instance `self`.
 * The returned value is owned by the caller (the VM): for object values the
 * getter must return a fresh reference (refcount already incremented); for
 * scalar values (number, bool) no refcounting is required.
 * Access the underlying C pointer with oak_native_instance(self). */
typedef struct oak_value_t (*oak_field_getter_t)(struct oak_value_t self);

/* Writes `value` into the native record instance `self`.
 * A NULL setter makes the field read-only; the VM will emit a runtime error
 * if Oak code attempts to assign to such a field. */
typedef void (*oak_field_setter_t)(struct oak_value_t self,
                                   struct oak_value_t value);

/* ---------- Native field descriptor ---------- */

struct oak_native_field_t
{
  const char* name;
  usize name_len;
  /* Compile-time type of this field.  Use OAK_TYPE_NUMBER / OAK_TYPE_STRING /
   * OAK_TYPE_BOOL for primitives, or another oak_native_type_t::type_id for
   * fields whose type is itself a native record. */
  oak_type_id_t field_type_id;
  oak_field_getter_t getter;
  oak_field_setter_t setter; /* NULL = read-only */
};

/* ---------- Native type descriptor ---------- */

struct oak_native_type_t
{
  enum oak_bind_type_kind_t kind;
  const char* name;
  usize name_len;
  /* Stable id assigned by oak_bind_type() from opts->next_type_id.
   * Valid for the lifetime of the oak_compile_options_t it was registered in.
   * Use this value as field_type_id or receiver_type_id when referencing
   * this type from another binding. */
  oak_type_id_t type_id;
  struct oak_native_field_t fields[OAK_MAX_NATIVE_FIELDS];
  int field_count;
};

/* ---------- Native function binding descriptor ---------- */

struct oak_native_fn_binding_t
{
  enum oak_bind_fn_kind_t kind;
  /* OAK_TYPE_VOID = global (only with OAK_BIND_FN_GLOBAL).  Otherwise the
   * native record type_id for instance or static methods on that type. */
  oak_type_id_t receiver_type_id;
  const char* name;
  oak_native_fn_t impl;
  /* User-visible arity: for GLOBAL and STATIC_METHOD, full argument count;
   * for INSTANCE_METHOD, excludes implicit self (compiler adds +1 for VM). */
  int arity;
  /* Return type: OAK_TYPE_VOID, OAK_TYPE_NUMBER, OAK_TYPE_STRING,
   * OAK_TYPE_BOOL, or a native type's type_id. */
  oak_type_id_t return_type_id;
  /* If OAK_BIND_RETURN_ARRAY, the return is return_type_id[]; otherwise void
   * and scalar returns use OAK_BIND_RETURN_SCALAR. */
  enum oak_bind_return_shape_t return_shape;
};

/* Input for oak_bind_fn(); same fields as the stored binding descriptor. */
typedef struct oak_native_fn_binding_t oak_bind_fn_params_t;

/* ---------- Compilation options ---------- */

struct oak_compile_options_t
{
  /* Native record types (owned; populated by oak_bind_type). */
  struct oak_native_type_t** native_types;
  int native_type_count;
  int native_type_capacity; /* private */

  /* Native function / method bindings (owned; populated by oak_bind_fn). */
  struct oak_native_fn_binding_t* native_fns;
  int native_fn_count;
  int native_fn_capacity; /* private */

  /* Next type id to assign; initialised to OAK_TYPE_FIRST_USER by
   * oak_compile_options_init and incremented by each oak_bind_type call. */
  oak_type_id_t next_type_id; /* private */
};

/* ---------- Compile-options lifecycle ---------- */

void oak_compile_options_init(struct oak_compile_options_t* opts);
void oak_compile_options_free(struct oak_compile_options_t* opts);

/* ---------- Binding API ---------- */

/* Allocate a native type descriptor, assign it a stable type_id from
 * opts->next_type_id, register it in opts, and return a pointer for
 * subsequent oak_bind_field calls.  The descriptor is owned by opts and
 * freed by oak_compile_options_free; do not free it separately.
 * Returns NULL if opts or name is NULL, or if the type-id space is
 * exhausted (max OAK_MAX_TYPES total types). */
struct oak_native_type_t* oak_bind_type(struct oak_compile_options_t* opts,
                                        enum oak_bind_type_kind_t kind,
                                        const char* name);

/* Register a field on a native type.  Fields are assigned indices in
 * registration order, matching the order the compiler resolves them.
 * Returns 0 on success, -1 if the field limit (OAK_MAX_NATIVE_FIELDS) is
 * reached or if a field with the same name already exists. */
int oak_bind_field(struct oak_native_type_t* type,
                   const char* name,
                   oak_type_id_t field_type_id,
                   oak_field_getter_t getter,
                   oak_field_setter_t setter);

/* Register a native function, instance method, or static method.
 * `params` must not be NULL; it supplies kind, receiver_type_id, name, impl,
 * arity, return_type_id, and return_shape (see oak_bind_fn_params_t).
 *   OAK_BIND_FN_GLOBAL: receiver_type_id must be OAK_TYPE_VOID; `arity` is the
 *     full VM argument count.
 *   OAK_BIND_FN_INSTANCE_METHOD: receiver_type_id is the record's type_id;
 *     `arity` is the user-visible count excluding `self` (VM adds one).
 *   OAK_BIND_FN_STATIC_METHOD: same receiver_type_id as the record; `arity` is
 *     the full argument count (no `self`); called as TypeName.name(...).
 *   return_shape: OAK_BIND_RETURN_ARRAY means the function returns
 *     return_type_id[] (return_type_id is the element type).
 * Returns 0 on success, -1 on invalid arguments. */
int oak_bind_fn(struct oak_compile_options_t* opts,
                const oak_bind_fn_params_t* params);

/* ---------- Runtime helpers ---------- */

/* Wrap a C instance pointer in an Oak value typed as the given native type.
 * The resulting Oak value participates in normal refcounting; when its
 * refcount reaches zero the wrapper is freed but `instance` is never freed
 * by Oak — lifetime is the caller's responsibility.
 * `instance` may be NULL for sentinel / placeholder values. */
struct oak_value_t oak_native_record_new(const struct oak_native_type_t* type,
                                         void* instance);

/* Extract the raw C instance pointer from a native record Oak value.
 * Asserts that `value` is actually a native record (OAK_OBJ_NATIVE_RECORD).
 * Intended for use inside getter / setter callbacks:
 *   MyType* p = oak_native_instance(self); */
void* oak_native_instance(struct oak_value_t value);

/* ---------- Extended compilation ---------- */

/* Like oak_compile() but registers native types and functions from `opts`
 * into the compiler before the first pass so that Oak source code can refer
 * to them by name.
 * `opts` may be NULL, in which case this is identical to oak_compile(). */
void oak_compile_ex(const struct oak_ast_node_t* root,
                    const struct oak_compile_options_t* opts,
                    struct oak_compile_result_t* out);
