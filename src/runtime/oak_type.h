#pragma once

#include "oak_type_id.h"
#include "oak_types.h"

#define OAK_MAX_TYPES 64

/* Discriminates the three shapes a compile-time type slot can have. The
 * default (zero) value is OAK_TYPE_KIND_SCALAR so that zero-initialised
 * oak_type_t structs are valid scalar types without explicit assignment. */
enum oak_type_kind_t
{
  OAK_TYPE_KIND_SCALAR = 0, /* plain value: number, bool, string, user struct */
  OAK_TYPE_KIND_ARRAY,      /* typed array; element type is `id` */
  OAK_TYPE_KIND_MAP, /* typed map; key type is `key_id`, value type is `id` */
};

/* A typed slot.
 * - kind == OAK_TYPE_KIND_ARRAY  → value is an array whose element type is
 * `id`.
 * - kind == OAK_TYPE_KIND_MAP    → value is a map; value type is `id`, key is
 * `key_id`.
 * - kind == OAK_TYPE_KIND_SCALAR → plain scalar (number, bool, string, struct).
 * Two slots are equal iff `id`, `kind`, and (when MAP) `key_id` all match. */
struct oak_type_t
{
  oak_type_id_t id;
  oak_type_id_t key_id;
  enum oak_type_kind_t kind;
};

struct oak_type_entry_t
{
  const char* name;
  usize len;
};

struct oak_type_registry_t
{
  struct oak_type_entry_t entries[OAK_MAX_TYPES];
  int count;
};

/* Initializes the registry and pre-populates the built-in type ids. */
void oak_type_registry_init(struct oak_type_registry_t* reg);

/* Returns the id of an existing entry, or OAK_TYPE_UNKNOWN if not found. */
oak_type_id_t oak_type_registry_lookup(const struct oak_type_registry_t* reg,
                                       const char* name,
                                       usize len);

/* Returns the id of an existing entry, or registers a new one. Returns
 * OAK_TYPE_UNKNOWN if the registry is full. */
oak_type_id_t oak_type_registry_intern(struct oak_type_registry_t* reg,
                                       const char* name,
                                       usize len);

/* Like oak_type_registry_intern but uses a caller-supplied `id` instead of
 * assigning the next sequential one.  Use this when native types have been
 * pre-assigned stable ids by oak_bind_type() so that the compiler registry
 * matches those ids exactly.
 * Returns `id` on success, or OAK_TYPE_UNKNOWN if the slot is already
 * occupied by a different name or `id` is out of range. */
oak_type_id_t oak_type_registry_intern_with_id(struct oak_type_registry_t* reg,
                                               const char* name,
                                               usize len,
                                               oak_type_id_t id);

/* Returns a printable name for `id` (always non-null; "<unknown>" if the id
 * is invalid). The returned string lives as long as the registry. */
const char* oak_type_registry_name(const struct oak_type_registry_t* reg,
                                   oak_type_id_t id);

/* Convenience helpers for oak_type_t. */
static inline void oak_type_clear(struct oak_type_t* t)
{
  t->id = OAK_TYPE_UNKNOWN;
  t->key_id = OAK_TYPE_UNKNOWN;
  t->kind = OAK_TYPE_KIND_SCALAR;
}

static inline int oak_type_is_known(const struct oak_type_t* t)
{
  return t->id != OAK_TYPE_UNKNOWN;
}

static inline int oak_type_is_void(const struct oak_type_t* t)
{
  return t->kind == OAK_TYPE_KIND_SCALAR && t->id == OAK_TYPE_VOID;
}

static inline int oak_type_equal(const struct oak_type_t* a,
                                 const struct oak_type_t* b)
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
static inline int oak_type_is_refcounted(const struct oak_type_t* t)
{
  if (t->kind != OAK_TYPE_KIND_SCALAR)
    return 1;
  if (t->id == OAK_TYPE_STRING)
    return 1;
  if (t->id >= OAK_TYPE_FIRST_USER)
    return 1;
  return 0;
}
