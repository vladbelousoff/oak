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
  OAK_BIND_STRUCT,
};

/* ---------- Getter / setter callback types ---------- */

/* Returns the field value for the native struct instance `self`.
 * The returned value is owned by the caller (the VM): for object values the
 * getter must return a fresh reference (refcount already incremented); for
 * scalar values (number, bool) no refcounting is required.
 * Access the underlying C pointer with oak_native_instance(self). */
typedef struct oak_value_t (*oak_field_getter_t)(struct oak_value_t self);

/* Writes `value` into the native struct instance `self`.
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
   * fields whose type is itself a native struct. */
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
  /* OAK_TYPE_VOID (0) = global function; any other id = method on that type. */
  oak_type_id_t receiver_type_id;
  const char* name;
  oak_native_fn_t impl;
  /* User-visible arity.  For methods, does NOT include the implicit self
   * receiver; the compiler adds 1 automatically. */
  int arity;
  /* Return type: OAK_TYPE_VOID, OAK_TYPE_NUMBER, OAK_TYPE_STRING,
   * OAK_TYPE_BOOL, or a native type's type_id. */
  oak_type_id_t return_type_id;
};

/* ---------- Compilation options ---------- */

struct oak_compile_options_t
{
  /* Native struct types (owned; populated by oak_bind_type). */
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

/* Register a native function or method.
 *   receiver_type_id == OAK_TYPE_VOID  → global function callable by name.
 *   receiver_type_id == some_type->type_id → method on that native struct.
 * `arity` is the user-visible argument count (excludes implicit self for
 * methods).  Returns 0 on success, -1 on invalid arguments. */
int oak_bind_fn(struct oak_compile_options_t* opts,
                oak_type_id_t receiver_type_id,
                const char* name,
                oak_native_fn_t impl,
                int arity,
                oak_type_id_t return_type_id);

/* ---------- Runtime helpers ---------- */

/* Wrap a C instance pointer in an Oak value typed as the given native type.
 * The resulting Oak value participates in normal refcounting; when its
 * refcount reaches zero the wrapper is freed but `instance` is never freed
 * by Oak — lifetime is the caller's responsibility.
 * `instance` may be NULL for sentinel / placeholder values. */
struct oak_value_t oak_native_struct_new(const struct oak_native_type_t* type,
                                         void* instance);

/* Extract the raw C instance pointer from a native struct Oak value.
 * Asserts that `value` is actually a native struct (OAK_OBJ_NATIVE_STRUCT).
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
