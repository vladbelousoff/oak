#pragma once

#include "oak_compiler.h"
#include "oak_container.h"
#include "oak_count_of.h"
#include "oak_export.h"
#include "oak_native.h"
#include "oak_parser.h"
#include "oak_type_id.h"
#include "oak_type_kind.h"
#include "oak_types.h"
#include "oak_vector.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oak_module_registry oak_module_registry_t;
typedef struct oak_module oak_module_t;
typedef struct oak_bind_type oak_bind_type_t;
typedef struct oak_bind_enum oak_bind_enum_t;


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

/* The largest arity a binding may declare.  Bytecode encodes a call's argument
 * count in a single byte, so this is a hard ceiling rather than a style limit:
 * oak_bind_fn and oak_bind_fn_global reject anything above it, instead of
 * letting the count wrap silently when the call is emitted. */
#define OAK_MAX_ARITY 255u

/* Where a native method is bound on its receiver type (see oak_bind_fn). */
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
 * a zero-initialised ref is a scalar of type OAK_TYPE_VOID.
 *
 * A ref can also name a native enum registered with oak_bind_enum -- use
 * OAK_BIND_ENUM, so that `f(EnumName.Variant)` type-checks at the call site. */
typedef struct oak_bind_type_ref oak_bind_type_ref_t;
struct oak_bind_type_ref
{
  oak_type_id_t id;          /* element/value type */
  oak_type_id_t key_id;      /* map key type; ignored for non-map kinds */
  const oak_bind_type_t* type;     /* custom element/value type */
  const oak_bind_type_t* key_type; /* custom map key type */
  /* Native enum element/value type.  Mutually exclusive with `type`. */
  const oak_bind_enum_t* enum_type;
  oak_type_kind_t kind; /* SCALAR / ARRAY / MAP */
  /* Non-owning reference; set with OAK_BIND_WEAK.  Zero (owning) by default. */
  int is_weak;
};

/* Constructor helper for oak_bind_type_ref_t. Implemented as a function rather
 * than a compound literal so the OAK_BIND_* macros expand to an expression
 * usable wherever a value is expected, including as a struct field initializer
 * in automatic storage.
 *
 * A call is not a constant expression, though, so these cannot initialize an
 * object with static storage duration. For a `static const` table of type
 * refs, use the OAK_BIND_*_INIT brace-initializer forms below instead. */
