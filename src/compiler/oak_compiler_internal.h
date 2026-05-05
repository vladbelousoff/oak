#pragma once

#include "oak_bind.h"
#include "oak_chunk.h"
#include "oak_compiler.h"
#include "oak_count_of.h"
#include "oak_dynarr.h"
#include "oak_hash_table.h"
#include "oak_log.h"
#include "oak_mem.h"
#include "oak_module.h"
#include "oak_str.h"
#include "oak_type.h"
#include "oak_value.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define OAK_MAX_LOCALS 256
/* Max recorded forward jumps (break or continue) per loop. */
#define OAK_MAX_LOOP_BRANCHES 64

#define OAK_LOC_SYNTHETIC ((struct oak_code_loc_t){ .line = 0, .column = 1 })

/* Bail out of the current (void) function on the first compilation error. */
#define CHECK_ERROR(c) do { if ((c)->has_error) return; } while (0)

#define OAK_MAX_ARRAY_METHODS          8
#define OAK_MAX_MAP_METHODS            8
#define OAK_MAX_STRING_METHODS         8
#define OAK_MAX_BOOL_METHODS           4
#define OAK_MAX_NUMBER_METHODS         4
#define OAK_MAX_RECORD_BUILTIN_METHODS 4
#define OAK_MAX_RECORD_FIELDS          32

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
  usize break_jumps[OAK_MAX_LOOP_BRANCHES];
  int break_count;
  usize continue_jumps[OAK_MAX_LOOP_BRANCHES];
  int continue_count;
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

/* ---------- Fn registry ---------- */

/* decl is null for native (C) functions/methods registered at compile time.
 * receiver_type_id == OAK_TYPE_VOID means global function; any other value
 * means a method on the record with that type_id.
 * return_type_id == OAK_TYPE_VOID means void (or inferred from decl).
 * is_static distinguishes static methods (no implicit self) from instance
 * methods (implicit self at slot 0); ignored for global fns. */
struct oak_registered_fn_t
{
  const char* name;
  usize name_len;
  u16 const_idx;
  /* For global fns and static methods: user-facing arity.
   * For instance methods: total arity including the implicit self receiver
   * (so user writes N args, stored as N+1). */
  int arity;
  oak_type_id_t receiver_type_id; /* OAK_TYPE_VOID = global function */
  oak_type_id_t return_type_id;   /* OAK_TYPE_VOID for user-defined (from AST) */
  /* From native fn binding: array vs scalar return (element type in return_type_id) */
  enum oak_type_kind_t return_kind; /* SCALAR or ARRAY for native; else SCALAR */
  int is_static;                     /* 1 = static method, 0 = instance/global */
  const struct oak_ast_node_t* decl; /* null for native */
};

/* Unbounded registry of user-declared and native fns.
 * Lookup is O(1) via the hash table; entries owns the storage. */
struct oak_fn_registry_t
{
  struct oak_hash_table_t by_name; /* name bytes → index into entries */
  OAK_DYNARR(struct oak_registered_fn_t) entries;
};

/* ---------- Builtin method tables (fixed, small, static sets) ---------- */

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

/* ---------- Record registry ---------- */

struct oak_record_field_t
{
  /* Borrowed pointer into the lexer arena (lives for the compilation). */
  const char* name;
  usize name_len;
  struct oak_type_t type;
};

struct oak_registered_record_t
{
  const char* name;
  usize name_len;
  oak_type_id_t type_id;
  int field_count;
  struct oak_record_field_t fields[OAK_MAX_RECORD_FIELDS];
  /* Instance and static methods share one growable array, distinguished by
   * `is_static` on each entry. Freed by oak_record_registry_free. */
  OAK_DYNARR(struct oak_registered_fn_t) methods;
};

/* Unbounded registry of user record types.
 * by_name gives O(1) name lookup; find_by_type_id uses a linear scan
 * (type_id lookups are infrequent and record counts remain small). */
struct oak_record_registry_t
{
  struct oak_hash_table_t by_name; /* name bytes → index */
  OAK_DYNARR(struct oak_registered_record_t) entries;
};

/* ---------- Enum registry ---------- */

/* A single variant of a user-defined enum, lowered to a named integer
 * constant in the chunk's constant pool. */
struct oak_enum_variant_t
{
  /* Borrowed pointers into the lexer arena (live for the compilation). */
  const char* name;
  usize name_len;
  const char* enum_name;
  usize enum_name_len;
  u16 const_idx;
  int value;
};

/* Unbounded registry of enum variants.
 * by_name gives O(1) unqualified variant lookup.
 * enum_names gives O(1) existence check for enum type names.
 * Qualified lookup (EnumName::Variant) uses a linear scan — it is rare. */
struct oak_enum_registry_t
{
  struct oak_hash_table_t by_name;    /* variant name → index into variants */
  struct oak_hash_table_t enum_names; /* enum type name → 1 (set)           */
  OAK_DYNARR(struct oak_enum_variant_t) variants;
};

