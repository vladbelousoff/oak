#pragma once

#include "oak_container.h"
#include "oak_defs.h"
#include "oak_type.h"
#include "oak_vector.h"

#include <string.h>

typedef struct oak_ast_node oak_ast_node_t;

typedef struct oak_interface_method oak_interface_method_t;
struct oak_interface_method
{
  const char* name;
  int arity;  /* total, including self */
  const oak_ast_node_t* sig_decl; /* set for local interfaces; null for imported */
  const oak_ast_node_t* decl; /* for default impl; null = abstract */
  int self_is_mut;
  oak_type_t* param_types;
  oak_type_t return_type;
};

typedef struct oak_registered_interface oak_registered_interface_t;
struct oak_registered_interface
{
  const char* name;
  oak_type_id_t interface_id;
  u16 source_module_id;
  oak_container_t* methods; /* vector of oak_interface_method_t */
};

/* One entry per (concrete record, interface) impl pair.
 * vtable[i] is the const_idx of the function implementing interface method i.
 * vtable_array_const_idx is the chunk constant index for the OAK_OBJ_ARRAY
 * of function values used at runtime by OAK_OP_MAKE_INTERFACE_OBJECT. */
typedef struct oak_interface_impl oak_interface_impl_t;
struct oak_interface_impl
{
  oak_type_id_t interface_id;
  oak_type_id_t record_type_id;
  u16* vtable;
  int vtable_count;
  u16 vtable_array_const_idx; /* set during oak_compiler_build_vtables() */
  int vtable_built;
};

typedef struct oak_interface_registry oak_interface_registry_t;
struct oak_interface_registry
{
  oak_allocator_t* allocator;
  oak_container_t* interfaces; /* vector of oak_registered_interface_t */
  oak_container_t* impls;      /* vector of oak_interface_impl_t */
};

void oak_interface_registry_init(oak_interface_registry_t* r,
                             oak_allocator_t* allocator);
void oak_interface_registry_free(oak_interface_registry_t* r);


static inline const oak_registered_interface_t*
oak_interface_find(const oak_interface_registry_t* r,
                const char* name)
{
  const oak_registered_interface_t* interfaces =
      OAK_CDATA(oak_registered_interface_t, r->interfaces);
  for (usize i = 0; i < oak_size(r->interfaces); ++i)
    if (strcmp(interfaces[i].name, name) == 0)
      return &interfaces[i];
  return null;
}

static inline const oak_registered_interface_t*
oak_interface_find_by_id(const oak_interface_registry_t* r, oak_type_id_t id)
{
  const oak_registered_interface_t* interfaces =
      OAK_CDATA(oak_registered_interface_t, r->interfaces);
  for (usize i = 0; i < oak_size(r->interfaces); ++i)
    if (interfaces[i].interface_id == id)
      return &interfaces[i];
  return null;
}

/* Returns the method slot index within the interface, or -1 if not found. */
static inline int oak_interface_method_slot(const oak_registered_interface_t* tr,
                                         const char* name)
{
  const oak_interface_method_t* methods =
      OAK_CDATA(oak_interface_method_t, tr->methods);
  for (usize i = 0; i < oak_size(tr->methods); ++i)
    if (strcmp(methods[i].name, name) == 0)
      return (int)i;
  return -1;
}

/* Find the impl record for (record_type_id, interface_id), or NULL. */
static inline oak_interface_impl_t*
oak_interface_impl_find(oak_interface_registry_t* r,
                     oak_type_id_t record_type_id,
                     oak_type_id_t interface_id)
{
  oak_interface_impl_t* impls =
      OAK_DATA(oak_interface_impl_t, r->impls);
  for (usize i = 0; i < oak_size(r->impls); ++i)
  {
    if (impls[i].record_type_id == record_type_id &&
        impls[i].interface_id == interface_id)
      return &impls[i];
  }
  return null;
}
