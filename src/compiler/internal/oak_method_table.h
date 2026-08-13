#pragma once

#include "oak_defs.h"
#include "oak_type.h"
#include "oak_value_impl.h"

typedef struct oak_compiler oak_compiler_t;

/* Optional compile-time argument validator for a method binding. Receives the
 * full call AST node (children are: callee, then user args), the inferred
 * receiver type, and a fallback token to attribute errors to when an arg has
 * no token of its own. */
typedef void (*oak_method_validate_args_fn)(oak_compiler_t* c,
                                            const oak_ast_node_t* call,
                                            oak_type_t recv_ty,
                                            const oak_token_t* err_tok);

/* Static description of a method bound to a receiver type (e.g. arrays).
 * Methods are not exposed as global fns; they're only reachable
 * through `receiver.name(...)` syntax. */
typedef struct oak_method_binding oak_method_binding_t;
struct oak_method_binding
{
  const char* name;
  u16 const_idx;
  /* Includes the implicit receiver. So `arr.push(x)` -> arity 2. */
  int total_arity;
  /* Compile-time return type of this method (always a built-in id). */
  oak_type_id_t return_type_id;
  int mutates_receiver;
  /* Optional. Called after arity is verified, before bytecode is emitted. */
  oak_method_validate_args_fn validate_args;
};

/* Array, map, and string built-in method tables.  These are fixed,
 * fully-static sets registered once at startup, so plain fixed arrays suffice.
 */
typedef struct oak_builtin_methods oak_builtin_methods_t;
struct oak_builtin_methods
{
  oak_method_binding_t array[OAK_MAX_ARRAY_METHODS];
  int array_count;
  oak_method_binding_t map[OAK_MAX_MAP_METHODS];
  int map_count;
  oak_method_binding_t string[OAK_MAX_STRING_METHODS];
  int string_count;
  oak_method_binding_t bool_[OAK_MAX_BOOL_METHODS];
  int bool_count;
  oak_method_binding_t number[OAK_MAX_NUMBER_METHODS];
  int number_count;
  oak_method_binding_t record[OAK_MAX_RECORD_BUILTIN_METHODS];
  int record_count;
};


typedef struct oak_builtin_method_def oak_builtin_method_def_t;
struct oak_builtin_method_def
{
  const char* name;
  oak_native_fn_t impl;
  /* Total arity, including the implicit receiver. */
  int total_arity;
  oak_type_id_t return_type_id;
  int mutates_receiver;
  oak_method_validate_args_fn validate_args;
};

typedef struct oak_native_binding oak_native_binding_t;
struct oak_native_binding
{
  const char* name;
  oak_native_fn_t impl;
  int arity;
  oak_type_id_t return_type_id;
};
