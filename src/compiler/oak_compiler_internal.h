#pragma once

#include "oak_compiler.h"
#include "oak_count_of.h"
#include "oak_str.h"
#include "oak_log.h"
#include "oak_mem.h"
#include "oak_type.h"
#include "oak_value.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define OAK_MAX_LOCALS   256
#define OAK_MAX_USER_FNS 64
/* Max recorded forward jumps (break or continue) per loop. */
#define OAK_MAX_LOOP_BRANCHES 64
/* Max total enum variant entries across all enum declarations. */
#define OAK_MAX_ENUM_VARIANTS 128

#define OAK_LOC_SYNTHETIC ((struct oak_code_loc_t){ .line = 0, .column = 1 })

#define OAK_MAX_ARRAY_METHODS  8
#define OAK_MAX_MAP_METHODS    8
#define OAK_MAX_STRUCTS        32
#define OAK_MAX_STRUCT_FIELDS  32
#define OAK_MAX_STRUCT_METHODS 16

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
  usize break_jumps[OAK_MAX_LOOP_BRANCHES];
  int break_count;
  usize continue_jumps[OAK_MAX_LOOP_BRANCHES];
  int continue_count;
};

/* decl is null for native (C) builtins registered at compile time. */
struct oak_registered_fn_t
{
  const char* name;
  usize name_len;
  u16 const_idx;
  int arity_min;
  int arity_max;
  const struct oak_ast_node_t* decl;
};

struct oak_native_binding_t
{
  const char* name;
  oak_native_fn_t impl;
  int arity_min;
  int arity_max;
};

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
 * Methods are not exposed as global functions; they're only reachable
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

struct oak_struct_field_t
{
  /* Borrowed pointer into the lexer arena (lives for the compilation). */
  const char* name;
  usize name_len;
  struct oak_type_t type;
};

/* User-defined method bound to a struct type. The method's compiled body is
 * stored in the chunk's constants table at `const_idx`; it is invoked like a
 * regular function with the receiver passed as the first argument (slot 0
 * inside the body, accessible as `self`). */
struct oak_struct_method_t
{
  const char* name;
  usize name_len;
  u16 const_idx;
  /* Total arity including the implicit `self` receiver. */
  int arity;
  const struct oak_ast_node_t* decl;
};

struct oak_registered_struct_t
{
  const char* name;
  usize name_len;
  oak_type_id_t type_id;
  int field_count;
  struct oak_struct_field_t fields[OAK_MAX_STRUCT_FIELDS];
  int method_count;
  struct oak_struct_method_t methods[OAK_MAX_STRUCT_METHODS];
};

/* A single variant of a user-defined enum, lowered to a named integer
 * constant in the chunk's constant pool. */
struct oak_enum_variant_t
{
  /* Borrowed pointer into the lexer arena (lives for the compilation). */
  const char* name;
  usize name_len;
  u16 const_idx;
  int value;
};

struct oak_compiler_t
{
  struct oak_chunk_t* chunk;
  struct oak_local_t locals[OAK_MAX_LOCALS];
  int local_count;
  int scope_depth;
  int stack_depth;
  int has_error;
  /* Total errors accumulated across all recovered statement boundaries. */
  int error_count;
  /* Return type of the function being compiled: omitted `->` is void
   * (OAK_TYPE_VOID). Cleared to unknown between functions. */
  struct oak_type_t declared_return_type;
  struct oak_loop_frame_t* current_loop;
  int function_depth;
  struct oak_registered_fn_t fn_registry[OAK_MAX_USER_FNS];
  int fn_registry_count;
  struct oak_type_registry_t type_registry;
  struct oak_method_binding_t array_methods[OAK_MAX_ARRAY_METHODS];
  int array_method_count;
  struct oak_method_binding_t map_methods[OAK_MAX_MAP_METHODS];
  int map_method_count;
  struct oak_registered_struct_t structs[OAK_MAX_STRUCTS];
  int struct_count;
  struct oak_enum_variant_t enum_variants[OAK_MAX_ENUM_VARIANTS];
  int enum_variant_count;
};

/* Static table row for a built-in receiver method (array or map). */
struct oak_builtin_method_def_t
{
  const char* name;
  oak_native_fn_t impl;
  /* Total arity, including the implicit receiver. */
  int total_arity;
  oak_type_id_t return_type_id;
  oak_method_validate_args_fn validate_args;
};

/* ---------- oak_compiler_error.c ---------- */

