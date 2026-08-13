#pragma once

#include "oak_compiler.h"
#include "oak_container.h"
#include "oak_export.h"
#include "oak_parser.h"
#include "oak_type.h"
#include "oak_type_id.h"
#include "oak_types.h"
#include "oak_value.h"
#include "oak_vector.h"

typedef struct oak_module_registry oak_module_registry_t;
typedef struct oak_module oak_module_t;
typedef struct oak_bind_type oak_bind_type_t;


typedef enum oak_bind_type_kind oak_bind_type_kind_t;
enum oak_bind_type_kind
{
  OAK_BIND_TYPE_RECORD,
  /* An inline value type: instances live directly in the 8-byte oak_value_t
   * (an opaque pointer/handle payload of at most 61 bits) with no heap
   * wrapper, no refcount, and no destructor.  Value types expose data through
   * methods only — they cannot declare data fields (oak_bind_field rejects
   * them). */
  OAK_BIND_TYPE_VALUE,
};

/* Where a native method is bound on its receiver type (see oak_bind_fn). */
typedef enum oak_bind_fn_kind oak_bind_fn_kind_t;
typedef enum oak_bind_fn_kind oak_bind_fn_kind_t;
enum oak_bind_fn_kind
{
  OAK_BIND_FN_INSTANCE_METHOD,
  OAK_BIND_FN_STATIC_METHOD,
};


/* A compile-time type slot used for native fields, function parameters, and
 * return types.  Mirrors the internal oak_type_t and reuses oak_type_kind_t so
 * it can express scalars, typed arrays (element type = `id`), and typed maps
 * (value = `id`, key = `key_id`).  Construct with the OAK_BIND_* macros below;
 * a zero-initialised ref is a scalar of type OAK_TYPE_VOID. */
typedef struct oak_bind_type_ref oak_bind_type_ref_t;
typedef struct oak_bind_type_ref oak_bind_type_ref_t;
struct oak_bind_type_ref
{
  oak_type_id_t id;          /* element/value type */
  oak_type_id_t key_id;      /* map key type; ignored for non-map kinds */
  const oak_bind_type_t* type;     /* custom element/value type */
  const oak_bind_type_t* key_type; /* custom map key type */
  oak_type_kind_t kind; /* SCALAR / ARRAY / MAP */
};

/* Constructor helper for oak_bind_type_ref_t. Implemented as a function rather
 * than a compound literal so the OAK_BIND_* macros expand to an expression that
 * is valid anywhere, including in initializers of static storage. */
static inline oak_bind_type_ref_t oak_bind_type_ref_make(
    oak_type_id_t id, oak_type_id_t key_id, oak_type_kind_t kind)
{
  oak_bind_type_ref_t ref;
  ref.id = id;
  ref.key_id = key_id;
  ref.type = null;
  ref.key_type = null;
  ref.kind = kind;
  return ref;
}

/* Convenience constructors for oak_bind_type_ref_t. */
#define OAK_BIND_SCALAR(tid)                                                   \
  oak_bind_type_ref_make((tid), 0, OAK_TYPE_KIND_SCALAR)
#define OAK_BIND_ARRAY(elem)                                                   \
  oak_bind_type_ref_make((elem), 0, OAK_TYPE_KIND_ARRAY)
#define OAK_BIND_MAP(k, v)                                                     \
  oak_bind_type_ref_make((v), (k), OAK_TYPE_KIND_MAP)

static inline oak_bind_type_ref_t oak_bind_type_ref_native_make(
    const oak_bind_type_t* type,
    const oak_bind_type_t* key_type,
    oak_type_kind_t kind)
{
  oak_bind_type_ref_t ref = oak_bind_type_ref_make(
      OAK_TYPE_VOID, OAK_TYPE_VOID, kind);
  ref.type = type;
  ref.key_type = key_type;
  return ref;
}

#define OAK_BIND_NATIVE(t)                                                     \
  oak_bind_type_ref_native_make((t), null, OAK_TYPE_KIND_SCALAR)
#define OAK_BIND_NATIVE_ARRAY(t)                                               \
  oak_bind_type_ref_native_make((t), null, OAK_TYPE_KIND_ARRAY)
#define OAK_BIND_NATIVE_MAP(k, v)                                              \
  oak_bind_type_ref_native_make((v), (k), OAK_TYPE_KIND_MAP)


