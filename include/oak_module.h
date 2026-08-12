#pragma once

#include "oak_chunk.h"
#include "oak_container.h"
#include "oak_diagnostic.h"
#include "oak_export.h"
#include "oak_file_map.h"
#include "oak_hash_map.h"
#include "oak_parser.h"
#include "oak_symbol.h"
#include "oak_type.h"
#include "oak_vector.h"

/* Sentinel module_id used by native fns and the entry-only chunk before a
 * registry exists. */
#define OAK_MODULE_ID_NONE ((u16)0xFFFF)


typedef struct oak_module_export_fn oak_module_export_fn_t;
struct oak_module_export_fn
{
  const char* name; /* borrowed from the module's lexer arena */
  u16 const_idx; /* index into the module's chunk constants */
  int arity;     /* user-visible arity (no implicit self for globals) */
  /* Per-parameter resolved types and mutability flags.
   * Type IDs reference the owning module's type registry.
   * NULL when arity == 0. */
  oak_type_t* param_types;
  u8* param_mut_flags;
  oak_type_t return_type;
  /* Heap-allocated pointer array; each element points into the module's lexer
   * arena.  Populated for bodyless stubs so apply_native_module_function_exports
   * can carry attributes onto the replacement native function object.
   * NULL when attr_count == 0.  Freed by oak_module_free. */
  const char** stub_attrs;
  int stub_attr_count;
};

typedef struct oak_module_export_record_field oak_module_export_record_field_t;
struct oak_module_export_record_field
{
  const char* name; /* borrowed from lexer arena */
  oak_type_t type; /* IDs reference the owning module's type registry */
};

/* Per-method metadata for exported record methods.
 * Populated by oak_populate_module_exports; used by importing modules to
 * register callable methods on imported records. */
typedef struct oak_module_export_record_method oak_module_export_record_method_t;
struct oak_module_export_record_method
{
  const char* name;        /* borrowed from lexer arena */
  u16 const_idx;           /* index into the source module's chunk constants */
  int arity;               /* total arity including implicit self for instance */
  int is_static;           /* 1 = static method, 0 = instance method */
  oak_type_t* param_types;
  u8* param_mut_flags;
  oak_type_t return_type;
  const char** stub_attrs; /* heap-allocated array; elements from lexer arena */
  int stub_attr_count;
};

typedef struct oak_module_export_record oak_module_export_record_t;
struct oak_module_export_record
{
  const char* name; /* borrowed from lexer arena */
  oak_container_t* fields;  /* oak_module_export_record_field_t  */
  u16 layout_id; /* const-pool slot in this module's chunk (for new mod.T{}) */
  oak_container_t* methods; /* oak_module_export_record_method_t */
  /* 1 for inline value types (OAK_BIND_TYPE_VALUE): non-refcounted, inline. */
  int is_value;
};

/* One variant entry for an exported enum. */
typedef struct oak_module_export_enum_variant oak_module_export_enum_variant_t;
struct oak_module_export_enum_variant
{
  const char* name; /* borrowed from lexer arena */
  int value; /* ordinal (0, 1, 2, …) */
};

typedef struct oak_module_export_enum oak_module_export_enum_t;
struct oak_module_export_enum
{
  const char* name; /* enum type name, borrowed from lexer arena */
  oak_container_t* variants; /* oak_module_export_enum_variant_t */
};

/* Per-method metadata for an exported interface. */
typedef struct oak_module_export_interface_method oak_module_export_interface_method_t;
struct oak_module_export_interface_method
{
  const char* name;
  int arity;
  int self_is_mut;
  oak_type_t* param_types;
  oak_type_t return_type;
};

/* One exported interface declaration. */
typedef struct oak_module_export_interface oak_module_export_interface_t;
struct oak_module_export_interface
{
  const char* name;
  oak_container_t* methods; /* oak_module_export_interface_method_t */
};



typedef struct oak_module oak_module_t;
struct oak_module
{
  oak_allocator_t* allocator;

  /* Identity */
  char* canonical_path; /* owned; null-terminated; hash-table key */
  char* dotted_name;    /* owned; e.g. "a.b.c"; for diagnostics only */
  u16 module_id;        /* dense index into registry */
  int is_entry;         /* 1 if this is the program entry module */

  /* Source + parsing artefacts (owned) */
  oak_file_map_t source;
  oak_lexer_result_t* lexer;
  oak_parser_result_t parser;

  /* Bytecode (owned; null until compile succeeds) */
  oak_chunk_t* chunk;

  /* Resolved imports (alias_name -> dependency module_id) */
  oak_container_t* imports;        /* alias name → usize module_id  */
  oak_container_t* import_modules; /* vector of u16, direct deps    */

  /* Type catalog (moved from compiler after compilation).
   * Persists so imports can resolve names for module-qualified type IDs. */
  oak_type_registry_t types;

  /* Exports (populated post-compile) — unified symbol registry with payloads */
  oak_symbol_registry_t exports;

  /* Lifecycle */
  enum
  {
    OAK_MOD_PARSED,
    OAK_MOD_COMPILED,
  } state;
};


typedef struct oak_module_registry oak_module_registry_t;
struct oak_module_registry
{
  oak_allocator_t* allocator;
  oak_container_t* modules; /* vector of oak_module_t*, index = id */
  oak_container_t* by_canonical_path; /* path → usize module_id    */
};


OAK_API void oak_module_registry_init(oak_module_registry_t* reg,
                                     oak_allocator_t* allocator);
OAK_API void oak_module_registry_free(oak_module_registry_t* reg);

/* O(1) lookup. Returns null if no module with that id. */
OAK_API oak_module_t*
oak_module_registry_get(const oak_module_registry_t* reg, u16 module_id);

/* O(1) lookup by canonical path. Returns null if not present. */
OAK_API oak_module_t*
oak_module_registry_find_by_path(const oak_module_registry_t* reg,
                                 const char* canonical_path);

/* Allocate a module, append to registry, assign it a module_id. The strings
 * `canonical_path` and `dotted_name` are duplicated. */
OAK_API oak_module_t*
oak_module_registry_new(oak_module_registry_t* reg,
                           const char* canonical_path,
                           const char* dotted_name);

/* Look up any exported top-level symbol in the module's single namespace. */
OAK_API const oak_symbol_t* oak_module_find_export_symbol(
    const oak_module_t* mod, const char* name);

/* Look up a function export. Returns null if not found. */
OAK_API const oak_module_export_fn_t* oak_module_find_export_fn(
    const oak_module_t* mod, const char* name);

/* Look up a record export. Returns null if not found. */
OAK_API const oak_module_export_record_t* oak_module_find_export_record(
    const oak_module_t* mod, const char* name);

/* Look up an enum export. Returns null if not found. */
OAK_API const oak_module_export_enum_t* oak_module_find_export_enum(
    const oak_module_t* mod, const char* name);

/* Look up an interface export. Returns null if not found. */
OAK_API const oak_module_export_interface_t* oak_module_find_export_interface(
    const oak_module_t* mod, const char* name);
