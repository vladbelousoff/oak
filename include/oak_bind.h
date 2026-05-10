#pragma once

#include "oak_compiler.h"
#include "oak_dynarr.h"
#include "oak_parser.h"
#include "oak_type_id.h"
#include "oak_types.h"
#include "oak_value.h"

struct oak_module_registry_t;
struct oak_module_t;

/* Maximum number of fields a native type may expose to Oak. */
#define OAK_BIND_MAX_FIELDS 32

/* Maximum number of variants a native enum may expose to Oak. */
#define OAK_BIND_MAX_ENUM_VARIANTS 64

/* ---------- Kind discriminants ---------- */

enum oak_bind_type_kind_t
{
  OAK_BIND_TYPE_RECORD,
};

/* Where a native function is bound in Oak (see oak_bind_fn). */
enum oak_bind_fn_kind_t
{
  OAK_BIND_FN_GLOBAL,
  OAK_BIND_FN_INSTANCE_METHOD,
  OAK_BIND_FN_STATIC_METHOD,
};

/* Shape of a binding's value: scalar, or a typed array whose *element* type
 * is the associated type id (used for both field types and function returns).
 * OAK_BIND_SHAPE_ARRAY means T[] where T is the type id. */
enum oak_bind_shape_t
{
  OAK_BIND_SHAPE_SCALAR = 0,
  OAK_BIND_SHAPE_ARRAY,
};

/* ---------- Getter / setter / destructor callbacks ---------- */

/* Returns the field value for the native record instance `self`.
 * The returned value is owned by the caller (the VM): for object values the
 * getter must return a fresh reference (refcount already incremented); for
 * scalar values (number, bool) no refcounting is required.
 * Access the underlying C pointer with oak_native_instance(self). */
typedef struct oak_value_t (*oak_bind_field_getter_t)(struct oak_value_t self);

/* Writes `value` into the native record instance `self`.
 * A NULL setter makes the field read-only; the VM will emit a runtime error
 * if Oak code attempts to assign to such a field. */
typedef void (*oak_bind_field_setter_t)(struct oak_value_t self,
                                        struct oak_value_t value);

/* Optional: frees heap data owned by `instance` when the native record's
 * refcount reaches zero. If NULL, only the wrapper object is freed (legacy). */
typedef void (*oak_bind_destructor_t)(void* instance);

/* ---------- Native field descriptor ---------- */

struct oak_bind_field_t
{
  const char* name;
  usize name_len;
  /* Compile-time type of this field.  Use OAK_TYPE_NUMBER / OAK_TYPE_STRING /
   * OAK_TYPE_BOOL for primitives, or another oak_bind_type_t::type_id for
   * scalar native records, or the *element* type id when `shape` is ARRAY. */
  oak_type_id_t field_type_id;
  enum oak_bind_shape_t shape; /* 0 = SCALAR */
  oak_bind_field_getter_t getter;
  oak_bind_field_setter_t setter; /* NULL = read-only */
};

/* ---------- Native type descriptor ---------- */

struct oak_bind_type_t
{
  enum oak_bind_type_kind_t kind;
  /* Optional native module name. When set, the type is exported from that
   * synthetic module and imported as `module.Type`; when NULL, it is
   * registered in the global namespace as before. */
  const char* module_name;
  usize module_name_len;
  const char* name;
  usize name_len;
  /* Stable id assigned by oak_bind_type() from opts->next_type_id.
   * Valid for the lifetime of the oak_compile_options_t it was registered in.
   * Use this value as field_type_id or receiver_type_id when referencing
   * this type from another binding. */
  oak_type_id_t type_id;
  struct oak_bind_field_t fields[OAK_BIND_MAX_FIELDS];
  int field_count;
  oak_bind_destructor_t destructor;
};

/* ---------- Native function binding descriptor ---------- */

struct oak_bind_fn_t
{
  enum oak_bind_fn_kind_t kind;
  /* Optional native module name for global functions. A global function with
   * module_name = "math" is called as `math.sqrt(...)` after `import math;`.
   * Instance/static methods still bind to their receiver type. */
  const char* module_name;
  usize module_name_len;
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
  /* If OAK_BIND_SHAPE_ARRAY, the return is return_type_id[]; otherwise void
   * and scalar returns use OAK_BIND_SHAPE_SCALAR. */
  enum oak_bind_shape_t return_shape;
};

/* ---------- Native enum descriptor ---------- */

/* A single variant of a native-bound enum: a name plus an integer value.
 * Variants are exposed to Oak source as `EnumName.Variant`, lowering to the
 * variant's integer value (the same shape as user-declared enums). */
struct oak_bind_enum_variant_t
{
  const char* name;
  usize name_len;
  int value;
};

struct oak_bind_enum_t
{
  /* Optional native module name. When set, variants are exported from that
   * synthetic module and referenced as `module.Enum.Variant`. */
  const char* module_name;
  usize module_name_len;
  const char* name;
  usize name_len;
  struct oak_bind_enum_variant_t variants[OAK_BIND_MAX_ENUM_VARIANTS];
  int variant_count;
};

