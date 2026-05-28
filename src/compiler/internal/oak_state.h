#pragma once

#include "oak_defs.h"

struct oak_allocator_t;       /* defined in oak_allocator.h */
struct oak_compile_options_t; /* defined in oak_bind.h */
#include "oak_enum_registry.h"
#include "oak_fn_registry.h"
#include "oak_generic_registry.h"
#include "oak_method_table.h"
#include "oak_record_registry.h"
#include "oak_trait_registry.h"
#include "oak_htable.h"
#include "oak_module.h"
#include "oak_type.h"

/* ---------- Per-fn ephemeral compilation state ---------- */

struct oak_local_t
{
  const char* name;
  usize length;
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
  int break_count;
  int break_capacity;
  usize* continue_jumps;
  int continue_count;
  int continue_capacity;
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
 *   3. The program's own decls: `user_record_start`, `user_enum_start`,
 *      `user_trait_start` mark where each pass's source-level entries
 *      begin in the corresponding registry.  Anything before the cursor is
 *      native or imported; anything after is exported to the module.
 *
 * `types` is the type-id interner shared by all registries.  After
 * compilation it is moved into `current_module` via
 * oak_compiler_move_types_to_module().
 *
 * `generic_params` / `generic_param_count` are the *active* type-parameter
 * context — non-null only while lowering a generic fn / record body or
 * checking a generic call site.  They must be paired (save before, restore
 * after) by anyone who mutates them; see infer_generic_call_type for an
 * example. */

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
  struct oak_generic_registry_t generics;
  /* Active type parameter bindings during compilation of a generic body.
   * NULL + count=0 when outside a generic context. */
  struct oak_generic_param_t* generic_params;
  int generic_param_count;
  /* Names bound at module scope (top-level `let` items only). Used to reject
   * access from inside user function and method bodies. */
  struct oak_htable_t module_scope_names;
  /* Module-system context. Null when compiling standalone. */
  struct oak_module_registry_t* module_registry;
  struct oak_module_t* current_module;
  int allow_bodyless_fns;
  /* Index into c->records.entries where user-defined records begin (after
   * native and imported records).  Set just before register_program_records. */
  int user_record_start;
  /* Index into c->enums.variants where user-defined enum variants begin.
   * Set just before register_program_enums.  -1 means unset. */
  int user_enum_start;
  int user_trait_start;
  /* Compile options (borrowed; NULL when compiling standalone). Used by
   * attribute callback dispatch to look up named attribute bindings. */
  const struct oak_compile_options_t* opts;
  int anon_fn_count;
};
