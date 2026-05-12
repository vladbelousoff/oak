#pragma once

#include "oak_chunk.h"
#include "oak_diagnostic.h"
#include "oak_dynarr.h"
#include "oak_file_map.h"
#include "oak_htable.h"
#include "oak_parser.h"
#include "oak_type.h"

/* Sentinel module_id used by native fns and the entry-only chunk before a
 * registry exists. */
#define OAK_MODULE_ID_NONE ((u16)0xFFFF)

/* ----- Per-module exports populated at end of compile ----- */

struct oak_module_export_fn_t
{
  const char* name; /* borrowed from the module's lexer arena */
  usize name_len;
  u16 const_idx; /* index into the module's chunk constants */
  int arity;     /* user-visible arity (no implicit self for globals) */
  /* Borrowed pointer to the function's return-type AST node (or null when
   * the fn returns void).  Lives for the module's parser arena lifetime. */
  const struct oak_ast_node_t* return_type_node;
  oak_type_id_t return_type_id;
  enum oak_type_kind_t return_kind;
  /* Heap-allocated pointer array; each element points into the module's lexer
   * arena.  Populated for bodyless stubs so apply_native_module_function_exports
   * can carry attributes onto the replacement native function object.
   * NULL when attr_count == 0.  Freed by oak_module_free. */
  const char** stub_attrs;
  int stub_attr_count;
};

struct oak_module_export_record_field_t
{
  const char* name; /* borrowed from lexer arena */
  usize name_len;
  /* Type stored as a name so importing modules can re-intern it against their
   * own type registry.  For primitives this is "number"/"string"/"bool"; for
   * user-defined types it is borrowed from the source module's type registry
   * and lives for the module's lifetime. */
  const char* type_name;
  usize type_name_len;
};

/* Per-method metadata for bodyless native stub methods that carry attributes.
 * Populated by populate_module_exports; consulted by oakc_register_native_fns
 * in calling modules to wire runtime attribute hooks onto native fn objects. */
struct oak_module_export_record_method_t
{
  const char* name;        /* borrowed from lexer arena */
  usize name_len;
  const char** stub_attrs; /* heap-allocated array; elements from lexer arena */
  int stub_attr_count;
};

struct oak_module_export_record_t
{
  const char* name; /* borrowed from lexer arena */
  usize name_len;
  struct oak_module_export_record_field_t* fields;
  int field_count;
  int field_capacity;
  u16 layout_id; /* const-pool slot in this module's chunk (for new mod.T{}) */
  struct oak_module_export_record_method_t* methods;
  int method_count;
  int method_capacity;
};

/* One variant entry for an exported enum. */
struct oak_module_export_enum_variant_t
{
  const char* name; /* borrowed from lexer arena */
  usize name_len;
  int value; /* ordinal (0, 1, 2, …) */
};

struct oak_module_export_enum_t
{
  const char* name; /* enum type name, borrowed from lexer arena */
  usize name_len;
  struct oak_module_export_enum_variant_t* variants;
  int variant_count;
  int variant_capacity;
};

/* ----- Dynamic-array type for module-id lists ----- */

struct oak_u16_vec_t
{
  u16* items;
  int count;
  int capacity;
};

/* ----- Export tables: each bundles a name→index htable with its typed array */

struct oak_fn_export_table_t
{
  struct oak_htable_t by_name;
  struct oak_module_export_fn_t* items;
  int count;
  int capacity;
};

struct oak_rec_export_table_t
{
  struct oak_htable_t by_name;
  struct oak_module_export_record_t* items;
  int count;
  int capacity;
};

struct oak_enum_export_table_t
{
  struct oak_htable_t by_name;
  struct oak_module_export_enum_t* items;
  int count;
  int capacity;
};

/* ----- Module ----- */

struct oak_module_t
{
  /* Identity */
  char* canonical_path; /* owned; null-terminated; hash-table key */
  char* dotted_name;    /* owned; e.g. "a.b.c"; for diagnostics only */
  u16 module_id;        /* dense index into registry */
  int is_entry;         /* 1 if this is the program entry module */

  /* Source + parsing artefacts (owned) */
  struct oak_file_map_t source;
  struct oak_lexer_result_t* lexer;
  struct oak_parser_result_t parser;

  /* Bytecode (owned; null until compile succeeds) */
  struct oak_chunk_t* chunk;

  /* Resolved imports (alias_name -> dependency module_id) */
  struct oak_htable_t imports;
  struct oak_u16_vec_t import_modules; /* module_ids of direct deps */

  /* Exports (populated post-compile) */
  struct oak_fn_export_table_t exports_fn;
  struct oak_rec_export_table_t exports_record;
  struct oak_enum_export_table_t exports_enum;

  /* Lifecycle */
  enum
  {
    OAK_MOD_PARSED,
    OAK_MOD_COMPILED,
  } state;
};

/* ----- Registry ----- */

struct oak_module_ptr_vec_t
{
  struct oak_module_t** items;
  int count;
  int capacity;
};

struct oak_module_registry_t
{
  struct oak_module_ptr_vec_t modules;   /* index = module_id */
  struct oak_htable_t by_canonical_path; /* path -> module_id */
};

/* ----- Lifecycle ----- */

void oak_module_registry_init(struct oak_module_registry_t* reg);
void oak_module_registry_free(struct oak_module_registry_t* reg);

/* O(1) lookup. Returns null if no module with that id. */
struct oak_module_t*
oak_module_registry_get(const struct oak_module_registry_t* reg, u16 module_id);

/* O(1) lookup by canonical path. Returns null if not present. */
struct oak_module_t*
oak_module_registry_find_by_path(const struct oak_module_registry_t* reg,
                                 const char* canonical_path);

/* Allocate a module, append to registry, assign it a module_id. The strings
 * `canonical_path` and `dotted_name` are duplicated. */
struct oak_module_t*
oak_module_registry_create(struct oak_module_registry_t* reg,
                           const char* canonical_path,
                           const char* dotted_name);

/* Look up a function export. Returns null if not found. */
const struct oak_module_export_fn_t* oak_module_find_export_fn(
    const struct oak_module_t* mod, const char* name, usize name_len);

/* Look up a record export. Returns null if not found. */
const struct oak_module_export_record_t* oak_module_find_export_record(
    const struct oak_module_t* mod, const char* name, usize name_len);

/* Look up an enum export. Returns null if not found. */
const struct oak_module_export_enum_t* oak_module_find_export_enum(
    const struct oak_module_t* mod, const char* name, usize name_len);
