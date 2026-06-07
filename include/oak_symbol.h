#pragma once

#include "oak_htable.h"
#include "oak_types.h"

struct oak_allocator_t;
struct oak_module_export_fn_t;
struct oak_module_export_record_t;
struct oak_module_export_enum_t;
struct oak_module_export_trait_t;

enum oak_symbol_kind_t
{
  OAK_SYMBOL_FUNCTION,
  OAK_SYMBOL_RECORD,
  OAK_SYMBOL_ENUM,
  OAK_SYMBOL_TRAIT,
  OAK_SYMBOL_GLOBAL,
  OAK_SYMBOL_MODULE_ALIAS,
};

struct oak_symbol_t
{
  const char* name;
  enum oak_symbol_kind_t kind;
  u16 owner_module_id;
  int payload_index;
  int is_exported;
  int is_imported;
};

struct oak_symbol_registry_t
{
  struct oak_allocator_t* allocator;
  struct oak_htable_t by_name;
  struct oak_symbol_t* symbols;

  struct oak_module_export_fn_t* fns;
  struct oak_module_export_record_t* records;
  struct oak_module_export_enum_t* enums;
  struct oak_module_export_trait_t* traits;
};

void oak_symbol_registry_init(struct oak_symbol_registry_t* registry,
                              struct oak_allocator_t* allocator);
void oak_symbol_registry_free(struct oak_symbol_registry_t* registry);

/* Inserts a symbol into the module namespace. Returns null on collision. */
struct oak_symbol_t* oak_symbol_registry_insert(
    struct oak_symbol_registry_t* registry, const struct oak_symbol_t* symbol);

const struct oak_symbol_t* oak_symbol_registry_find(
    const struct oak_symbol_registry_t* registry, const char* name);

/* Typed insert: appends payload to the internal array and inserts a symbol
 * with payload_index set automatically. Returns stored payload, null on
 * collision. */
struct oak_module_export_fn_t*
oak_symbol_registry_insert_fn(struct oak_symbol_registry_t* registry,
                              const char* name,
                              u16 owner_module_id,
                              const struct oak_module_export_fn_t* fn);

struct oak_module_export_record_t*
oak_symbol_registry_insert_record(struct oak_symbol_registry_t* registry,
                                  const char* name,
                                  u16 owner_module_id,
                                  const struct oak_module_export_record_t* rec);

struct oak_module_export_enum_t*
oak_symbol_registry_insert_enum(struct oak_symbol_registry_t* registry,
                                const char* name,
                                u16 owner_module_id,
                                const struct oak_module_export_enum_t* en);

struct oak_module_export_trait_t*
oak_symbol_registry_insert_trait(struct oak_symbol_registry_t* registry,
                                 const char* name,
                                 u16 owner_module_id,
                                 const struct oak_module_export_trait_t* tr);

/* Typed find: name lookup + kind check + payload dereference. */
const struct oak_module_export_fn_t*
oak_symbol_registry_find_fn(const struct oak_symbol_registry_t* registry,
                            const char* name);

const struct oak_module_export_record_t*
oak_symbol_registry_find_record(const struct oak_symbol_registry_t* registry,
                                const char* name);

const struct oak_module_export_enum_t*
oak_symbol_registry_find_enum(const struct oak_symbol_registry_t* registry,
                              const char* name);

const struct oak_module_export_trait_t*
oak_symbol_registry_find_trait(const struct oak_symbol_registry_t* registry,
                               const char* name);
