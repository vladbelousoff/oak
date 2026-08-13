#pragma once

#include "oak_defs.h"

typedef struct oak_allocator oak_allocator_t; /* defined in oak_allocator.h */
typedef struct oak_compile_options oak_compile_options_t; /* in oak_bind.h */
#include "oak_container.h"
#include "oak_enum_registry.h"
#include "oak_fn_registry.h"
#include "oak_hash_set.h"
#include "oak_method_table.h"
#include "oak_record_registry.h"
#include "oak_interface_registry.h"
#include "oak_module_impl.h"
#include "oak_symbol.h"
#include "oak_type.h"


typedef struct oak_local oak_local_t;
struct oak_local
{
  const char* name;
  int slot;
  int is_mutable;
  int depth;
  oak_type_t type;
};

typedef struct oak_loop_frame oak_loop_frame_t;
struct oak_loop_frame
{
  oak_loop_frame_t* enclosing;
  usize loop_start;
  int exit_depth;
  int continue_depth;
  oak_container_t* break_jumps;    /* vector of usize */
  oak_container_t* continue_jumps; /* vector of usize */
};

/* State that is reset for every fn body being compiled. */
typedef struct oak_scope_ctx oak_scope_ctx_t;
struct oak_scope_ctx
{
  oak_local_t locals[OAK_MAX_LOCALS];
  int local_count;
  int scope_depth;
  int stack_depth;
  /* Return type of the fn being compiled: omitted `->` is void
   * (OAK_TYPE_VOID). Cleared to unknown between fns. */
  oak_type_t declared_return_type;
  oak_loop_frame_t* current_loop;
  int fn_depth;
};

/*
 * Top-level compiler state.
 *
 * `oak_compiler_t` owns every registry needed to lower an AST into a chunk.
 * Registries are populated in passes (see oak_compiler_pipeline.c):
 *
 *   1. Native bindings from `opts` (records → `records`, enums → `enums`,
 *      free fns / methods → `fns` and `records.*.methods`).
 *   2. Imports from other modules (resolve_new_style_imports), which extend
 *      `records`, `enums`, `interfaces`, and `fns` with translated entries.
 *   3. The program's own declarations are marked as exported symbols.
 *
 * `types` catalogs type names and module-qualified IDs shared by all
 * registries. After
 * compilation it is moved into `current_module` via
 * oak_compiler_move_types_to_module(). */

typedef struct oak_compiler oak_compiler_t;
struct oak_compiler
{
  oak_allocator_t* allocator;
  oak_chunk_t* chunk;
  oak_compile_result_t* result; /* errors written directly here */
  int has_error;
  oak_scope_ctx_t scope;
  oak_fn_registry_t fns;
  oak_type_registry_t types;
  oak_builtin_methods_t builtin_methods;
  oak_record_registry_t records;
  oak_enum_registry_t enums;
  oak_interface_registry_t interfaces;
  /* Authoritative namespace for all top-level declarations visible while
   * compiling this module. Typed registries own declaration metadata. */
  oak_symbol_registry_t symbols;
  /* Names bound at module scope (top-level `let` items only). Used to reject
   * access from inside user function and method bodies. */
  oak_container_t* module_scope_names; /* set of names */
  /* Module-system context. Null when compiling standalone. */
  oak_module_registry_t* module_registry;
  oak_module_t* current_module;
  int allow_bodyless_fns;
  /* Compile options (borrowed; NULL when compiling standalone). Used by
   * attribute callback dispatch to look up named attribute bindings. */
  const oak_compile_options_t* opts;
  int anon_fn_count;
  /* Cursors into opts->native_types / native_global_fns / native_fns marking
   * how many native bindings have already been registered into the compiler.
   * Native registration is incremental: it resumes from these cursors so it
   * can run a second time after record-decl attribute callbacks bind new
   * native types/methods (e.g. component views) without re-processing — and
   * erroring on — entries registered in the first pass. */
  int native_types_cursor;
  /* How many of opts->bind_errors have been reported (see
   * oak_compiler_report_bind_errors): attribute callbacks bind more
   * mid-compile, and each message must surface exactly once. */
  int bind_errors_cursor;
  int native_global_fns_cursor;
  int native_fns_cursor;
  /* Strong-ownership reachability over record registry entries, computed by
   * oak_compiler_check_cycles: cycle_reach[i * n + j] != 0 iff record i can
   * reach record j through strong fields (reflexive; n = cycle_reach_count).
   */
  u8* cycle_reach;
  int cycle_reach_count;
};
