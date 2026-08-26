#pragma once

#include "oak_container.h"
#include "oak_type_id.h"
#include "oak_type_kind.h"
#include "oak_types.h"
#include "oak_vector.h"

typedef struct oak_allocator oak_allocator_t;
struct oak_bind_type_ref;
struct oak_bind_fn;
struct oak_obj_native_fn;

/* A typed slot.
 * - kind == OAK_TYPE_KIND_ARRAY  → value is an array whose element type is
 * `id`.
 * - kind == OAK_TYPE_KIND_MAP    → value is a map; value type is `id`, key is
 * `key_id`.
 * - kind == OAK_TYPE_KIND_SCALAR → plain scalar (number, bool, string, record).
 * `is_weak` marks a non-owning reference to a refcounted value. Two slots are
 * equal iff `id`, `kind`, `is_weak`, and (when MAP) `key_id` all match. */
typedef struct oak_type oak_type_t;
struct oak_type
{
  oak_type_id_t id;
  oak_type_id_t key_id;
  oak_type_kind_t kind;
  int is_weak;
};

typedef struct oak_type_entry oak_type_entry_t;
struct oak_type_entry
{
  const char* name;
  oak_type_id_t id;
};

typedef struct oak_type_registry oak_type_registry_t;
struct oak_type_registry
{
  oak_allocator_t* allocator;
  oak_container_t* entries; /* vector of oak_type_entry_t */
  u16 owner_module_id;
  u16 next_local_slot;
};

/* Initializes the registry and pre-populates the built-in type ids. */
void oak_type_registry_init(oak_type_registry_t* reg,
                            oak_allocator_t* allocator);
void oak_type_registry_set_owner(oak_type_registry_t* reg,
                                 u16 module_id);
void oak_type_registry_free(oak_type_registry_t* reg);

/* Returns the id of an existing entry, or -1 if not found. */
oak_type_id_t oak_type_registry_lookup(const oak_type_registry_t* reg,
                                       const char* name);

/* Returns the id of an existing entry, or registers a new one. Returns
 * -1 if `name` is null/empty. */
oak_type_id_t oak_type_registry_intern(oak_type_registry_t* reg,
                                       const char* name);

/* Catalogs a caller-supplied module-qualified ID under `name`.
 * Returns `id` on success, or -1 on a name/ID conflict. */
oak_type_id_t oak_type_registry_intern_with_id(oak_type_registry_t* reg,
                                               const char* name,
                                               oak_type_id_t id);

/* Returns a printable name for `id` (always non-null; "<void>" if the id
 * is OAK_TYPE_VOID, "<unknown>" if the id is otherwise invalid). The
 * returned string lives as long as the registry. */
const char* oak_type_registry_name(const oak_type_registry_t* reg,
                                   oak_type_id_t id);

/* Lower a public oak_bind_type_ref_t into an internal oak_type_t.
 *
 * The single place that knows how a binding's type reference resolves: a
 * native-type or native-enum descriptor contributes the id it was assigned at
 * registration, and only a MAP kind carries a key. Everything that consumes
 * oak_bind_type_ref_t goes through here -- the compiler's native registration,
 * the record-declaration cross-check, and the module loader's export
 * synthesis -- so a ref form added to the public API cannot be honoured in one
 * consumer and silently dropped in another. */
void oak_lower_bind_ref(const struct oak_bind_type_ref* r, oak_type_t* out);

/* Carry the parts of a binding a callback sees onto the native-fn object it is
 * interned as: its kind, and the native record type it is about.  Every site
 * that interns a binding needs both set the same way, so they are set in one
 * place -- see oak_native_call_t::self_type for what "about" means. */
void oak_native_fn_attach_binding(struct oak_obj_native_fn* native,
                                  const struct oak_bind_fn* binding);

/* Convenience helpers for oak_type_t. */
static inline void oak_type_clear(oak_type_t* t)
{
  t->id = OAK_TYPE_VOID;
  t->key_id = OAK_TYPE_VOID;
  t->kind = OAK_TYPE_KIND_SCALAR;
  t->is_weak = 0;
}

static inline int oak_type_is_known(const oak_type_t* t)
{
  return t->id != OAK_TYPE_VOID;
}

static inline int oak_type_is_void(const oak_type_t* t)
{
  return t->kind == OAK_TYPE_KIND_SCALAR && t->id == OAK_TYPE_VOID;
}

static inline int oak_type_equal(const oak_type_t* a,
                                 const oak_type_t* b)
{
  if (a->id != b->id)
    return 0;
  if (a->kind != b->kind)
    return 0;
  if (a->is_weak != b->is_weak)
    return 0;
  if (a->kind == OAK_TYPE_KIND_MAP && a->key_id != b->key_id)
    return 0;
  return 1;
}

static inline int oak_type_equal_base(const oak_type_t* a,
                                      const oak_type_t* b)
{
  if (a->id != b->id)
    return 0;
  if (a->kind != b->kind)
    return 0;
  if (a->kind == OAK_TYPE_KIND_MAP && a->key_id != b->key_id)
    return 0;
  return 1;
}

/* Returns 1 for types whose values are heap-allocated and reference-counted
 * (strings, arrays, maps, user-defined structs).  Returns 0 for inline value
 * types (number, bool) that are copied on every assignment and therefore have
 * no aliasing semantics that need mutability protection. */
static inline int oak_type_is_refcounted(const oak_type_t* t)
{
  if (t->kind != OAK_TYPE_KIND_SCALAR)
    return 1;
  if (t->id == OAK_TYPE_STRING)
    return 1;
  if (t->id >= OAK_TYPE_FIRST_USER)
    return 1;
  return 0;
}

static inline int oak_type_is_interface(const oak_type_t* t)
{
  return t->kind == OAK_TYPE_KIND_INTERFACE;
}
