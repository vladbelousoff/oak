#pragma once

#include "oak_container.h"
#include "oak_defs.h"
#include "oak_fn_registry.h"
#include "oak_hash_map.h"
#include "oak_type.h"
#include "oak_vector.h"

typedef struct oak_record_field oak_record_field_t;
struct oak_record_field
{
  /* Borrowed pointer into the lexer arena (lives for the compilation). */
  const char* name;
  oak_type_t type;
  /* 1 if the field lies on a strong type-graph cycle and is therefore
   * write-once: settable only in a record literal, never by assignment.
   * Computed by oak_compiler_check_cycles. */
  int cycle_locked;
};

typedef struct oak_registered_record oak_registered_record_t;
struct oak_registered_record
{
  const char* name;
  oak_type_id_t type_id;
  u16 source_module_id;
  /* Vector of oak_record_field_t. */
  oak_container_t* fields;
  /* Instance and static methods share one vector of oak_registered_fn_t,
   * distinguished by `is_static` on each entry. Freed by
   * oak_record_registry_free. */
  oak_container_t* methods;
  /* Attribute names.  Always heap-allocated; freed by registry_free. */
  const char** attrs;
  int attr_count;
  /* 1 for inline value types (OAK_BIND_TYPE_VALUE): scalar, non-refcounted,
   * represented inline as OAK_TAG_NATIVE rather than a heap object. */
  int is_value;
};

/* Unbounded registry of user record types.
 * by_name gives O(1) name lookup; find_by_type_id uses a linear scan
 * (type_id lookups are infrequent and record counts remain small). */
typedef struct oak_record_registry oak_record_registry_t;
struct oak_record_registry
{
  oak_allocator_t* allocator;
  oak_container_t* by_name; /* name → usize index into entries */
  oak_container_t* entries; /* vector of oak_registered_record_t */
};


void oak_record_registry_init(oak_record_registry_t* r,
                              oak_allocator_t* allocator);
void oak_record_registry_free(oak_record_registry_t* r);


/* Appends record and indexes it by name. Returns pointer to the stored entry.
 */
oak_registered_record_t*
oak_record_registry_insert(oak_record_registry_t* r,
                           const oak_registered_record_t* s);

/* O(1) lookup by name. Returns null if not found. */
const oak_registered_record_t* oak_records_find(
    const oak_record_registry_t* r, const char* name);

/* O(n) lookup by type_id (infrequent; records stay small). */
const oak_registered_record_t*
oak_records_find_by_id(const oak_record_registry_t* r,
                       oak_type_id_t type_id);