static inline oak_bind_type_ref_t oak_bind_type_ref_make(
    oak_type_id_t id, oak_type_id_t key_id, oak_type_kind_t kind)
{
  oak_bind_type_ref_t ref;
  ref.id = id;
  ref.key_id = key_id;
  ref.type = null;
  ref.key_type = null;
  ref.enum_type = null;
  ref.kind = kind;
  ref.is_weak = 0;
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

static inline oak_bind_type_ref_t oak_bind_type_ref_enum_make(
    const oak_bind_enum_t* e, oak_type_kind_t kind)
{
  oak_bind_type_ref_t ref = oak_bind_type_ref_make(
      OAK_TYPE_VOID, OAK_TYPE_VOID, kind);
  ref.enum_type = e;
  return ref;
}

/* A parameter, return or field typed as a native enum registered with
 * oak_bind_enum.  Prefer this over OAK_TYPE_NUMBER so the compiler accepts
 * `f(EnumName.Variant)` and rejects a bare integer.  The descriptor must have
 * been registered on the same options; its type id is assigned during
 * compilation, before any signature that references it is lowered. */
#define OAK_BIND_ENUM(e)                                                       \
  oak_bind_type_ref_enum_make((e), OAK_TYPE_KIND_SCALAR)
#define OAK_BIND_ENUM_ARRAY(e)                                                 \
  oak_bind_type_ref_enum_make((e), OAK_TYPE_KIND_ARRAY)

/* Brace-initializer forms of the three builtin constructors, for objects with
 * static storage duration -- typically a `static const oak_bind_type_ref_t[]`
 * shared between a binding's param_types and its callback's own guard:
 *
 *   static const oak_bind_type_ref_t params[] = {
 *     OAK_BIND_SCALAR_INIT(OAK_TYPE_STRING),
 *     OAK_BIND_SCALAR_INIT(OAK_TYPE_NUMBER),
 *   };
 *
 * There is no OAK_BIND_NATIVE_INIT or OAK_BIND_ENUM_INIT: those descriptors
 * come from oak_bind_type / oak_bind_enum at run time, so a table referring to
 * one cannot be static anyway -- make it a local array and use the
 * OAK_BIND_NATIVE / OAK_BIND_ENUM forms. */
#define OAK_BIND_SCALAR_INIT(tid)                                              \
  { .id = (tid), .key_id = 0, .kind = OAK_TYPE_KIND_SCALAR }
#define OAK_BIND_ARRAY_INIT(elem)                                              \
  { .id = (elem), .key_id = 0, .kind = OAK_TYPE_KIND_ARRAY }
#define OAK_BIND_MAP_INIT(k, v)                                                \
  { .id = (v), .key_id = (k), .kind = OAK_TYPE_KIND_MAP }

/* A non-owning reference, for a field or parameter that must not keep its
 * target alive.  Wraps any of the constructors above:
 *
 *   .type = OAK_BIND_WEAK(OAK_BIND_NATIVE(node_type))
 *
 * This is how a native binding stays inside the acyclicity rule the compiler
 * enforces (see src/compiler/oak_compiler_cycles.c): Oak has no cycle
 * collector, so a back-pointer that would close a strong ownership loop has to
 * be declared weak, exactly as it would in Oak source.  A weak field takes part
 * in the same analysis as a declared one. */
static inline oak_bind_type_ref_t oak_bind_type_ref_weaken(
    oak_bind_type_ref_t ref)
{
  ref.is_weak = 1;
  return ref;
}

#define OAK_BIND_WEAK(ref) oak_bind_type_ref_weaken((ref))


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
 * if Oak code attempts to assign to such a field.
 *
 * `value` is *borrowed*: the VM releases it as soon as the setter returns, so
 * a setter that stores an object value must oak_value_incref it first.  This
 * is the mirror image of the getter above, which returns an owned reference. */
typedef void (*oak_bind_field_setter_t)(oak_value_t self,
                                        oak_value_t value,
                                        void* user_data);

/* Optional: frees heap data owned by `instance` when the native record's
 * refcount reaches zero.  If NULL, only the wrapper object is freed and
 * `instance` is the embedder's to manage.
 *
 * `user_data` is oak_bind_type_t::user_data, and is how a destructor reaches
 * the allocator that made `instance` -- it runs during refcount teardown, with
 * no VM and no call in scope, so there is nowhere else for that to come from. */
typedef void (*oak_bind_destructor_t)(void* instance, void* user_data);


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
  /* The options this descriptor was registered on, so oak_bind_field can
   * record a rejection there.  Borrowed. */
  struct oak_compile_options* opts; /* private */
  oak_bind_destructor_t destructor;
  /* Passed to `destructor`; borrowed, and must outlive every instance of this
   * type.  Typically the allocator that instances were made with. */
  void* user_data;
  oak_allocator_t* allocator;
};


/* Use oak_bind_fn_global() to register a free function or module-scoped
 * function (e.g. math.sqrt).  Global functions are not attached to any type. */
typedef struct oak_bind_global_fn oak_bind_global_fn_t;
struct oak_bind_global_fn
{
  /* NULL for a top-level global; "math" to scope it as `math.fn()`. */
  const char* module_name;
  const char* name;
  oak_native_fn_t impl;
  usize arity;
  /* Return type.  Build with OAK_BIND_SCALAR/ARRAY/MAP. */
  oak_bind_type_ref_t return_type;
  /* Optional per-parameter types used for call-site type checking.  When
   * non-NULL, param_types must list `arity` entries.  Registration copies this
   * struct but not the array it points at, so the array is borrowed: the
   * embedder owns it and it must outlive oak_compile_ex. */
  const oak_bind_type_ref_t* param_types;
  usize param_count;
  /* Optional pointer surfaced to `impl` as oak_native_call_t::user_data;
   * borrowed and must outlive every chunk compiled with this binding. */
  void* user_data;
};