struct oak_code_loc_t oak_compiler_loc_from_token(const struct oak_token_t* t);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
void oak_compiler_error_at(struct oak_compiler_t* c,
                           const struct oak_token_t* token,
                           const char* fmt,
                           ...);

/* ---------- oak_compiler_emit.c ---------- */

void oak_compiler_emit_byte(const struct oak_compiler_t* c,
                            u8 byte,
                            struct oak_code_loc_t loc);

void oak_compiler_emit_op(struct oak_compiler_t* c,
                          u8 op,
                          struct oak_code_loc_t loc);

void oak_compiler_emit_op_arg(struct oak_compiler_t* c,
                              u8 op,
                              u8 arg,
                              struct oak_code_loc_t loc);

u16 oak_compiler_intern_constant(struct oak_compiler_t* c,
                                 struct oak_value_t value);

void oak_compiler_emit_constant(struct oak_compiler_t* c,
                                u16 idx,
                                struct oak_code_loc_t loc);

usize oak_compiler_emit_jump(struct oak_compiler_t* c,
                             u8 op,
                             struct oak_code_loc_t loc);

void oak_compiler_patch_jump(struct oak_compiler_t* c, usize offset);

void oak_compiler_patch_jumps(struct oak_compiler_t* c,
                              const usize* jumps,
                              int count);

void oak_compiler_emit_loop(struct oak_compiler_t* c,
                            usize loop_start,
                            struct oak_code_loc_t loc);

void oak_compiler_emit_pops(struct oak_compiler_t* c,
                            int count,
                            struct oak_code_loc_t loc);

void oak_compiler_emit_loop_control_jump(struct oak_compiler_t* c,
                                         usize* jumps,
                                         int* count,
                                         int target_depth,
                                         const char* keyword);

/* ---------- oak_compiler_scope.c ---------- */

int oak_compiler_find_local(const struct oak_compiler_t* c,
                            const char* name,
                            usize length,
                            int* out_is_mutable);

void oak_compiler_add_local(struct oak_compiler_t* c,
                            const char* name,
                            usize length,
                            int slot,
                            int is_mutable,
                            struct oak_type_t type);

void oak_compiler_begin_scope(struct oak_compiler_t* c);

void oak_compiler_end_scope(struct oak_compiler_t* c);

int oak_compiler_compile_assign_target(struct oak_compiler_t* c,
                                       const struct oak_ast_node_t* lhs,
                                       const char* non_ident_msg);

int oak_compiler_expr_is_mutable_place(const struct oak_compiler_t* c,
                                        const struct oak_ast_node_t* expr);

/* ---------- oak_compiler_types.c ---------- */

void oak_compiler_type_node_to_type(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* type_node,
                                    struct oak_type_t* out);

/* Fails compilation if the expression is typed as void (e.g. call to a void
 * function). No-op for null or not-yet-inferrable types. */
void oak_compiler_reject_void_value_expr(struct oak_compiler_t* c,
                                        const struct oak_ast_node_t* expr);

oak_type_id_t oak_compiler_intern_type_token(struct oak_compiler_t* c,
                                             const struct oak_token_t* token);

int oak_compiler_local_type_get(struct oak_compiler_t* c,
                                const char* name,
                                usize len,
                                struct oak_type_t* out);

void oak_compiler_infer_expr_static_type(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* expr,
                                         struct oak_type_t* out);

const char* oak_compiler_type_kind_name(struct oak_compiler_t* c,
                                        struct oak_type_t t);

const char* oak_compiler_type_full_name(struct oak_compiler_t* c,
                                        struct oak_type_t t);

/* ---------- oak_compiler_builtins.c ---------- */

u16 oak_compiler_intern_native_constant(struct oak_compiler_t* c,
                                        oak_native_fn_t impl,
                                        int arity_min,
                                        int arity_max,
                                        const char* name);

void oak_compiler_register_native_builtins(struct oak_compiler_t* c);

void oak_compiler_register_array_methods(struct oak_compiler_t* c);

void oak_compiler_register_map_methods(struct oak_compiler_t* c);

const struct oak_method_binding_t*
oak_compiler_find_array_method(struct oak_compiler_t* c,
                               const char* name,
                               usize len);

const struct oak_method_binding_t*
oak_compiler_find_map_method(struct oak_compiler_t* c,
                             const char* name,
                             usize len);

/* ---------- oak_compiler_enums.c ---------- */

void oak_compiler_register_program_enums(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* prog);

const struct oak_enum_variant_t*
oak_compiler_find_enum_variant(const struct oak_compiler_t* c,
                               const char* name,
                               usize len);

/* ---------- oak_compiler_structs.c ---------- */

