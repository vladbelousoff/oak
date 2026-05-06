#pragma once

#include "oak_chunk.h"
#include "oak_diagnostic.h"
#include "oak_dynarr.h"
#include "oak_file_map.h"
#include "oak_htable.h"
#include "oak_parser.h"

/* Sentinel module_id used by native fns and the entry-only chunk before a
 * registry exists. */
#define OAK_MODULE_ID_NONE ((u16)0xFFFF)

/* ----- Per-module exports populated at end of compile ----- */

struct oak_module_export_fn_t
{
  const char* name; /* borrowed from the module's lexer arena */
  usize name_len;
  u16 const_idx;    /* index into the module's chunk constants */
  int arity;        /* user-visible arity (no implicit self for globals) */
  /* Borrowed pointer to the function's return-type AST node (or null when
   * the fn returns void).  Lives for the module's parser arena lifetime. */
  const struct oak_ast_node_t* return_type_node;
};

/* Maximum record fields mirrored here so oak_module.h is self-contained. */
#define OAK_MODULE_MAX_RECORD_FIELDS 32

struct oak_module_export_record_field_t
{
  const char* name;      /* borrowed from lexer arena */
  usize name_len;
  /* Type stored as a name so importing modules can re-intern it against their
   * own type registry.  For primitives this is "number"/"string"/"bool"; for
   * user-defined types it is borrowed from the source module's type registry
   * and lives for the module's lifetime. */
  const char* type_name;
  usize type_name_len;
};

struct oak_module_export_record_t
{
  const char* name;  /* borrowed from lexer arena */
  usize name_len;
  int field_count;
  struct oak_module_export_record_field_t fields[OAK_MODULE_MAX_RECORD_FIELDS];
  u16 layout_id; /* const-pool slot in this module's chunk (for new mod.T{}) */
};

/* One variant entry for an exported enum. */
struct oak_module_export_enum_variant_t
{
  const char* name;      /* borrowed from lexer arena */
  usize name_len;
  int value;             /* ordinal (0, 1, 2, …) */
};

#define OAK_MODULE_MAX_ENUM_VARIANTS 64

struct oak_module_export_enum_t
{
  const char* name;  /* enum type name, borrowed from lexer arena */
  usize name_len;
  int variant_count;
  struct oak_module_export_enum_variant_t variants[OAK_MODULE_MAX_ENUM_VARIANTS];
};

/* ----- Concrete dynamic-array types used by the module system ----- */

struct oak_u16_vec_t         { u16*                               items; int count; int capacity; };
struct oak_export_fn_vec_t   { struct oak_module_export_fn_t*     items; int count; int capacity; };
struct oak_export_rec_vec_t  { struct oak_module_export_record_t* items; int count; int capacity; };
struct oak_export_enum_vec_t { struct oak_module_export_enum_t*   items; int count; int capacity; };

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
  struct oak_u16_vec_t import_modules;          /* module_ids of direct deps */

  /* Exports (populated post-compile) */
  struct oak_htable_t exports_fn_by_name;
  struct oak_export_fn_vec_t exports_fn;
  struct oak_htable_t exports_record_by_name;
  struct oak_export_rec_vec_t exports_record;
  struct oak_htable_t exports_enum_by_name;
  struct oak_export_enum_vec_t exports_enum;

  /* Lifecycle */
  enum
  {
    OAK_MOD_PARSED,
    OAK_MOD_COMPILED,
  } state;
};

/* ----- Registry ----- */

struct oak_module_ptr_vec_t { struct oak_module_t** items; int count; int capacity; };

struct oak_module_registry_t
{
  struct oak_module_ptr_vec_t modules;          /* index = module_id */
  struct oak_htable_t by_canonical_path;    /* path -> module_id */
};

/* ----- Lifecycle ----- */

void oak_module_registry_init(struct oak_module_registry_t* reg);
void oak_module_registry_free(struct oak_module_registry_t* reg);

/* O(1) lookup. Returns null if no module with that id. */
struct oak_module_t*
oak_module_registry_get(const struct oak_module_registry_t* reg, u16 module_id);

/* O(1) lookup by canonical path. Returns null if not present. */
struct oak_module_t* oak_module_registry_find_by_path(
    const struct oak_module_registry_t* reg, const char* canonical_path);

/* Allocate a module, append to registry, assign it a module_id. The strings
 * `canonical_path` and `dotted_name` are duplicated. */
struct oak_module_t*
oak_module_registry_create(struct oak_module_registry_t* reg,
                           const char* canonical_path,
                           const char* dotted_name);

/* Look up a function export. Returns null if not found. */
const struct oak_module_export_fn_t*
oak_module_find_export_fn(const struct oak_module_t* mod,
                          const char* name,
                          usize name_len);

/* Look up a record export. Returns null if not found. */
const struct oak_module_export_record_t*
oak_module_find_export_record(const struct oak_module_t* mod,
                              const char* name,
                              usize name_len);

/* Look up an enum export. Returns null if not found. */
const struct oak_module_export_enum_t*
oak_module_find_export_enum(const struct oak_module_t* mod,
                            const char* name,
                            usize name_len);