/* Use oak_bind_fn() to register instance or static methods on a native type.
 * For global or module-scoped functions use oak_bind_fn_global() instead. */
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
  usize arity;
  /* Return type.  Build with OAK_BIND_SCALAR/ARRAY/MAP. */
  oak_bind_type_ref_t return_type;
  /* Optional per-parameter types used for call-site type checking.  param_types
   * lists the user-visible parameters (excluding the implicit self for instance
   * methods) and must hold `arity` entries when non-NULL.  Registration copies
   * this struct but not the array it points at, so the array is borrowed: the
   * embedder owns it and it must outlive oak_compile_ex. */
  const oak_bind_type_ref_t* param_types;
  usize param_count;
  /* Optional pointer surfaced to `impl` as oak_native_call_t::user_data;
   * borrowed and must outlive every chunk compiled with this binding. */
  void* user_data;
};


/* A single variant of a native-bound enum: a name plus an integer value.
 * Variants are exposed to Oak source as `EnumName.Variant`, lowering to the
 * variant's integer value (the same shape as user-declared enums). */
typedef struct oak_bind_enum_variant oak_bind_enum_variant_t;
struct oak_bind_enum_variant
{
  const char* name;
  int value;
};

struct oak_bind_enum
{
  /* Optional native module name. When set, variants are exported from that
   * synthetic module and referenced as `module.Enum.Variant`. */
  const char* module_name;
  const char* name;
  /* Assigned when this descriptor is installed into a module/compiler
   * registry. Embedders should reference the descriptor via OAK_BIND_ENUM,
   * not this value. */
  oak_type_id_t resolved_type_id; /* private */
  oak_container_t* variants; /* vector of oak_bind_enum_variant_t */
  /* As oak_bind_type_t::opts, for oak_bind_enum_variant.  Borrowed. */
  struct oak_compile_options* opts; /* private */
  oak_allocator_t* allocator;
};


/* Target kind of a declaration bearing an attribute. */
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

/* oak_attr_runtime_cb_t is declared in oak_native.h (included above). */

typedef struct oak_bind_attr oak_bind_attr_t;
struct oak_bind_attr
{
  const char* name;              /* e.g. "Deprecated" */
  oak_attr_compile_cb_t on_decl; /* NULL = no compile-time action */
  oak_attr_runtime_cb_t on_call; /* NULL = no pre-call action */
  void* user_data;               /* forwarded to both callbacks */
};


/* Compilation inputs plus the registry of native bindings.
 *
 * Lifetime: this struct must outlive the VM and every native value created
 * from it, not merely the call to oak_compile_ex.  Each native-record object
 * retains a pointer to its oak_bind_type_t descriptor and reads
 * `destructor` from it when its refcount reaches zero, and those descriptors
 * are owned by these options.  Tear down in this order:
 *
 *   oak_vm_free(&vm);
 *   oak_compile_result_free(&result);
 *   oak_compile_options_free(&opts);
 *
 * Freeing the options first leaves every live native value pointing at a
 * released descriptor. */
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

  /* Messages from oak_bind_* calls that rejected their input (owned).
   *
   * A rejected binding used to vanish twice over: the oak_bind_* call returned
   * a bare -1 with no reason, and the compiler's registration pass then skipped
   * the malformed entry. The method simply was not there, and the first sign of
   * it was an "unknown method" error in Oak source, pointing at the call rather
   * than the binding. oak_compile_ex reports these as diagnostics before it
   * starts, so a mis-registered binding fails the compile and says why.
   *
   * Append-only: attribute callbacks register further bindings mid-compile. */
  oak_container_t* bind_errors; /* vector of char* */

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


/*
 * Every oak_bind_* function below returns 0 on success and -1 on rejection.
 *
 * A rejection is also recorded on the options and reported by oak_compile_ex
 * as a diagnostic, so a binding that failed to register cannot be silently
 * dropped just because the -1 was not checked -- the compile fails and names
 * the binding. That means these calls are not for probing what the API will
 * accept: use a throwaway oak_compile_options_t if you want to test a
 * rejection without failing the compile.
 */

OAK_API void oak_compile_options_init(oak_compile_options_t* opts,
                                      oak_allocator_t* allocator);

