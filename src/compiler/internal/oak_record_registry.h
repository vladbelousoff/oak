#pragma once

#include "oak_defs.h"
#include "oak_fn_registry.h"
#include "oak_htable.h"
#include "oak_type.h"

struct oak_record_field_t
{
  /* Borrowed pointer into the lexer arena (lives for the compilation). */
  const char* name;
  struct oak_type_t type;
  /* 1 if the field lies on a strong type-graph cycle and is therefore
   * write-once: settable only in a record literal, never by assignment.
   * Computed by oak_compiler_check_cycles. */
  int cycle_locked;
};

struct oak_registered_record_t
{
  const char* name;
  oak_type_id_t type_id;
  u16 source_module_id;
  struct oak_record_field_t* fields;
  /* Instance and static methods share one growable array, distinguished by
   * `is_static` on each entry. Freed by oak_record_registry_free. */
  struct oak_registered_fn_t* methods;
  /* Attribute names.  Always heap-allocated; freed by registry_free. */
  const char** attrs;
  int attr_count;
  /* 1 for inline value types (OAK_BIND_TYPE_VALUE): scalar, non-refcounted,
   * represented inline as OAK_TAG_NATIVE rather than a heap object. */
  int is_value;
};

/* Concrete dynamic-array type for registered records. */

/* Unbounded registry of user record types.
 * by_name gives O(1) name lookup; find_by_type_id uses a linear scan
 * (type_id lookups are infrequent and record counts remain small). */
struct oak_record_registry_t
{
  struct oak_allocator_t* allocator;
  struct oak_htable_t by_name; /* name bytes → index */
  struct oak_registered_record_t* entries;
};

/* ---------- Lifecycle ---------- */

void oak_record_registry_init(struct oak_record_registry_t* r,
                              struct oak_allocator_t* allocator);
void oak_record_registry_free(struct oak_record_registry_t* r);

/* ---------- Operations ---------- */

/* Appends record and indexes it by name. Returns pointer to the stored entry.
 */
struct oak_registered_record_t*
oak_record_registry_insert(struct oak_record_registry_t* r,
                           const struct oak_registered_record_t* s);

/* O(1) lookup by name. Returns null if not found. */
const struct oak_registered_record_t* oak_records_find(
    const struct oak_record_registry_t* r, const char* name);

/* O(n) lookup by type_id (infrequent; records stay small). */
const struct oak_registered_record_t*
oak_records_find_by_id(const struct oak_record_registry_t* r,
                       oak_type_id_t type_id);
