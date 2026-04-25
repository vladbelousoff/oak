#pragma once

#include "oak_compiler.h"
#include "oak_parser.h"
#include "oak_type_id.h"
#include "oak_types.h"
#include "oak_value.h"

/* Maximum number of fields a native type may expose to Oak. */
#define OAK_MAX_NATIVE_FIELDS 32

/* ---------- Kind discriminant (reserved for future binding targets) ----------
 */

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
  /* Stable id pre-assigned by oak_bind_type(); valid for the lifetime of the
   * process.  Use this value as the field_type_id when referencing this type
   * from another native type's field binding. */
  oak_type_id_t type_id;
  struct oak_native_field_t fields[OAK_MAX_NATIVE_FIELDS];
  int field_count;
};

/* ---------- Compilation options ---------- */

struct oak_compile_options_t
{
  struct oak_native_type_t** native_types;
  int native_type_count;
};

/* ---------- Binding API ---------- */

/* Allocate and initialise a new native type descriptor with a stable,
 * pre-assigned type_id.  Returns NULL if the global type-id counter is
 * exhausted (max OAK_MAX_TYPES total types across all compilations). */
struct oak_native_type_t* oak_bind_type(enum oak_bind_type_kind_t kind,
                                        const char* name);

/* Release a native type descriptor previously returned by oak_bind_type().
 * Does not affect any Oak values that were created with oak_native_struct_new()
 * using this descriptor — the caller must ensure all such values are gone
 * before freeing the descriptor. */
void oak_bind_type_free(struct oak_native_type_t* type);

/* Register a field on a native type.  Fields are assigned indices in
 * registration order, matching the order the compiler resolves them.
 * Returns 0 on success, -1 if the field limit (OAK_MAX_NATIVE_FIELDS) is
 * reached or if a field with the same name already exists. */
int oak_bind_field(struct oak_native_type_t* type,
                   const char* name,
                   oak_type_id_t field_type_id,
                   oak_field_getter_t getter,
                   oak_field_setter_t setter);

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

/* Like oak_compile() but registers native types from `opts` into the compiler
 * before the first pass so that Oak source code can refer to them by name.
 * `opts` may be NULL, in which case this is identical to oak_compile(). */
void oak_compile_ex(const struct oak_ast_node_t* root,
                    const struct oak_compile_options_t* opts,
                    struct oak_compile_result_t* out);