/* Release every descriptor registered into `opts` and null the pointers, so
 * calling this twice is a no-op rather than a double free.  Safe on a
 * zero-initialized options struct.
 *
 * Of the strings passed to the oak_bind_* functions, only field names were
 * copied; every other name is borrowed (see the individual declarations).
 * Must be called after oak_vm_free — see the lifetime note on
 * oak_compile_options_t. */
OAK_API void oak_compile_options_free(oak_compile_options_t* opts);


/* Allocate a native type descriptor, register it in opts, and return a pointer
 * for subsequent field/method/signature bindings. The descriptor is owned by
 * opts and freed by oak_compile_options_free; do not free it separately.
 * `name` is borrowed and must outlive `opts`.
 * Returns NULL if opts or name is NULL. */
OAK_API oak_bind_type_t* oak_bind_type(oak_compile_options_t* opts,
                                       oak_bind_type_kind_t kind,
                                       const char* name);

/* As oak_bind_type, but exports the type from the synthetic module
 * `module_name` so Oak source refers to it as `module.Type`.
 * Both `module_name` and `name` are borrowed and must outlive `opts`. */
OAK_API oak_bind_type_t* oak_bind_type_in_module(
    oak_compile_options_t* opts,
    const char* module_name,
    oak_bind_type_kind_t kind,
    const char* name);

/* Register a field on a native type.  Fields are assigned indices in
 * registration order, matching the order the compiler resolves them.
 * `params` must not be NULL; it supplies name, field_type_id, getter, and
 * optional setter (same shape as oak_bind_field_t).
 * Unlike the other oak_bind_* entry points, `params->name` is copied, so it
 * need not outlive this call.
 * Returns 0 on success, -1 if a field with the same name already exists. */
OAK_API int oak_bind_field(oak_bind_type_t* type,
                           const oak_bind_field_t* params);

/*
 * Batch forms of the four registration calls.
 *
 * Each registers `count` descriptors in order and returns 0 only if every one
 * succeeded, -1 if any failed (earlier ones stay registered). They exist so a
 * binding can be written as a static table rather than a run of near-identical
 * compound literals:
 *
 *   static const oak_bind_fn_t methods[] = {
 *     { .kind = OAK_BIND_FN_INSTANCE_METHOD, .receiver_type = t,
 *       .name = "read", .impl = file_read, .arity = 0,
 *       .return_type = OAK_BIND_SCALAR(OAK_TYPE_STRING) },
 *     ...
 *   };
 *   oak_bind_fns(opts, methods, (int)oak_count_of(methods));
 *
 * Note the table cannot be `static const` when it references a descriptor
 * returned at run time by oak_bind_type; make it a local array in that case.
 */
/* Fills arity, param_types and param_count from one array, so the three cannot
 * disagree.  oak_bind_fn does reject a mismatch, but only after you have
 * written the count twice:
 *
 *   { .kind = OAK_BIND_FN_INSTANCE_METHOD, .receiver_type = t,
 *     .name = "write", .impl = file_write, OAK_BIND_PARAMS(write_params),
 *     .return_type = OAK_BIND_SCALAR(OAK_TYPE_VOID) },
 *
 * For an instance method the array lists the explicit parameters only; the
 * implicit self is not one of them. */
#define OAK_BIND_PARAMS(arr)                                                   \
  .arity = oak_count_of(arr), .param_types = (arr),                            \
  .param_count = oak_count_of(arr)

OAK_API int oak_bind_fields(oak_bind_type_t* type,
                            const oak_bind_field_t* fields,
                            int count);
OAK_API int oak_bind_fns(oak_compile_options_t* opts,
                         const oak_bind_fn_t* fns,
                         int count);
OAK_API int oak_bind_fns_global(oak_compile_options_t* opts,
                                const oak_bind_global_fn_t* fns,
                                int count);
OAK_API int oak_bind_enum_variants(oak_bind_enum_t* e,
                                   const oak_bind_enum_variant_t* variants,
                                   int count);

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
 * by opts and freed by oak_compile_options_free.  `name` is borrowed and must
 * outlive `opts`.  Returns NULL on invalid arguments. */
