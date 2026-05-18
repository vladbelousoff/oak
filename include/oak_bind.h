#pragma once

#include "oak_compiler.h"
#include "oak_dynarr.h"
#include "oak_export.h"
#include "oak_parser.h"
#include "oak_type_id.h"
#include "oak_types.h"
#include "oak_value.h"

struct oak_module_registry_t;
struct oak_module_t;

/* ---------- Kind discriminants ---------- */

enum oak_bind_type_kind_t
{
  OAK_BIND_TYPE_RECORD,
};

/* Where a native method is bound on its receiver type (see oak_bind_fn). */
enum oak_bind_fn_kind_t
{
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
  struct oak_bind_field_t* fields;
  int field_count;
  int field_capacity;
  oak_bind_destructor_t destructor;
  struct oak_allocator_t* allocator;
};

/* ---------- Native global function descriptor ---------- */

/* Use oak_bind_fn_global() to register a free function or module-scoped
 * function (e.g. math.sqrt).  Global functions are not attached to any type. */
struct oak_bind_global_fn_t
{
  /* NULL for a top-level global; "math" to scope it as `math.fn()`. */
  const char* module_name;
  usize module_name_len;
  const char* name;
  oak_native_fn_t impl;
  int arity;
  /* Return type: OAK_TYPE_VOID, OAK_TYPE_NUMBER, OAK_TYPE_STRING,
   * OAK_TYPE_BOOL, or a native type's type_id. */
  oak_type_id_t return_type_id;
  enum oak_bind_shape_t return_shape;
};

/* ---------- Native method binding descriptor ---------- */

/* Use oak_bind_fn() to register instance or static methods on a native type.
 * For global or module-scoped functions use oak_bind_fn_global() instead. */
struct oak_bind_fn_t
{
  enum oak_bind_fn_kind_t kind; /* INSTANCE_METHOD or STATIC_METHOD */
  /* The native record type_id for the receiver type. */
  oak_type_id_t receiver_type_id;
  const char* name;
  oak_native_fn_t impl;
  /* User-visible arity: for STATIC_METHOD, full argument count;
   * for INSTANCE_METHOD, excludes implicit self (compiler adds +1 for VM). */
  int arity;
  /* Return type: OAK_TYPE_VOID, OAK_TYPE_NUMBER, OAK_TYPE_STRING,
   * OAK_TYPE_BOOL, or a native type's type_id. */
  oak_type_id_t return_type_id;
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
  struct oak_bind_enum_variant_t* variants;
  int variant_count;
  int variant_capacity;
  struct oak_allocator_t* allocator;
};

/* ---------- Attribute callbacks ---------- */

/* Target kind of a declaration bearing an attribute. */
enum oak_attr_target_t
{
  OAK_ATTR_TARGET_FN,
  OAK_ATTR_TARGET_METHOD,
  OAK_ATTR_TARGET_RECORD,
  OAK_ATTR_TARGET_ENUM,
};

/* Context passed to compile-time attribute callbacks. */
struct oak_attr_compile_ctx_t
{
  enum oak_attr_target_t target;
  const char* decl_name; /* name of the declaration bearing this attribute */
  void* user_data;
};

typedef void (*oak_attr_compile_cb_t)(const struct oak_attr_compile_ctx_t* ctx);

/* oak_attr_runtime_cb_t is defined in oak_value.h (included above). */

struct oak_bind_attr_t
{
  const char* name;               /* e.g. "Deprecated" */
  oak_attr_compile_cb_t on_decl;  /* NULL = no compile-time action */
  oak_attr_runtime_cb_t on_call;  /* NULL = no pre-call action */
  void* user_data;                /* forwarded to both callbacks */
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
struct oak_bind_global_fn_vec_t
{
  struct oak_bind_global_fn_t* items;
  int count;
  int capacity;
};
struct oak_bind_enum_ptr_vec_t
{
  struct oak_bind_enum_t** items;
  int count;
  int capacity;
};
struct oak_bind_attr_vec_t
{
  struct oak_bind_attr_t* items;
  int count;
  int capacity;
};

/* ---------- Compilation options ---------- */

struct oak_compile_options_t
{
  /* Allocator used for all compilation and runtime allocations. */
  struct oak_allocator_t* allocator;

  /* Optional: path or label for the Oak source (borrowed). Set on the chunk. */
  const char* source_name;

  /* Native record types (owned; populated by oak_bind_type). */
  struct oak_bind_type_ptr_vec_t native_types;

  /* Native method bindings (owned; populated by oak_bind_fn). */
  struct oak_bind_fn_vec_t native_fns;

  /* Native global and module-scoped functions (owned; populated by oak_bind_fn_global). */
  struct oak_bind_global_fn_vec_t native_global_fns;

  /* Native enums (owned; populated by oak_bind_enum / oak_bind_enum_variant).
   */
  struct oak_bind_enum_ptr_vec_t native_enums;

  /* Named attribute bindings (owned; populated by oak_bind_attr). */
  struct oak_bind_attr_vec_t native_attrs;

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