const struct oak_registered_struct_t*
oak_compiler_find_struct_by_name(const struct oak_compiler_t* c,
                                 const char* name,
                                 usize len);

const struct oak_registered_struct_t*
oak_compiler_find_struct_by_type_id(const struct oak_compiler_t* c,
                                    oak_type_id_t type_id);

int oak_compiler_find_struct_field(const struct oak_registered_struct_t* s,
                                   const char* name,
                                   usize len);

/* If `recv_ty` is a known struct, sets `*out_sd` and returns the field index.
 * Returns -1 if the type is not a struct, or the field name is not found
 * (in the latter case `*out_sd` is still the matching struct). */
int oak_compiler_struct_field_index(
    const struct oak_compiler_t* c,
    struct oak_type_t recv_ty,
    const char* field_name,
    usize field_len,
    const struct oak_registered_struct_t** out_sd);

/* Resolves a member for codegen; emits errors and returns -1 on failure. */
int oak_compiler_require_struct_field(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* recv,
    const struct oak_ast_node_t* fname_ident,
    int is_assignment,
    const struct oak_registered_struct_t** out_sd);

void oak_compiler_register_program_structs(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* prog);

/* ---------- oak_compiler_functions.c ---------- */

int oak_compiler_fn_decl_has_receiver(const struct oak_ast_node_t* decl);

const struct oak_ast_node_t*
oak_compiler_fn_decl_param_list(const struct oak_ast_node_t* decl);

const struct oak_ast_node_t*
oak_compiler_fn_decl_name_node(const struct oak_ast_node_t* decl);

const struct oak_ast_node_t*
oak_compiler_fn_decl_self_param(const struct oak_ast_node_t* decl);

int oak_compiler_fn_param_self_is_mutable(const struct oak_ast_node_t* sp);

const struct oak_ast_node_t*
oak_compiler_fn_decl_block(const struct oak_ast_node_t* decl);

int oak_compiler_fn_param_is_mutable(const struct oak_ast_node_t* param);

const struct oak_ast_node_t*
oak_compiler_fn_param_ident(const struct oak_ast_node_t* param);

const struct oak_ast_node_t*
oak_compiler_fn_param_type_node(const struct oak_ast_node_t* param);

const struct oak_ast_node_t*
oak_compiler_fn_decl_param_at(const struct oak_ast_node_t* decl, int index);

const struct oak_ast_node_t*
oak_compiler_fn_decl_return_type_node(const struct oak_ast_node_t* decl);

int oak_compiler_count_fn_params(const struct oak_ast_node_t* decl);

void oak_compiler_register_program_functions(struct oak_compiler_t* c,
                                             const struct oak_ast_node_t* prog);

void oak_compiler_register_program_methods(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* prog);

const struct oak_registered_fn_t*
oak_compiler_find_registered_fn_entry(struct oak_compiler_t* c,
                                      const char* name,
                                      usize len);

void oak_compiler_compile_stmt_return(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* node);

void oak_compiler_compile_function_bodies(struct oak_compiler_t* c);

void oak_compiler_compile_method_bodies(struct oak_compiler_t* c);

void oak_compiler_validate_user_fn_call_arg_types(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* fn);

void oak_compiler_validate_struct_method_call_arg_types(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_struct_method_t* m);

/* ---------- oak_compiler_stmt.c ---------- */

void oak_compiler_compile_block(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* block);

void oak_compiler_compile_stmt_if(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* node);

void oak_compiler_compile_stmt_while(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* node);

void oak_compiler_compile_stmt_for_from(struct oak_compiler_t* c,
                                        const struct oak_ast_node_t* node);

void oak_compiler_compile_stmt_for_in(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* node);

/* ---------- oak_compiler_calls.c ---------- */

const struct oak_ast_node_t*
oak_compiler_fn_call_arg_expr_at(const struct oak_ast_node_t* call,
                                 usize index);

void oak_compiler_compile_fn_call(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* node);

void oak_compiler_compile_method_call(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* node,
                                      const struct oak_ast_node_t* callee);

void oak_compiler_compile_fn_call_arg(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* arg);

/* ---------- oak_compiler_expr.c ---------- */

usize oak_compiler_ast_child_count(const struct oak_ast_node_t* node);

int oak_compiler_ast_is_int_literal(const struct oak_ast_node_t* node,
                                    int value);

u8 oak_compiler_opcode_for_node_kind(enum oak_node_kind_t kind);

void oak_compiler_compile_node(struct oak_compiler_t* c,
                               const struct oak_ast_node_t* node);