OAK_API oak_bind_enum_t* oak_bind_enum(oak_compile_options_t* opts,
                                       const char* name);

/* As oak_bind_enum, but exports the variants from the synthetic module
 * `module_name` so Oak source refers to them as `module.Enum.Variant`.
 * Both `module_name` and `name` are borrowed and must outlive `opts`. */
OAK_API oak_bind_enum_t* oak_bind_enum_in_module(
    oak_compile_options_t* opts,
    const char* module_name,
    const char* name);

/* Append a variant to a native enum.  Values are forwarded to Oak as integer
 * constants as-is; uniqueness of *values* within an enum is not enforced.
 * `name` is borrowed and must outlive the enum's owning options.
 * Returns 0 on success, -1 if a variant with the same *name* already exists
 * in this enum. */
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


/* Raise a runtime error from inside a native callback, and return
 * OAK_FN_CALL_RUNTIME_ERROR so that failing is one line:
 *
 *   if (!fp)
 *     return oak_native_error(call, "cannot open '%s'", path);
 *
 * The message is prefixed with the binding's name and reaches the embedder
 * through oak_vm_last_error() like any other runtime error.  A native that
 * returns OAK_FN_CALL_RUNTIME_ERROR without calling this still reports, but
 * only as "native function '<name>' failed" -- which cannot distinguish a
 * missing file from a bad argument.  Prefer this at every failure point.
 *
 * Safe to call with a null `call` or a call with no VM: the message is then
 * logged and discarded, and the return value is unchanged. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
OAK_API oak_fn_call_result_t
oak_native_error(oak_native_call_t* call, const char* fmt, ...);


/*
 * Checked argument accessors.
 *
 * Each reads args[i], and returns non-zero having written *out.  On a missing
 * or wrongly-typed argument each raises a runtime error naming the binding,
 * the parameter position and the expected versus actual type, then returns 0 --
 * so a guarded callback reads as:
 *
 *   int fathoms;
 *   const char* name;
 *   if (!oak_arg_i32(call, args, argc, 0, &fathoms) ||
 *       !oak_arg_cstring(call, args, argc, 1, &name))
 *     return OAK_FN_CALL_RUNTIME_ERROR;
 *
 * and every diagnostic is phrased once, here, rather than per callback.
 *
 * These do not check arity: the VM already rejects an argc mismatch before a
 * callback runs (on every path, including oak_vm_call from C), so an
 * `argc != N` guard in a callback is dead code.  An index past argc is still
 * reported rather than read, because a callback reached through some future
 * path with fewer arguments must not read off the end.
 *
 * The string from oak_arg_cstring points into the argument, which the VM
 * releases when the call returns; copy it if it must outlive the callback.
 */
OAK_API int oak_arg_i32(oak_native_call_t* call,
                        const oak_value_t* args,
                        usize argc,
                        usize i,
                        int* out);
OAK_API int oak_arg_f32(oak_native_call_t* call,
                        const oak_value_t* args,
                        usize argc,
                        usize i,
                        float* out);
/* Accepts either numeric representation and widens, which is what a native
 * doing float maths wants; oak_arg_f32 insists on a float. */
OAK_API int oak_arg_number(oak_native_call_t* call,
                           const oak_value_t* args,
                           usize argc,
                           usize i,
                           float* out);
OAK_API int oak_arg_bool(oak_native_call_t* call,
                         const oak_value_t* args,
                         usize argc,
                         usize i,
                         int* out);
OAK_API int oak_arg_cstring(oak_native_call_t* call,
                            const oak_value_t* args,
                            usize argc,
                            usize i,
                            const char** out);
/* The string object rather than a C pointer, so the length travels with it:
 * an Oak string may contain embedded NULs, which oak_arg_cstring cannot
 * express.  Prefer this for anything that measures, slices or copies. */
OAK_API int oak_arg_string(oak_native_call_t* call,
                           const oak_value_t* args,
                           usize argc,
                           usize i,
                           const oak_obj_string_t** out);