/* ---------- Concrete dynamic-array types for compile options ---------- */

struct oak_bind_type_ptr_vec_t
{
  struct oak_bind_type_t** items;
  int count;
  int capacity;
};
struct oak_bind_fn_vec_t
{
  struct oak_bind_fn_t* items;
  int count;
  int capacity;
};
struct oak_bind_enum_ptr_vec_t
{
  struct oak_bind_enum_t** items;
  int count;
  int capacity;
};

/* ---------- Compilation options ---------- */

struct oak_compile_options_t
{
  /* Optional: path or label for the Oak source (borrowed). Set on the chunk. */
  const char* source_name;

  /* Native record types (owned; populated by oak_bind_type). */
  struct oak_bind_type_ptr_vec_t native_types;

  /* Native function / method bindings (owned; populated by oak_bind_fn). */
  struct oak_bind_fn_vec_t native_fns;

  /* Native enums (owned; populated by oak_bind_enum / oak_bind_enum_variant).
   */
  struct oak_bind_enum_ptr_vec_t native_enums;

  /* Next type id to assign; initialised to OAK_TYPE_FIRST_USER by
   * oak_compile_options_init and incremented by each oak_bind_type call. */
  oak_type_id_t next_type_id; /* private */

  /* When non-zero (default), the compiler attaches a debug section to the
   * chunk: per-byte source line/column and local-variable names. Set to 0 to
   * skip these allocations and produce a minimal runtime-only chunk. */
  int emit_debug_info;

  /* Module-system context (both null when compiling standalone — preserves
   * the original single-file behaviour).  When set, the compiler uses
   * `current_module` to attach exports and to resolve `import alias.name`
   * references via `module_registry`. */
  struct oak_module_registry_t* module_registry;
  struct oak_module_t* current_module;
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
struct oak_bind_type_t* oak_bind_type(struct oak_compile_options_t* opts,
                                      enum oak_bind_type_kind_t kind,
                                      const char* name);

struct oak_bind_type_t* oak_bind_type_in_module(
    struct oak_compile_options_t* opts,
    const char* module_name,
    enum oak_bind_type_kind_t kind,
    const char* name);

/* Register a field on a native type.  Fields are assigned indices in
 * registration order, matching the order the compiler resolves them.
 * `params` must not be NULL; it supplies name, field_type_id, getter, and
 * optional setter (same shape as struct oak_bind_field_t).  `name_len` in
 * params is ignored; it is set from `strlen(name)`.
 * Returns 0 on success, -1 if the field limit (OAK_BIND_MAX_FIELDS) is
 * reached or if a field with the same name already exists. */
int oak_bind_field(struct oak_bind_type_t* type,
                   const struct oak_bind_field_t* params);

/* Register a native function, instance method, or static method.
 * `params` must not be NULL; it supplies kind, receiver_type_id, name, impl,
 * arity, return_type_id, and return_shape (see struct oak_bind_fn_t).
 *   OAK_BIND_FN_GLOBAL: receiver_type_id must be OAK_TYPE_VOID; `arity` is the
 *     full VM argument count.
 *   OAK_BIND_FN_INSTANCE_METHOD: receiver_type_id is the record's type_id;
 *     `arity` is the user-visible count excluding `self` (VM adds one).
 *   OAK_BIND_FN_STATIC_METHOD: same receiver_type_id as the record; `arity` is
 *     the full argument count (no `self`); called as TypeName.name(...).
 *   return_shape: OAK_BIND_SHAPE_ARRAY means the function returns
 *     return_type_id[] (return_type_id is the element type).
 * Returns 0 on success, -1 on invalid arguments. */
int oak_bind_fn(struct oak_compile_options_t* opts,
                const struct oak_bind_fn_t* params);

/* Allocate a native enum descriptor and register it in opts.  Returns a
 * pointer for subsequent oak_bind_enum_variant calls; the descriptor is owned
 * by opts and freed by oak_compile_options_free.  Returns NULL on invalid
 * arguments. */
struct oak_bind_enum_t* oak_bind_enum(struct oak_compile_options_t* opts,
                                      const char* name);

struct oak_bind_enum_t* oak_bind_enum_in_module(
    struct oak_compile_options_t* opts,
    const char* module_name,
    const char* name);

/* Append a variant to a native enum.  Variant values must be unique within
 * an enum is not enforced — they are forwarded as-is to Oak as integer
 * constants.  Returns 0 on success, -1 if the variant cap is reached or a
 * variant with the same name already exists in this enum. */
int oak_bind_enum_variant(struct oak_bind_enum_t* e,
                          const char* name,
                          int value);

/* ---------- Runtime helpers ---------- */

/* Wrap a C instance pointer in an Oak value typed as the given native type.
 * The resulting Oak value participates in normal refcounting. When its
 * refcount reaches zero, `type->destructor` runs on non-NULL `instance`
 * (if registered), then the wrapper is freed. If `destructor` is NULL,
 * `instance` is not freed — lifetime is the embedder's responsibility.
 * `instance` may be NULL for sentinel / placeholder values. */
struct oak_value_t oak_native_record_new(const struct oak_bind_type_t* type,
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