/* Returns the field value for the native record instance `self`.
 * The returned value is owned by the caller (the VM): for object values the
 * getter must return a fresh reference (refcount already incremented); for
 * scalar values (number, bool) no refcounting is required.
 * `user_data` is the oak_bind_field_t::user_data pointer for this field.
 * Access the underlying C pointer with oak_native_instance(self). */
typedef oak_value_t (*oak_bind_field_getter_t)(oak_value_t self,
                                               void* user_data);

/* Writes `value` into the native record instance `self`.
 * `user_data` is the oak_bind_field_t::user_data pointer for this field.
 * A NULL setter makes the field read-only; the VM will emit a runtime error
 * if Oak code attempts to assign to such a field. */
typedef void (*oak_bind_field_setter_t)(oak_value_t self,
                                        oak_value_t value,
                                        void* user_data);

/* Optional: frees heap data owned by `instance` when the native record's
 * refcount reaches zero. If NULL, only the wrapper object is freed (legacy). */
typedef void (*oak_bind_destructor_t)(void* instance);


typedef struct oak_bind_field oak_bind_field_t;
struct oak_bind_field
{
  const char* name;
  /* Compile-time type of this field. Use OAK_BIND_* for builtins and
   * OAK_BIND_NATIVE* for custom descriptors. */
  oak_bind_type_ref_t type;
  oak_bind_field_getter_t getter;
  oak_bind_field_setter_t setter; /* NULL = read-only */
  /* Optional pointer passed to both getter and setter; borrowed and must
   * outlive every value of this type. */
  void* user_data;
};


typedef struct oak_bind_type oak_bind_type_t;
struct oak_bind_type
{
  oak_bind_type_kind_t kind;
  /* Optional native module name. When set, the type is exported from that
   * synthetic module and imported as `module.Type`; when NULL, it is
   * registered in the global namespace as before. */
  const char* module_name;
  const char* name;
  /* Assigned when this descriptor is installed into a module/compiler
   * registry. Embedders should reference the descriptor, not this value. */
  oak_type_id_t resolved_type_id; /* private */
  oak_container_t* fields; /* vector of oak_bind_field_t */
  oak_bind_destructor_t destructor;
  oak_allocator_t* allocator;
};


/* Use oak_bind_fn_global() to register a free function or module-scoped
 * function (e.g. math.sqrt).  Global functions are not attached to any type. */
typedef struct oak_bind_global_fn oak_bind_global_fn_t;
typedef struct oak_bind_global_fn oak_bind_global_fn_t;
struct oak_bind_global_fn
{
  /* NULL for a top-level global; "math" to scope it as `math.fn()`. */
  const char* module_name;
  const char* name;
  oak_native_fn_t impl;
  int arity;
  /* Return type.  Build with OAK_BIND_SCALAR/ARRAY/MAP. */
  oak_bind_type_ref_t return_type;
  /* Optional per-parameter types used for call-site type checking.  When
   * non-NULL, param_types must list `arity` entries; the embedder owns the
   * array (it is copied at registration) and it must outlive oak_compile_ex. */
  const oak_bind_type_ref_t* param_types;
  int param_count;
  /* Optional pointer surfaced to `impl` as oak_native_ctx_t::user_data;
   * borrowed and must outlive every chunk compiled with this binding. */
  void* user_data;
};


/* Use oak_bind_fn() to register instance or static methods on a native type.
 * For global or module-scoped functions use oak_bind_fn_global() instead. */
typedef struct oak_bind_fn oak_bind_fn_t;
typedef struct oak_bind_fn oak_bind_fn_t;
struct oak_bind_fn
{
  oak_bind_fn_kind_t kind; /* INSTANCE_METHOD or STATIC_METHOD */
  /* Native record descriptor for the receiver type. */
  const oak_bind_type_t* receiver_type;
  const char* name;
  oak_native_fn_t impl;
  /* User-visible arity: for STATIC_METHOD, full argument count;
   * for INSTANCE_METHOD, excludes implicit self (compiler adds +1 for VM). */
  int arity;
  /* Return type.  Build with OAK_BIND_SCALAR/ARRAY/MAP. */
  oak_bind_type_ref_t return_type;
  /* Optional per-parameter types used for call-site type checking.  param_types
   * lists the user-visible parameters (excluding the implicit self for instance
   * methods) and must hold `arity` entries when non-NULL.  It is borrowed and
   * must outlive oak_compile_ex. */
  const oak_bind_type_ref_t* param_types;
  int param_count;
  /* Optional pointer surfaced to `impl` as oak_native_ctx_t::user_data;
   * borrowed and must outlive every chunk compiled with this binding. */
  void* user_data;
};