/* The checked form of oak_native_instance: verifies args[i] is a native record
 * of exactly `type` before yielding its instance pointer.  oak_native_instance
 * asserts only that the value is *some* native record and then reinterprets
 * whatever C struct is behind it, so a receiver of the wrong bound type is
 * silently misread -- reachable from C through oak_vm_call, where no
 * compile-time check applies.  A null instance is refused too. */
OAK_API int oak_arg_native(oak_native_call_t* call,
                           const oak_value_t* args,
                           usize argc,
                           usize i,
                           const oak_bind_type_t* type,
                           void** out);

/* oak_arg_native against args[0] and the receiver type of the method being
 * called.  For an instance method bound with oak_bind_fn this is the whole
 * receiver check:
 *
 *   my_handle_t* h;
 *   if (!oak_arg_self(call, args, argc, (void**)&h))
 *     return OAK_FN_CALL_RUNTIME_ERROR;
 *
 * Fails if the binding is not an instance method (there is no receiver). */
OAK_API int oak_arg_self(oak_native_call_t* call,
                         const oak_value_t* args,
                         usize argc,
                         void** out);

/* Wrap `instance` as a value of the receiver type of the method being called,
 * owned by that call's VM.  The one-liner form of
 * oak_vm_native_record_new(call->vm, call->self_type, instance), for the
 * common case of a static method acting as a constructor for its own type.
 * Returns OAK_VALUE_NONE if the binding has no receiver type. */
OAK_API oak_value_t oak_native_self_new(oak_native_call_t* call,
                                        void* instance);


/* Non-zero when `value` satisfies the declared type `ref`.
 * A ref of OAK_TYPE_VOID matches anything, which is how an unspecified
 * parameter type behaves at a call site. */
OAK_API int oak_value_matches(oak_value_t value, oak_bind_type_ref_t ref);

/* Non-zero when argc == count and every args[i] satisfies types[i].
 *
 * Checks a whole signature in one call, against the same array passed as
 * oak_bind_fn_t::param_types, so the signature is written once and serves both
 * the compiler's call-site checking and the callback's own guard:
 *
 *   static const oak_bind_type_ref_t params[] = {
 *     OAK_BIND_SCALAR(OAK_TYPE_STRING), OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
 *   };
 *   static oak_fn_call_result_t my_fn(oak_native_call_t* call,
 *                                     const oak_value_t* args,
 *                                     const usize argc, oak_value_t* out)
 *   {
 *     if (!oak_native_args_match(args, argc, params, oak_count_of(params)))
 *       return OAK_FN_CALL_RUNTIME_ERROR;
 *     ...
 *   }
 *
 * Prefer the oak_arg_* accessors above for anything that then reads the
 * arguments: they unwrap and check in one step, and they say which argument
 * was wrong and why, where this only answers yes or no.  This remains useful
 * for a callback that wants to validate a signature it does not immediately
 * destructure.
 *
 * Guarding at all is still worth it even with param_types declared: that makes
 * the compiler check Oak call sites, but a native can also be reached from C
 * through oak_vm_call with arbitrary values.
 *
 * For an instance method, args[0] is the receiver and param_types describes
 * only the explicit parameters, so check the tail -- guarding argc first,
 * since it is unsigned:
 *   argc > 0 && oak_native_args_match(args + 1, argc - 1, params, count) */
OAK_API int oak_native_args_match(const oak_value_t* args,
                                  usize argc,
                                  const oak_bind_type_ref_t* types,
                                  usize count);


/* Compile a parsed AST into bytecode, registering the native types, functions
 * and enums described by `opts` before the first pass so that Oak source can
 * refer to them by name.
 *
 * The caller retains ownership of `root`; the AST is not freed here.  `out`
 * must be initialized by the caller (zero-initialization is enough); it
 * receives the chunk on success or diagnostics on failure, and is released
 * with oak_compile_result_free.
 *
 * `opts` may be NULL to compile with no bindings, in which case allocation
 * falls back to the process-wide system allocator — pass options with an
 * explicit allocator if you want to account for compiler allocations. */
OAK_API void oak_compile_ex(const oak_ast_node_t* root,
                            const oak_compile_options_t* opts,
                            oak_compile_result_t* out);

#ifdef __cplusplus
}
#endif
