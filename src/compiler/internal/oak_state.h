#pragma once

#include "oak_defs.h"

struct oak_allocator_t;       /* defined in oak_allocator.h */
struct oak_compile_options_t; /* defined in oak_bind.h */
#include "oak_enum_registry.h"
#include "oak_fn_registry.h"
#include "oak_method_table.h"
#include "oak_record_registry.h"
#include "oak_trait_registry.h"
#include "oak_htable.h"
#include "oak_module.h"
#include "oak_symbol.h"
#include "oak_type.h"

/* ---------- Per-fn ephemeral compilation state ---------- */

struct oak_local_t
{
  const char* name;
  int slot;
  int is_mutable;
  int depth;
  struct oak_type_t type;
};

struct oak_loop_frame_t
{
  struct oak_loop_frame_t* enclosing;
  usize loop_start;
  int exit_depth;
  int continue_depth;
  usize* break_jumps;
  usize* continue_jumps;
};

/* State that is reset for every fn body being compiled. */
struct oak_scope_ctx_t
{
  struct oak_local_t locals[OAK_MAX_LOCALS];
  int local_count;
  int scope_depth;
  int stack_depth;
  /* Return type of the fn being compiled: omitted `->` is void
   * (OAK_TYPE_VOID). Cleared to unknown between fns. */
  struct oak_type_t declared_return_type;
  struct oak_loop_frame_t* current_loop;
  int fn_depth;
};

/* ---------- Top-level compiler state ----------
 *
 * `oak_compiler_t` owns every registry needed to lower an AST into a chunk.
 * Registries are populated in passes (see oak_compiler_pipeline.c):
 *
 *   1. Native bindings from `opts` (records → `records`, enums → `enums`,
 *      free fns / methods → `fns` and `records.*.methods`).
 *   2. Imports from other modules (resolve_new_style_imports), which extend
 *      `records`, `enums`, `traits`, and `fns` with translated entries.
 *   3. The program's own declarations are marked as exported symbols.
 *
 * `types` catalogs type names and module-qualified IDs shared by all
 * registries. After
 * compilation it is moved into `current_module` via
 * oak_compiler_move_types_to_module(). */

struct oak_compiler_t
{
  struct oak_allocator_t* allocator;
  struct oak_chunk_t* chunk;
  struct oak_compile_result_t* result; /* errors written directly here */
  int has_error;
  struct oak_scope_ctx_t scope;
  struct oak_fn_registry_t fns;
  struct oak_type_registry_t types;
  struct oak_builtin_methods_t builtin_methods;
  struct oak_record_registry_t records;
  struct oak_enum_registry_t enums;
  struct oak_trait_registry_t traits;
  /* Authoritative namespace for all top-level declarations visible while
   * compiling this module. Typed registries own declaration metadata. */
  struct oak_symbol_registry_t symbols;
  /* Names bound at module scope (top-level `let` items only). Used to reject
   * access from inside user function and method bodies. */
  struct oak_htable_t module_scope_names;
  /* Module-system context. Null when compiling standalone. */
  struct oak_module_registry_t* module_registry;
  struct oak_module_t* current_module;
  int allow_bodyless_fns;
  /* Compile options (borrowed; NULL when compiling standalone). Used by
   * attribute callback dispatch to look up named attribute bindings. */
  const struct oak_compile_options_t* opts;
  int anon_fn_count;
  /* Cursors into opts->native_types / native_global_fns / native_fns marking
   * how many native bindings have already been registered into the compiler.
   * Native registration is incremental: it resumes from these cursors so it
   * can run a second time after record-decl attribute callbacks bind new
   * native types/methods (e.g. component views) without re-processing — and
   * erroring on — entries registered in the first pass. */
  int native_types_cursor;
  int native_global_fns_cursor;
  int native_fns_cursor;
};
