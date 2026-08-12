#pragma once

#include "oak_container.h"
#include "oak_hash_map.h"
#include "oak_str.h"
#include "oak_type.h"
#include "oak_vector.h"
#include <oak_compiler.h>

/* decl is null for native (C) functions/methods registered at compile time.
 * receiver_type_id == OAK_TYPE_VOID means global function; any other value
 * means a method on the record with that type_id.
 * return_type_id == OAK_TYPE_VOID means void (or inferred from decl).
 * is_static distinguishes static methods (no implicit self) from instance
 * methods (implicit self at slot 0); ignored for global fns. */
typedef struct oak_registered_fn oak_registered_fn_t;
struct oak_registered_fn
{
  const char* name;
  u16 const_idx;
  /* For global fns and static methods: user-facing arity.
   * For instance methods: total arity including the implicit self receiver
   * (so user writes N args, stored as N+1). */
  int arity;
  oak_type_id_t receiver_type_id; /* OAK_TYPE_VOID = global function */
  oak_type_t return_type; /* void when inferred from decl */
  int is_static;   /* 1 = static method, 0 = instance/global */
  int is_exported;
  const oak_ast_node_t* decl; /* null for native and imported fns */
  /* Per-parameter resolved types and mutability for imported functions (decl is
   * null).  NULL when the function has no parameters or is locally declared. */
  oak_type_t* param_types;
  u8* param_mut_flags;
  /* Attribute names (e.g. "Native", "Deprecated").
   * Always heap-allocated; freed by registry_free. */
  const char** attrs;
  int attr_count;
  u16 source_module_id;
  u16 source_const_idx;
};

/* Unbounded registry of user-declared and native fns.
 * Lookup is O(1) via the hash map; entries owns the storage. */
typedef struct oak_fn_registry oak_fn_registry_t;
struct oak_fn_registry
{
  oak_allocator_t* allocator;
  oak_container_t* by_name; /* name → usize index into entries */
  oak_container_t* entries; /* vector of oak_registered_fn_t */
};


void oak_fn_registry_init(oak_fn_registry_t* r,
                          oak_allocator_t* allocator);
void oak_fn_registry_free(oak_fn_registry_t* r);


/* Appends fn and indexes it by name. Returns pointer to the stored entry. */
oak_registered_fn_t*
oak_fn_registry_insert(oak_fn_registry_t* r,
                       const oak_registered_fn_t* fn);

/* O(1) lookup by name. Returns null if not found. */
const oak_registered_fn_t* oak_fn_registry_find(
    const oak_fn_registry_t* r, const char* name);