/* ---------- Compiler ---------- */

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
  /* Names bound at module scope (top-level `let` items only). Used to reject
   * access from inside user function and method bodies. */
  struct oak_hash_table_t module_scope_names;
  /* Module-system context. Null when compiling standalone. */
  struct oak_module_registry_t* module_registry;
  struct oak_module_t* current_module;
  /* Index into c->records.entries where user-defined records begin (after
   * native and imported records).  Set just before register_program_records. */
  int user_record_start;
  /* Index into c->enums.variants where user-defined enum variants begin.
   * Set just before register_program_enums.  -1 means unset. */
  int user_enum_start;
};

/* ---------- Registry lifecycle (called from oak_compiler.c) ---------- */

void oak_fn_registry_init(struct oak_fn_registry_t* r);
void oak_fn_registry_free(struct oak_fn_registry_t* r);

void oak_record_registry_init(struct oak_record_registry_t* r);
void oak_record_registry_free(struct oak_record_registry_t* r);

void oak_enum_registry_init(struct oak_enum_registry_t* r);
void oak_enum_registry_free(struct oak_enum_registry_t* r);

/* ---------- Registry operations ---------- */

/* Appends fn and indexes it by name. Returns pointer to the stored entry. */
struct oak_registered_fn_t*
oak_fn_registry_insert(struct oak_fn_registry_t* r,
                       const struct oak_registered_fn_t* fn);

/* O(1) lookup by name. Returns null if not found. */
const struct oak_registered_fn_t* oak_fn_registry_find(
    const struct oak_fn_registry_t* r, const char* name, usize len);

/* Appends record and indexes it by name. Returns pointer to the stored entry.
 */
struct oak_registered_record_t*
oak_record_registry_insert(struct oak_record_registry_t* r,
                           const struct oak_registered_record_t* s);

/* O(1) lookup by name. Returns null if not found. */
const struct oak_registered_record_t* oak_record_registry_find_by_name(
    const struct oak_record_registry_t* r, const char* name, usize len);

/* O(n) lookup by type_id (infrequent; records stay small). */
const struct oak_registered_record_t*
oak_record_registry_find_by_type_id(const struct oak_record_registry_t* r,
                                    oak_type_id_t type_id);

/* Appends variant and indexes it by name and enum name. */
struct oak_enum_variant_t*
oak_enum_registry_insert(struct oak_enum_registry_t* r,
                         const struct oak_enum_variant_t* v);

/* O(1) lookup by unqualified variant name. Returns null if not found. */
const struct oak_enum_variant_t* oak_enum_registry_find(
    const struct oak_enum_registry_t* r, const char* name, usize len);

/* O(n) lookup by qualified (enum_name, variant_name). */
const struct oak_enum_variant_t*
oak_enum_registry_find_qualified(const struct oak_enum_registry_t* r,
                                 const char* enum_name,
                                 usize enum_name_len,
                                 const char* variant_name,
                                 usize variant_name_len);

/* O(1) check: is this name a registered enum type name? */
int oak_enum_registry_is_enum_name(const struct oak_enum_registry_t* r,
                                   const char* name,
                                   usize len);

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

typedef enum { OAK_EMIT_U8, OAK_EMIT_U16 } oak_emit_arg_type_t;
typedef struct
{
  oak_emit_arg_type_t type;
  u16 value;
} oak_emit_arg_t;

#define OAK_ARG_U8(v)  ((oak_emit_arg_t){OAK_EMIT_U8, (u16)(v)})
#define OAK_ARG_U16(v) ((oak_emit_arg_t){OAK_EMIT_U16, (v)})

void oak_compiler_emit_op_impl(struct oak_compiler_t* c,
                                u8 op,
                                struct oak_code_loc_t loc,
                                const oak_emit_arg_t* args,
                                int n_args);