/* A single variant of a native-bound enum: a name plus an integer value.
 * Variants are exposed to Oak source as `EnumName.Variant`, lowering to the
 * variant's integer value (the same shape as user-declared enums). */
typedef struct oak_bind_enum_variant oak_bind_enum_variant_t;
typedef struct oak_bind_enum_variant oak_bind_enum_variant_t;
struct oak_bind_enum_variant
{
  const char* name;
  int value;
};

typedef struct oak_bind_enum oak_bind_enum_t;
struct oak_bind_enum
{
  /* Optional native module name. When set, variants are exported from that
   * synthetic module and referenced as `module.Enum.Variant`. */
  const char* module_name;
  const char* name;
  oak_container_t* variants; /* vector of oak_bind_enum_variant_t */
  oak_allocator_t* allocator;
};


/* Target kind of a declaration bearing an attribute. */
typedef enum oak_attr_target oak_attr_target_t;
typedef enum oak_attr_target oak_attr_target_t;
enum oak_attr_target
{
  OAK_ATTR_TARGET_FN,
  OAK_ATTR_TARGET_METHOD,
  OAK_ATTR_TARGET_RECORD,
  OAK_ATTR_TARGET_ENUM,
};

/* Per-parameter metadata exposed to attribute callbacks on FN/METHOD targets. */
typedef struct oak_attr_param_info oak_attr_param_info_t;
typedef struct oak_attr_param_info oak_attr_param_info_t;
struct oak_attr_param_info
{
  const char* name;
  const char* type_name;
  oak_type_id_t type_id;
  int is_mut;
  int is_weak;
};

/* Per-field metadata exposed to attribute callbacks on RECORD targets. */
typedef struct oak_attr_field_info oak_attr_field_info_t;
typedef struct oak_attr_field_info oak_attr_field_info_t;
struct oak_attr_field_info
{
  const char* name;
  const char* type_name;
  oak_type_id_t type_id;
};

/* Context passed to compile-time attribute callbacks. */
typedef struct oak_compile_options oak_compile_options_t;

typedef struct oak_attr_compile_ctx oak_attr_compile_ctx_t;
struct oak_attr_compile_ctx
{
  oak_attr_target_t target;
  const char* decl_name; /* name of the declaration bearing this attribute */
  void* user_data;
  /* For FN/METHOD targets: function parameter metadata. */
  int param_count;
  const oak_attr_param_info_t* params;
  /* For RECORD targets: field metadata. */
  int field_count;
  const oak_attr_field_info_t* fields;
  /* Index of the declaration's value in the chunk's constant pool (-1 if N/A). */
  int const_index;

  /* The live compile options the compiler is using for this compilation.
   * Native types/functions bound here (via oak_bind_type / oak_bind_fn) are
   * picked up by the native-binding pass that runs right after record
   * declarations, so an on_decl callback can register new types/methods that
   * later code in the same module can reference. The bound structs are
   * allocated with opts->allocator, so store any pointer the embedder needs at
   * runtime — it outlives compilation. */
  oak_compile_options_t* opts;
};

typedef void (*oak_attr_compile_cb_t)(const oak_attr_compile_ctx_t* ctx);

/* oak_attr_runtime_cb_t is defined in oak_value.h (included above). */

typedef struct oak_bind_attr oak_bind_attr_t;
struct oak_bind_attr
{
  const char* name;              /* e.g. "Deprecated" */
  oak_attr_compile_cb_t on_decl; /* NULL = no compile-time action */
  oak_attr_runtime_cb_t on_call; /* NULL = no pre-call action */
  void* user_data;               /* forwarded to both callbacks */
};


typedef struct oak_compile_options oak_compile_options_t;
struct oak_compile_options
{
  /* Allocator used for all compilation and runtime allocations. */
  oak_allocator_t* allocator;

  /* Optional: path or label for the Oak source (borrowed). Set on the chunk. */
  const char* source_name;

