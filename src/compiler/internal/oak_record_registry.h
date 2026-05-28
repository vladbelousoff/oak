#pragma once

#include "oak_defs.h"
#include "oak_fn_registry.h"
#include "oak_htable.h"
#include "oak_type.h"

struct oak_record_field_t
{
  /* Borrowed pointer into the lexer arena (lives for the compilation). */
  const char* name;
  int name_len;
  struct oak_type_t type;
};

struct oak_registered_record_t
{
  const char* name;
  int name_len;
  oak_type_id_t type_id;
  u16 source_module_id;
  struct oak_record_field_t* fields;
  int field_count;
  int field_capacity;
  /* Instance and static methods share one growable array, distinguished by
   * `is_static` on each entry. Freed by oak_record_registry_free. */
  struct oak_registered_fn_vec_t methods;
  /* Attribute names.  Always heap-allocated; freed by registry_free. */
  const char** attrs;
  int attr_count;
  int generic_param_count;
  int generic_def_index;
};

/* Concrete dynamic-array type for registered records. */
struct oak_registered_record_vec_t
{
  struct oak_registered_record_t* items;
  int count;
  int capacity;
};

/* Unbounded registry of user record types.
 * by_name gives O(1) name lookup; find_by_type_id uses a linear scan
 * (type_id lookups are infrequent and record counts remain small). */
struct oak_record_registry_t
{
  struct oak_allocator_t* allocator;
  struct oak_htable_t by_name; /* name bytes → index */
  struct oak_registered_record_vec_t entries;
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
    const struct oak_record_registry_t* r, const char* name, usize len);

/* O(n) lookup by type_id (infrequent; records stay small). */
const struct oak_registered_record_t*
oak_records_find_by_id(const struct oak_record_registry_t* r,
                                    oak_type_id_t type_id);