/* Variadic emit: oak_compiler_emit_op(c, op, loc [, OAK_ARG_U8/U16, ...]) */
#define oak_compiler_emit_op(c, op, loc, ...)                                 \
  oak_compiler_emit_op_impl(                                                   \
      c,                                                                        \
      op,                                                                       \
      loc,                                                                      \
      (const oak_emit_arg_t[]){{0}, ##__VA_ARGS__} + 1,                       \
      (int)(sizeof((const oak_emit_arg_t[]){{0}, ##__VA_ARGS__}) /            \
            sizeof(oak_emit_arg_t)) -                                           \
          1)

/* Returns the imported module's id if `name` is a registered alias on the
 * current compilation unit, otherwise -1. Returns -1 when there is no module
 * registry (single-file mode). */
int oak_compiler_lookup_import_alias(const struct oak_compiler_t* c,
                                     const char* name,
                                     usize name_len);

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

/* True if `name` is bound at module scope (top-level `let` in this program). */
int oak_compiler_is_module_scope_name(const struct oak_compiler_t* c,
                                      const char* name,
                                      usize len);

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

/* Returns the local-table index (NOT slot) of the local with the given slot,
 * or -1 if no such local is in scope. */
int oak_compiler_local_index_for_slot(const struct oak_compiler_t* c, int slot);

/* If `expr` is a bare identifier or `self` referring to a live local, returns
 * its local-table index. Returns -1 otherwise. */
int oak_compiler_local_index_for_ident_expr(
    const struct oak_compiler_t* c, const struct oak_ast_node_t* expr);

/* Returns the local-table index of the root-binding of a place expression
 * (walks through .field / [idx] chains), or -1 if not a place. */
int oak_compiler_local_index_for_place_root(
    const struct oak_compiler_t* c, const struct oak_ast_node_t* expr);

/* ---------- oak_compiler_types.c ---------- */

void oak_compiler_type_node_to_type(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* type_node,
                                    struct oak_type_t* out);

/* Fails compilation if the expression is typed as void (e.g. call to a void
 * fn). No-op for null or not-yet-inferrable types. */
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
                                        int arity,
                                        const char* name);

void oak_compiler_register_native_builtins(struct oak_compiler_t* c);

void oak_compiler_register_array_methods(struct oak_compiler_t* c);

void oak_compiler_register_map_methods(struct oak_compiler_t* c);

void oak_compiler_register_string_methods(struct oak_compiler_t* c);

const struct oak_method_binding_t* oak_compiler_find_array_method(
    struct oak_compiler_t* c, const char* name, usize len);

const struct oak_method_binding_t* oak_compiler_find_map_method(
    struct oak_compiler_t* c, const char* name, usize len);

const struct oak_method_binding_t* oak_compiler_find_string_method(
    struct oak_compiler_t* c, const char* name, usize len);

void oak_compiler_register_bool_methods(struct oak_compiler_t* c);
void oak_compiler_register_number_methods(struct oak_compiler_t* c);
void oak_compiler_register_record_builtin_methods(struct oak_compiler_t* c);

const struct oak_method_binding_t* oak_compiler_find_bool_method(
    struct oak_compiler_t* c, const char* name, usize len);

const struct oak_method_binding_t* oak_compiler_find_number_method(
    struct oak_compiler_t* c, const char* name, usize len);

const struct oak_method_binding_t* oak_compiler_find_record_builtin_method(
    struct oak_compiler_t* c, const char* name, usize len);

/* ---------- oak_compiler_enums.c ---------- */

void oak_compiler_register_program_enums(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* prog);

/* ---------- oak_compiler_records.c ---------- */

int oak_compiler_find_record_field(const struct oak_registered_record_t* s,
                                   const char* name,
                                   usize len);

/* Look up a method by name on a record. If `static_only` is non-zero, only
 * static methods are returned; if zero, only instance methods. */
const struct oak_registered_fn_t* oak_compiler_find_record_method(
    const struct oak_registered_record_t* sd,
    const char* name,
    usize len,
    int static_only);

/* If `recv_ty` is a known record, sets `*out_sd` and returns the field index.
 * Returns -1 if the type is not a record, or the field name is not found
 * (in the latter case `*out_sd` is still the matching record). */
int oak_compiler_record_field_index(
    const struct oak_compiler_t* c,
    struct oak_type_t recv_ty,
    const char* field_name,
    usize field_len,
    const struct oak_registered_record_t** out_sd);

/* Resolves a member for codegen; emits errors and returns -1 on failure. */
int oak_compiler_require_record_field(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* recv,
    const struct oak_ast_node_t* fname_ident,
    int is_assignment,
    const struct oak_registered_record_t** out_sd);

void oak_compiler_register_program_records(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* prog);

/* Register native types from `opts` into the compiler's record and type
 * registries before any source-level passes run.  Must be called before
 * oak_compiler_register_program_records so that Oak source can reference
 * native type names (e.g. in function parameter types). */
void oak_compiler_register_native_types(
    struct oak_compiler_t* c, const struct oak_compile_options_t* opts);

/* Register native functions and methods from `opts`.  Must be called after
 * oak_compiler_register_native_types so that receiver type ids are already
 * in the record registry.  Global fns go into c->fns; methods are appended
 * to the matching oak_registered_record_t. */
void oak_compiler_register_native_fns(
    struct oak_compiler_t* c, const struct oak_compile_options_t* opts);

/* ---------- oak_compiler_functions.c ---------- */

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

const struct oak_registered_fn_t* oak_compiler_find_registered_fn_entry(
    struct oak_compiler_t* c, const char* name, usize len);

void oak_compiler_compile_stmt_return(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* node);

void oak_compiler_compile_function_bodies(struct oak_compiler_t* c);

void oak_compiler_compile_method_bodies(struct oak_compiler_t* c);

void oak_compiler_validate_user_fn_call_arg_types(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* fn);

void oak_compiler_validate_record_method_call_arg_types(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* m);

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
