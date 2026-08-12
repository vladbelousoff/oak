#pragma once

#include "oak_container.h"
#include "oak_hash_map.h"
#include "oak_hash_set.h"
#include "oak_type.h"
#include "oak_vector.h"

/* A single variant of a user-defined enum, lowered to a named integer
 * constant in the chunk's constant pool. */
typedef struct oak_enum_variant oak_enum_variant_t;
struct oak_enum_variant
{
  /* Borrowed pointers into the lexer arena (live for the compilation). */
  const char* name;
  const char* enum_name;
  u16 const_idx;
  int value;
  /* Type id of the enum that owns this variant.  Populated when the enum
   * is registered (program, native, or import) so that expressions like
   * `EnumName.Variant` infer to a distinct type rather than `number`. */
  oak_type_id_t type_id;
};

/* Per-enum metadata (attributes). Variants are stored separately in the
 * registry's `variants` vector; this struct holds enum-level information. */
typedef struct oak_registered_enum oak_registered_enum_t;
struct oak_registered_enum
{
  const char* name;
  oak_type_id_t type_id;
  u16 source_module_id;
  /* Attribute names.  Always heap-allocated; freed by registry_free. */
  const char** attrs;
  int attr_count;
};

/* Unbounded registry of enum variants.
 * by_name gives O(1) unqualified variant lookup.
 * enum_names gives O(1) existence check for enum type names.
 * Qualified lookup (EnumName::Variant) uses a linear scan — it is rare. */
typedef struct oak_enum_registry oak_enum_registry_t;
struct oak_enum_registry
{
  oak_allocator_t* allocator;
  oak_container_t* by_name;    /* variant name → usize into variants */
  oak_container_t* enum_names; /* set of enum type names             */
  oak_container_t* variants;   /* vector of oak_enum_variant_t       */
  oak_container_t* enums;      /* vector of oak_registered_enum_t    */
};


void oak_enum_registry_init(oak_enum_registry_t* r,
                            oak_allocator_t* allocator);
void oak_enum_registry_free(oak_enum_registry_t* r);


/* Appends variant and indexes it by name and enum name. */
oak_enum_variant_t*
oak_enum_registry_insert(oak_enum_registry_t* r,
                         const oak_enum_variant_t* v);

/* O(1) lookup by unqualified variant name. Returns null if not found. */
const oak_enum_variant_t* oak_enum_registry_find(
    const oak_enum_registry_t* r, const char* name);

/* O(n) lookup by qualified (enum_name, variant_name). */
const oak_enum_variant_t*
oak_enums_find_qualified(const oak_enum_registry_t* r,
                                 const char* enum_name,
                                 const char* variant_name);

/* O(1) check: is this name a registered enum type name? */
int oak_is_enum_name(const oak_enum_registry_t* r, const char* name);

/* O(n) lookup by name. Returns null if not found. */
const oak_registered_enum_t* oak_enum_find(
    const oak_enum_registry_t* r, const char* name);