  /* Internal module-loader option: native stdlib declaration modules contain
   * function signatures without Oak bodies because their implementations are
   * provided by native bindings. */
  int allow_bodyless_fns;
};

/* ---------- Compile-options lifecycle ---------- */

OAK_API void oak_compile_options_init(struct oak_compile_options_t* opts,
                                     struct oak_allocator_t* allocator);
OAK_API void oak_compile_options_free(struct oak_compile_options_t* opts);

/* ---------- Binding API ---------- */

/* Allocate a native type descriptor, assign it a stable type_id from
 * opts->next_type_id, register it in opts, and return a pointer for
 * subsequent oak_bind_field calls.  The descriptor is owned by opts and
 * freed by oak_compile_options_free; do not free it separately.
 * Returns NULL if opts or name is NULL. */
OAK_API struct oak_bind_type_t* oak_bind_type(struct oak_compile_options_t* opts,
                                              enum oak_bind_type_kind_t kind,
                                              const char* name);

OAK_API struct oak_bind_type_t* oak_bind_type_in_module(
    struct oak_compile_options_t* opts,
    const char* module_name,
    enum oak_bind_type_kind_t kind,
    const char* name);

/* Register a field on a native type.  Fields are assigned indices in
 * registration order, matching the order the compiler resolves them.
 * `params` must not be NULL; it supplies name, field_type_id, getter, and
 * optional setter (same shape as struct oak_bind_field_t).  `name_len` in
 * params is ignored; it is set from `strlen(name)`.
 * Returns 0 on success, -1 if a field with the same name already exists. */
OAK_API int oak_bind_field(struct oak_bind_type_t* type,
                           const struct oak_bind_field_t* params);

/* Register a global or module-scoped native function.
 * Use this for free functions like `toInt(v)` or `math.sqrt(v)`.
 * Returns 0 on success, -1 on invalid arguments. */
OAK_API int oak_bind_fn_global(struct oak_compile_options_t* opts,
                               const struct oak_bind_global_fn_t* params);

/* Register a native instance or static method on a native type.
 * `params->kind` must be OAK_BIND_FN_INSTANCE_METHOD or OAK_BIND_FN_STATIC_METHOD.
 * `params->receiver_type_id` must be a type_id from a prior oak_bind_type() call.
 *   INSTANCE_METHOD: `arity` excludes implicit self (compiler adds +1 for VM).
 *   STATIC_METHOD: `arity` is the full argument count; called as TypeName.name(...).
 *   return_shape: OAK_BIND_SHAPE_ARRAY means return_type_id[].
 * Returns 0 on success, -1 on invalid arguments. */
OAK_API int oak_bind_fn(struct oak_compile_options_t* opts,
                        const struct oak_bind_fn_t* params);

/* Allocate a native enum descriptor and register it in opts.  Returns a
 * pointer for subsequent oak_bind_enum_variant calls; the descriptor is owned
 * by opts and freed by oak_compile_options_free.  Returns NULL on invalid
 * arguments. */
OAK_API struct oak_bind_enum_t* oak_bind_enum(struct oak_compile_options_t* opts,
                                              const char* name);

OAK_API struct oak_bind_enum_t* oak_bind_enum_in_module(
    struct oak_compile_options_t* opts,
    const char* module_name,
    const char* name);

/* Append a variant to a native enum.  Variant values must be unique within
 * an enum is not enforced — they are forwarded as-is to Oak as integer
 * constants.  Returns 0 on success, -1 if a variant with the same name
 * already exists in this enum. */
OAK_API int oak_bind_enum_variant(struct oak_bind_enum_t* e,
                                  const char* name,
                                  int value);

/* Match attrs[] against opts->native_attrs and fire on_decl for each
 * matching binding that has one.  target and decl_name identify the
 * declaration.  Safe to call with empty attrs or no bindings. */
OAK_API void
oak_dispatch_compile_attr_cbs(const struct oak_compile_options_t* opts,
                              const char** attrs,
                              int attr_count,
                              const char* decl_name,
                              enum oak_attr_target_t target);

/* Match attrs[] against opts->native_attrs and attach all bindings whose
 * on_call is non-NULL as a heap-allocated hooks array on fn_obj or native_obj
 * (exactly one must be non-NULL).  Safe to call with no matches. */
OAK_API void oak_apply_attr_hooks(const struct oak_compile_options_t* opts,
                                  struct oak_obj_fn_t* fn_obj,
                                  struct oak_obj_native_fn_t* native_obj,
                                  const char** attrs,
                                  int attr_count);

/* Register a named attribute with optional compile-time and runtime callbacks.
 * on_decl fires once per declaration during the compilation pass.
 * on_call fires before every call to a function bearing the attribute;
 * returning OAK_FN_CALL_RUNTIME_ERROR aborts the call.
 * Either callback may be NULL. Returns 0 on success, -1 on invalid arguments.
 */
OAK_API int oak_bind_attr(struct oak_compile_options_t* opts,
                          const struct oak_bind_attr_t* params);

/* ---------- Runtime helpers ---------- */

/* Wrap a C instance pointer in an Oak value typed as the given native type.
 * The resulting Oak value participates in normal refcounting. When its
 * refcount reaches zero, `type->destructor` runs on non-NULL `instance`
 * (if registered), then the wrapper is freed. If `destructor` is NULL,
 * `instance` is not freed — lifetime is the embedder's responsibility.
 * `instance` may be NULL for sentinel / placeholder values. */
OAK_API struct oak_value_t
oak_native_record_new(struct oak_allocator_t* allocator,
                      const struct oak_bind_type_t* type,
                      void* instance);

/* Extract the raw C instance pointer from a native record Oak value.
 * Asserts that `value` is actually a native record (OAK_OBJ_NATIVE_RECORD).
 * Intended for use inside getter / setter callbacks:
 *   MyType* p = oak_native_instance(self); */
OAK_API void* oak_native_instance(struct oak_value_t value);

/* ---------- Extended compilation ---------- */

/* Like oak_compile() but registers native types and functions from `opts`
 * into the compiler before the first pass so that Oak source code can refer
 * to them by name.
 * `opts` may be NULL, in which case this is identical to oak_compile(). */
OAK_API void oak_compile_ex(const struct oak_ast_node_t* root,
                            const struct oak_compile_options_t* opts,
                            struct oak_compile_result_t* out);