  /* Native record types (owned; populated by oak_bind_type). */
  oak_container_t* native_types; /* vector of oak_bind_type_t* */

  /* Native method bindings (owned; populated by oak_bind_fn). */
  oak_container_t* native_fns; /* vector of oak_bind_fn_t */

  /* Native global and module-scoped functions (owned; populated by oak_bind_fn_global). */
  oak_container_t* native_global_fns; /* oak_bind_global_fn_t */

  /* Native enums (owned; populated by oak_bind_enum / oak_bind_enum_variant).
   */
  oak_container_t* native_enums; /* vector of oak_bind_enum_t* */

  /* Named attribute bindings (owned; populated by oak_bind_attr). */
  oak_container_t* native_attrs; /* vector of oak_bind_attr_t */

  /* When non-zero (default), the compiler attaches a debug section to the
   * chunk: per-byte source line/column and local-variable names. Set to 0 to
   * skip these allocations and produce a minimal runtime-only chunk. */
  int emit_debug_info;

  /* Module-system context (both null when compiling standalone — preserves
   * the original single-file behaviour).  When set, the compiler uses
   * `current_module` to attach exports and to resolve `import alias.name`
   * references via `module_registry`. */
  oak_module_registry_t* module_registry;
  oak_module_t* current_module;

  /* Internal module-loader option: native stdlib declaration modules contain
   * function signatures without Oak bodies because their implementations are
   * provided by native bindings. */
  int allow_bodyless_fns;

  /* When non-zero, importing a native module whose Oak stub file cannot be
   * found synthesizes the module from the registered native bindings alone
   * instead of failing. The synthesized module carries only what the bindings
   * describe, so everything the stub adds on top (parameter types, mutability,
   * stub-only declarations) is lost and calls are checked more loosely —
   * hence it is off by default and a missing stub is an error. Enable
   * it for hosts that have no filesystem to load the stub from (the WebAssembly
   * playground) or via the CLI's --allow-synthetic-modules. */
  int allow_synthetic_native_modules;
};


OAK_API void oak_compile_options_init(oak_compile_options_t* opts,
                                     oak_allocator_t* allocator);
OAK_API void oak_compile_options_free(oak_compile_options_t* opts);


/* Allocate a native type descriptor, register it in opts, and return a pointer
 * for subsequent field/method/signature bindings. The descriptor is owned by
 * opts and freed by oak_compile_options_free; do not free it separately.
 * Returns NULL if opts or name is NULL. */
OAK_API oak_bind_type_t* oak_bind_type(oak_compile_options_t* opts,
                                              oak_bind_type_kind_t kind,
                                              const char* name);

OAK_API oak_bind_type_t* oak_bind_type_in_module(
    oak_compile_options_t* opts,
    const char* module_name,
    oak_bind_type_kind_t kind,
    const char* name);

/* Register a field on a native type.  Fields are assigned indices in
 * registration order, matching the order the compiler resolves them.
 * `params` must not be NULL; it supplies name, field_type_id, getter, and
 * optional setter (same shape as oak_bind_field_t).
 * Returns 0 on success, -1 if a field with the same name already exists. */
OAK_API int oak_bind_field(oak_bind_type_t* type,
                           const oak_bind_field_t* params);

/* Register a global or module-scoped native function.
 * Use this for free functions like `to_int(v)` or `math.sqrt(v)`.
 * Returns 0 on success, -1 on invalid arguments. */
OAK_API int oak_bind_fn_global(oak_compile_options_t* opts,
                               const oak_bind_global_fn_t* params);

/* Register a native instance or static method on a native type.
 * `params->kind` must be OAK_BIND_FN_INSTANCE_METHOD or OAK_BIND_FN_STATIC_METHOD.
 * `params->receiver_type` must be a descriptor from a prior oak_bind_type() call.
 *   INSTANCE_METHOD: `arity` excludes implicit self (compiler adds +1 for VM).
 *   STATIC_METHOD: `arity` is the full argument count; called as TypeName.name(...).
 * Returns 0 on success, -1 on invalid arguments. */
OAK_API int oak_bind_fn(oak_compile_options_t* opts,
                        const oak_bind_fn_t* params);

/* Allocate a native enum descriptor and register it in opts.  Returns a
 * pointer for subsequent oak_bind_enum_variant calls; the descriptor is owned
 * by opts and freed by oak_compile_options_free.  Returns NULL on invalid
 * arguments. */
