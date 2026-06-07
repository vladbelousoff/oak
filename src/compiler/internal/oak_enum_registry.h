#pragma once

#include "oak_htable.h"
#include "oak_type.h"

/* A single variant of a user-defined enum, lowered to a named integer
 * constant in the chunk's constant pool. */
struct oak_enum_variant_t
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

/* Concrete dynamic-array type for enum variants. */

/* Per-enum metadata (attributes). Variants are stored separately in
 * oak_enum_variant_vec_t; this struct holds enum-level information. */
struct oak_registered_enum_t
{
  const char* name;
  oak_type_id_t type_id;
  u16 source_module_id;
  /* Attribute names.  Always heap-allocated; freed by registry_free. */
  const char** attrs;
  int attr_count;
};

/* Concrete dynamic-array type for registered enums. */

/* Unbounded registry of enum variants.
 * by_name gives O(1) unqualified variant lookup.
 * enum_names gives O(1) existence check for enum type names.
 * Qualified lookup (EnumName::Variant) uses a linear scan — it is rare. */
struct oak_enum_registry_t
{
  struct oak_allocator_t* allocator;
  struct oak_htable_t by_name;    /* variant name → index into variants */
  struct oak_htable_t enum_names; /* enum type name → 1 (set)           */
  struct oak_enum_variant_t* variants;
  struct oak_registered_enum_t* enums; /* one entry per enum type */
};

/* ---------- Lifecycle ---------- */

void oak_enum_registry_init(struct oak_enum_registry_t* r,
                            struct oak_allocator_t* allocator);
void oak_enum_registry_free(struct oak_enum_registry_t* r);

/* ---------- Operations ---------- */

/* Appends variant and indexes it by name and enum name. */
struct oak_enum_variant_t*
oak_enum_registry_insert(struct oak_enum_registry_t* r,
                         const struct oak_enum_variant_t* v);

/* O(1) lookup by unqualified variant name. Returns null if not found. */
const struct oak_enum_variant_t* oak_enum_registry_find(
    const struct oak_enum_registry_t* r, const char* name);

/* O(n) lookup by qualified (enum_name, variant_name). */
const struct oak_enum_variant_t*
oak_enums_find_qualified(const struct oak_enum_registry_t* r,
                                 const char* enum_name,
                                 const char* variant_name);

/* O(1) check: is this name a registered enum type name? */
int oak_is_enum_name(const struct oak_enum_registry_t* r, const char* name);

/* O(n) lookup by name. Returns null if not found. */
const struct oak_registered_enum_t* oak_enum_find(
    const struct oak_enum_registry_t* r, const char* name);
