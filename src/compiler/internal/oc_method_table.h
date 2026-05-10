#pragma once

#include "oc_defs.h"
#include "oak_type.h"
#include "oak_value.h"

struct oak_compiler_t;

/* Optional compile-time argument validator for a method binding. Receives the
 * full call AST node (children are: callee, then user args), the inferred
 * receiver type, and a fallback token to attribute errors to when an arg has
 * no token of its own. */
typedef void (*oak_method_validate_args_fn)(struct oak_compiler_t* c,
                                            const struct oak_ast_node_t* call,
                                            struct oak_type_t recv_ty,
                                            const struct oak_token_t* err_tok);

/* Static description of a method bound to a receiver type (e.g. arrays).
 * Methods are not exposed as global fns; they're only reachable
 * through `receiver.name(...)` syntax. */
struct oak_method_binding_t
{
  const char* name;
  usize name_len;
  u16 const_idx;
  /* Includes the implicit receiver. So `arr.push(x)` -> arity 2. */
  int total_arity;
  /* Compile-time return type of this method (always a built-in id). */
  oak_type_id_t return_type_id;
  /* Optional. Called after arity is verified, before bytecode is emitted. */
  oak_method_validate_args_fn validate_args;
};

/* Array, map, and string built-in method tables.  These are fixed,
 * fully-static sets registered once at startup, so plain fixed arrays suffice.
 */
struct oak_builtin_methods_t
{
  struct oak_method_binding_t array[OAK_MAX_ARRAY_METHODS];
  int array_count;
  struct oak_method_binding_t map[OAK_MAX_MAP_METHODS];
  int map_count;
  struct oak_method_binding_t string[OAK_MAX_STRING_METHODS];
  int string_count;
  struct oak_method_binding_t bool_[OAK_MAX_BOOL_METHODS];
  int bool_count;
  struct oak_method_binding_t number[OAK_MAX_NUMBER_METHODS];
  int number_count;
  struct oak_method_binding_t record[OAK_MAX_RECORD_BUILTIN_METHODS];
  int record_count;
};

/* ---------- Static table row for a built-in receiver method ---------- */

struct oak_builtin_method_def_t
{
  const char* name;
  oak_native_fn_t impl;
  /* Total arity, including the implicit receiver. */
  int total_arity;
  oak_type_id_t return_type_id;
  oak_method_validate_args_fn validate_args;
};

struct oak_native_binding_t
{
  const char* name;
  oak_native_fn_t impl;
  int arity;
  oak_type_id_t return_type_id;
};
