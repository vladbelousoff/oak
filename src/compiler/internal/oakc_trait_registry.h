#pragma once

#include "oakc_defs.h"
#include "oak_type.h"

struct oak_ast_node_t;

struct oak_trait_method_t
{
  const char* name;
  usize name_len;
  int arity;  /* total, including self */
  const struct oak_ast_node_t* sig_decl; /* always set — the trait method declaration */
  const struct oak_ast_node_t* decl; /* for default impl; NULL = abstract */
};

struct oak_registered_trait_t
{
  const char* name;
  usize name_len;
  oak_type_id_t trait_id;
  struct oak_trait_method_t* methods;
  int method_count;
  int method_capacity;
};

/* One entry per (concrete record, trait) impl pair.
 * vtable[i] is the const_idx of the function implementing trait method i.
 * vtable_array_const_idx is the chunk constant index for the OAK_OBJ_ARRAY
 * of function values used at runtime by OAK_OP_MAKE_TRAIT_OBJECT. */
struct oak_trait_impl_t
{
  oak_type_id_t trait_id;
  oak_type_id_t record_type_id;
  u16* vtable;
  int vtable_count;
  u16 vtable_array_const_idx; /* set during oak_compiler_build_vtables() */
  int vtable_built;
};

struct oak_trait_registry_t
{
  struct oak_allocator_t* allocator;
  struct oak_registered_trait_t* traits;
  int trait_count;
  int trait_capacity;
  struct oak_trait_impl_t* impls;
  int impl_count;
  int impl_capacity;
};

void oak_trait_registry_init(struct oak_trait_registry_t* r,
                             struct oak_allocator_t* allocator);
void oak_trait_registry_free(struct oak_trait_registry_t* r);

/* ---------- Lookups ---------- */

static inline const struct oak_registered_trait_t*
oakc_trait_find(const struct oak_trait_registry_t* r,
                const char* name,
                usize len)
{
  for (int i = 0; i < r->trait_count; ++i)
  {
    const struct oak_registered_trait_t* t = &r->traits[i];
    if (t->name_len == len && memcmp(t->name, name, len) == 0)
      return t;
  }
  return null;
}

static inline const struct oak_registered_trait_t*
oakc_trait_find_by_id(const struct oak_trait_registry_t* r, oak_type_id_t id)
{
  for (int i = 0; i < r->trait_count; ++i)
    if (r->traits[i].trait_id == id)
      return &r->traits[i];
  return null;
}

/* Returns the method slot index within the trait, or -1 if not found. */
static inline int oakc_trait_method_slot(const struct oak_registered_trait_t* tr,
                                         const char* name,
                                         usize len)
{
  for (int i = 0; i < tr->method_count; ++i)
    if (tr->methods[i].name_len == len &&
        memcmp(tr->methods[i].name, name, len) == 0)
      return i;
  return -1;
}

/* Find the impl record for (record_type_id, trait_id), or NULL. */
static inline struct oak_trait_impl_t*
oakc_trait_impl_find(struct oak_trait_registry_t* r,
                     oak_type_id_t record_type_id,
                     oak_type_id_t trait_id)
{
  for (int i = 0; i < r->impl_count; ++i)
  {
    if (r->impls[i].record_type_id == record_type_id &&
        r->impls[i].trait_id == trait_id)
      return &r->impls[i];
  }
  return null;
}
