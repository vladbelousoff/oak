#pragma once

#include "oak_defs.h"
#include "oak_type.h"

#include <string.h>

struct oak_ast_node_t;

struct oak_trait_method_t
{
  const char* name;
  int name_len;
  int arity;  /* total, including self */
  const struct oak_ast_node_t* sig_decl; /* set for local traits; null for imported */
  const struct oak_ast_node_t* decl; /* for default impl; null = abstract */
  int self_is_mut;
  struct oak_type_t* param_types;
  struct oak_type_t return_type;
};

struct oak_registered_trait_t
{
  const char* name;
  int name_len;
  oak_type_id_t trait_id;
  u16 source_module_id;
  struct oak_trait_method_t* methods;
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
  struct oak_trait_impl_t* impls;
};

void oak_trait_registry_init(struct oak_trait_registry_t* r,
                             struct oak_allocator_t* allocator);
void oak_trait_registry_free(struct oak_trait_registry_t* r);

/* ---------- Lookups ---------- */

static inline const struct oak_registered_trait_t*
oak_trait_find(const struct oak_trait_registry_t* r,
                const char* name)
{
  for (int i = 0; i < oak_dynarr_count(r->traits); ++i)
  {
    const struct oak_registered_trait_t* t = &r->traits[i];
    if (strcmp(t->name, name) == 0)
      return t;
  }
  return null;
}

static inline const struct oak_registered_trait_t*
oak_trait_find_by_id(const struct oak_trait_registry_t* r, oak_type_id_t id)
{
  for (int i = 0; i < oak_dynarr_count(r->traits); ++i)
    if (r->traits[i].trait_id == id)
      return &r->traits[i];
  return null;
}

/* Returns the method slot index within the trait, or -1 if not found. */
static inline int oak_trait_method_slot(const struct oak_registered_trait_t* tr,
                                         const char* name)
{
  for (int i = 0; i < oak_dynarr_count(tr->methods); ++i)
    if (strcmp(tr->methods[i].name, name) == 0)
      return i;
  return -1;
}

/* Find the impl record for (record_type_id, trait_id), or NULL. */
static inline struct oak_trait_impl_t*
oak_trait_impl_find(struct oak_trait_registry_t* r,
                     oak_type_id_t record_type_id,
                     oak_type_id_t trait_id)
{
  for (int i = 0; i < oak_dynarr_count(r->impls); ++i)
  {
    if (r->impls[i].record_type_id == record_type_id &&
        r->impls[i].trait_id == trait_id)
      return &r->impls[i];
  }
  return null;
}
