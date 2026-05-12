#pragma once

#include "oakc_defs.h"

struct oak_compile_options_t; /* defined in oak_bind.h */
#include "oakc_enum_registry.h"
#include "oakc_fn_registry.h"
#include "oakc_method_table.h"
#include "oakc_record_registry.h"
#include "oakc_trait_registry.h"
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
  /* Borrow state.
   *  alive = 0 means this binding has been moved out (e.g. `let mut y = x`
   *  where x was exclusive). Any further read/write is rejected.
   *  frozen_by_slot >= 0 means this exclusive binding is currently being
   *  shared-reborrowed by the local at that slot. Reads are still allowed
   *  (the freeze is read-only); writes are rejected. The freeze is released
   *  when the freezing local goes out of scope. */
  int alive;
  int frozen_by_slot;
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

/* ---------- Top-level compiler state ---------- */

struct oak_compiler_t
{
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
  /* Compile options (borrowed; NULL when compiling standalone). Used by
   * attribute callback dispatch to look up named attribute bindings. */
  const struct oak_compile_options_t* opts;
};
