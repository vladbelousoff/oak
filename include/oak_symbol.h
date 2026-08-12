#pragma once

#include "oak_container.h"
#include "oak_hash_map.h"
#include "oak_types.h"
#include "oak_vector.h"

typedef struct oak_allocator oak_allocator_t;
typedef struct oak_module_export_fn oak_module_export_fn_t;
typedef struct oak_module_export_record oak_module_export_record_t;
typedef struct oak_module_export_enum oak_module_export_enum_t;
typedef struct oak_module_export_interface oak_module_export_interface_t;

typedef enum oak_symbol_kind oak_symbol_kind_t;
enum oak_symbol_kind
{
  OAK_SYMBOL_FUNCTION,
  OAK_SYMBOL_RECORD,
  OAK_SYMBOL_ENUM,
  OAK_SYMBOL_INTERFACE,
  OAK_SYMBOL_GLOBAL,
  OAK_SYMBOL_MODULE_ALIAS,
};

typedef struct oak_symbol oak_symbol_t;
struct oak_symbol
{
  const char* name;
  oak_symbol_kind_t kind;
  u16 owner_module_id;
  int payload_index;
  int is_exported;
  int is_imported;
};

typedef struct oak_symbol_registry oak_symbol_registry_t;
struct oak_symbol_registry
{
  oak_allocator_t* allocator;
  oak_container_t* by_name; /* name → usize index into symbols */
  oak_container_t* symbols; /* vector of oak_symbol_t */

  /* Payload vectors, indexed by oak_symbol_t.payload_index. */
  oak_container_t* fns;        /* oak_module_export_fn_t        */
  oak_container_t* records;    /* oak_module_export_record_t    */
  oak_container_t* enums;      /* oak_module_export_enum_t      */
  oak_container_t* interfaces; /* oak_module_export_interface_t */
};

void oak_symbol_registry_init(oak_symbol_registry_t* registry,
                              oak_allocator_t* allocator);
void oak_symbol_registry_free(oak_symbol_registry_t* registry);

/* Inserts a symbol into the module namespace. Returns null on collision. */
oak_symbol_t* oak_symbol_registry_insert(
    oak_symbol_registry_t* registry, const oak_symbol_t* symbol);

const oak_symbol_t* oak_symbol_registry_find(
    const oak_symbol_registry_t* registry, const char* name);

/* Typed insert: appends payload to the internal array and inserts a symbol
 * with payload_index set automatically. Returns stored payload, null on
 * collision. */
oak_module_export_fn_t*
oak_symbol_registry_insert_fn(oak_symbol_registry_t* registry,
                              const char* name,
                              u16 owner_module_id,
                              const oak_module_export_fn_t* fn);

oak_module_export_record_t*
oak_symbol_registry_insert_record(oak_symbol_registry_t* registry,
                                  const char* name,
                                  u16 owner_module_id,
                                  const oak_module_export_record_t* rec);

oak_module_export_enum_t*
oak_symbol_registry_insert_enum(oak_symbol_registry_t* registry,
                                const char* name,
                                u16 owner_module_id,
                                const oak_module_export_enum_t* en);

oak_module_export_interface_t*
oak_symbol_registry_insert_interface(oak_symbol_registry_t* registry,
                                 const char* name,
                                 u16 owner_module_id,
                                 const oak_module_export_interface_t* tr);

/* Typed find: name lookup + kind check + payload dereference. */
const oak_module_export_fn_t*
oak_symbol_registry_find_fn(const oak_symbol_registry_t* registry,
                            const char* name);

const oak_module_export_record_t*
oak_symbol_registry_find_record(const oak_symbol_registry_t* registry,
                                const char* name);

const oak_module_export_enum_t*
oak_symbol_registry_find_enum(const oak_symbol_registry_t* registry,
                              const char* name);

const oak_module_export_interface_t*
oak_symbol_registry_find_interface(const oak_symbol_registry_t* registry,
                               const char* name);