OAK_API oak_bind_enum_t* oak_bind_enum(oak_compile_options_t* opts,
                                              const char* name);

OAK_API oak_bind_enum_t* oak_bind_enum_in_module(
    oak_compile_options_t* opts,
    const char* module_name,
    const char* name);

/* Append a variant to a native enum.  Variant values must be unique within
 * an enum is not enforced — they are forwarded as-is to Oak as integer
 * constants.  Returns 0 on success, -1 if a variant with the same name
 * already exists in this enum. */
OAK_API int oak_bind_enum_variant(oak_bind_enum_t* e,
                                  const char* name,
                                  int value);

/* Match attrs[] against opts->native_attrs and fire on_decl for each
 * matching binding that has one.  target and decl_name identify the
 * declaration.  params/fields provide structured metadata about the
 * declaration (NULL when not applicable).
 * Safe to call with empty attrs or no bindings. */
OAK_API void
oak_dispatch_compile_attr_cbs(const oak_compile_options_t* opts,
                              const char** attrs,
                              int attr_count,
                              const char* decl_name,
                              oak_attr_target_t target,
                              const oak_attr_param_info_t* params,
                              int param_count,
                              const oak_attr_field_info_t* fields,
                              int field_count,
                              int const_index);

/* Match attrs[] against opts->native_attrs and attach all bindings whose
 * on_call is non-NULL as a heap-allocated hooks array on fn_obj or native_obj
 * (exactly one must be non-NULL).  Safe to call with no matches. */
OAK_API void oak_apply_attr_hooks(const oak_compile_options_t* opts,
                                  oak_obj_fn_t* fn_obj,
                                  oak_obj_native_fn_t* native_obj,
                                  const char** attrs,
                                  int attr_count);

/* Register a named attribute with optional compile-time and runtime callbacks.
 * on_decl fires once per declaration during the compilation pass.
 * on_call fires before every call to a function bearing the attribute;
 * returning OAK_FN_CALL_RUNTIME_ERROR aborts the call.
 * Either callback may be NULL. Returns 0 on success, -1 on invalid arguments.
 */
OAK_API int oak_bind_attr(oak_compile_options_t* opts,
                          const oak_bind_attr_t* params);


/* Wrap a C instance pointer in a process-shared Oak value typed as the given
 * native type.  Use oak_vm_native_record_new() inside a native callback when
 * the wrapper should belong to that callback's VM.  The resulting Oak value
 * participates in normal refcounting. When its
 * refcount reaches zero, `type->destructor` runs on non-NULL `instance`
 * (if registered), then the wrapper is freed. If `destructor` is NULL,
 * `instance` is not freed — lifetime is the embedder's responsibility.
 * `instance` may be NULL for sentinel / placeholder values. */
OAK_API oak_value_t
oak_native_record_new(oak_allocator_t* allocator,
                      const oak_bind_type_t* type,
                      void* instance);

/* Extract the raw C instance pointer from a native record Oak value.
 * Asserts that `value` is actually a native record (OAK_OBJ_NATIVE_RECORD).
 * Intended for use inside getter / setter callbacks:
 *   MyType* p = oak_native_instance(self); */
OAK_API void* oak_native_instance(oak_value_t value);

/* Wrap an opaque pointer/handle as an inline value-type Oak value (a type
 * registered with OAK_BIND_TYPE_VALUE).  No allocation or refcounting occurs;
 * the payload is stored directly in the value and copied bitwise.  The embedder
 * owns whatever `payload` points to (if anything) — the runtime never frees or
 * dereferences it. */
OAK_API oak_value_t oak_native_value_new(void* payload);

/* Recover the payload from an inline value-type Oak value.  Asserts that
 * `value` is an inline native value.  Intended for use inside native methods
 * bound on an OAK_BIND_TYPE_VALUE type:  MyHandle h = oak_native_value(args[0]); */
OAK_API void* oak_native_value(oak_value_t value);


/* Like oak_compile() but registers native types and functions from `opts`
 * into the compiler before the first pass so that Oak source code can refer
 * to them by name.
 * `opts` may be NULL, in which case this is identical to oak_compile(). */
OAK_API void oak_compile_ex(const oak_ast_node_t* root,
                            const oak_compile_options_t* opts,
                            oak_compile_result_t* out);
