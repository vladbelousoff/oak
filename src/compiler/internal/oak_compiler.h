#pragma once

/* Granular sub-headers — include only what you need in new code. */
#include "oakc_defs.h"
#include "oakc_emit.h"
#include "oakc_enum_registry.h"
#include "oakc_fn_registry.h"
#include "oakc_method_table.h"
#include "oakc_record_registry.h"
#include "oakc_state.h"

/* Public and shared system headers still needed by all compiler files. */
#include "oak_bind.h"
#include "oak_chunk.h"
#include "oak_count_of.h"
#include "oak_dynarr.h"
#include "oak_htable.h"
#include "oak_log.h"
#include "oak_mem.h"
#include "oak_module.h"
#include "oak_str.h"
#include "oak_type.h"
#include "oak_value.h"
#include <oak_compiler.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- oak_compiler_error.c ---------- */

struct oak_code_loc_t oak_compiler_loc_from_token(const struct oak_token_t* t);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
void oak_compiler_error_at(struct oak_compiler_t* c,
                           const struct oak_token_t* token,
                           const char* fmt,
                           ...);

/* Returns the imported module's id if `name` is a registered alias on the
 * current compilation unit, otherwise -1. Returns -1 when there is no module
 * registry (single-file mode). */
int oakc_import_alias(const struct oak_compiler_t* c,
                                     const char* name,
                                     usize name_len);

/* ---------- oak_compiler_scope.c ---------- */

int oak_compiler_find_local(const struct oak_compiler_t* c,
                            const char* name,
                            usize length,
                            int* out_is_mutable);

/* True if `name` is bound at module scope (top-level `let` in this program). */
int oakc_is_module_scope(const struct oak_compiler_t* c,
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

int oakc_compile_assign_target(struct oak_compiler_t* c,
                                       const struct oak_ast_node_t* lhs,
                                       const char* non_ident_msg);

int oak_compiler_expr_is_mutable_place(const struct oak_compiler_t* c,
                                       const struct oak_ast_node_t* expr);

/* Returns the local-table index (NOT slot) of the local with the given slot,
 * or -1 if no such local is in scope. */
int oakc_local_at_slot(const struct oak_compiler_t* c, int slot);

/* If `expr` is a bare identifier or `self` referring to a live local, returns
 * its local-table index. Returns -1 otherwise. */
int oakc_ident_local(const struct oak_compiler_t* c,
                                            const struct oak_ast_node_t* expr);

/* Returns the local-table index of the root-binding of a place expression
 * (walks through .field / [idx] chains), or -1 if not a place. */
int oakc_place_root_local(const struct oak_compiler_t* c,
                                            const struct oak_ast_node_t* expr);

/* ---------- oak_compiler_types.c ---------- */

void oakc_lower_type_node(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* type_node,
                                    struct oak_type_t* out);

/* Fails compilation if the expression is typed as void (e.g. call to a void
 * fn). No-op for null or not-yet-inferrable types. */
void oakc_reject_void(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* expr);

oak_type_id_t oakc_intern_type_tok(struct oak_compiler_t* c,
                                             const struct oak_token_t* token);

int oakc_local_type_get(struct oak_compiler_t* c,
                                const char* name,
                                usize len,
                                struct oak_type_t* out);

void oakc_infer_type(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* expr,
                                         struct oak_type_t* out);

const char* oakc_type_kind_name(struct oak_compiler_t* c,
                                        struct oak_type_t t);

const char* oakc_type_full_name(struct oak_compiler_t* c,
                                        struct oak_type_t t);

/* ---------- oak_compiler_builtins.c ---------- */

u16 oakc_intern_native_const(struct oak_compiler_t* c,
                                        oak_native_fn_t impl,
                                        int arity,
                                        const char* name);

void oakc_register_native_builtins(struct oak_compiler_t* c);

void oakc_register_array_methods(struct oak_compiler_t* c);

void oakc_register_map_methods(struct oak_compiler_t* c);

void oakc_register_string_methods(struct oak_compiler_t* c);

const struct oak_method_binding_t* oakc_find_array_method(
    struct oak_compiler_t* c, const char* name, usize len);

const struct oak_method_binding_t* oakc_find_map_method(
    struct oak_compiler_t* c, const char* name, usize len);

const struct oak_method_binding_t* oakc_find_string_method(
    struct oak_compiler_t* c, const char* name, usize len);

void oakc_register_bool_methods(struct oak_compiler_t* c);
void oakc_register_number_methods(struct oak_compiler_t* c);
void oakc_register_record_methods(struct oak_compiler_t* c);

const struct oak_method_binding_t* oakc_find_bool_method(
    struct oak_compiler_t* c, const char* name, usize len);

const struct oak_method_binding_t* oakc_find_number_method(
    struct oak_compiler_t* c, const char* name, usize len);

const struct oak_method_binding_t* oakc_find_record_builtin_method(
    struct oak_compiler_t* c, const char* name, usize len);

/* ---------- oak_compiler_attrs.c ---------- */

/* If item is OAK_NODE_ATTR_DECL, returns the actual declaration node
 * (the last non-ATTR child).  Otherwise returns item unchanged. */
const struct oak_ast_node_t* oakc_unwrap_decl(
    const struct oak_ast_node_t* item);

/* Allocate and fill an array of attribute name strings from an ATTR_DECL
 * node's ATTR children.  *out_count is set to the number of attributes.
 * Returns a heap-allocated array (must be oak_free'd) or NULL if item is
 * not ATTR_DECL or has no attributes. */
const char** oakc_extract_attrs(const struct oak_ast_node_t* item,
                                int* out_count);

/* Allocate a copy of a static attribute name list.  Used by native
 * registration so every attrs array is uniformly heap-owned. */
const char** oakc_alloc_attrs(const char* const* names, int count);

/* Fire on_decl callbacks for any registered attribute that matches an entry in
 * attrs[].  No-op when c->opts has no native_attrs or attr_count == 0. */
void oakc_dispatch_compile_attr_cbs(struct oak_compiler_t* c,
                                    const char** attrs,
                                    int attr_count,
                                    const char* decl_name,
                                    enum oak_attr_target_t target);

/* Set fn_obj->attr_hook (or native_obj->attr_hook) to the on_call of the first
 * registered attribute that matches attrs[].  No-op if nothing matches. */
void oakc_apply_runtime_attr_hook(struct oak_compiler_t* c,
                                  struct oak_obj_fn_t* fn_obj,
                                  struct oak_obj_native_fn_t* native_obj,
                                  const char** attrs,
                                  int attr_count);

/* ---------- oak_compiler_enums.c ---------- */

void oakc_register_program_enums(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* prog);

/* ---------- oak_compiler_traits.c ---------- */

void oakc_register_program_traits(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* program);

void oakc_register_method_decls(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* program);

void oakc_compile_method_decl_bodies(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* program);

/* Returns the type-name IDENT node from a METHOD_DECL node.
 * Path: decl->lhs (METHOD_PROTO) -> lhs (METHOD_HEAD) -> lhs (type IDENT). */
const struct oak_ast_node_t* oakc_method_decl_type_ident(
    const struct oak_ast_node_t* decl);

int oakc_record_satisfies_trait(struct oak_compiler_t* c,
                                const struct oak_registered_record_t* sd,
                                const struct oak_registered_trait_t* tr);

u16 oakc_get_or_build_vtable(struct oak_compiler_t* c,
                             const struct oak_registered_record_t* sd,
                             const struct oak_registered_trait_t* tr);

/* If `want` is a trait type and `arg_expr`'s concrete type satisfies it,
 * emit OAK_OP_MAKE_TRAIT_OBJECT to wrap the top-of-stack value in a trait
 * object.  No-op when `want` is not a trait type. */
void oakc_emit_trait_coerce(struct oak_compiler_t* c,
                            const struct oak_ast_node_t* arg_expr,
                            struct oak_type_t want,
                            struct oak_code_loc_t loc);

/* ---------- oak_compiler_record_registry.c ---------- */

int oakc_record_field(const struct oak_registered_record_t* s,
                                   const char* name,
                                   usize len);

/* Look up a method by name on a record. If `static_only` is non-zero, only
 * static methods are returned; if zero, only instance methods. */
const struct oak_registered_fn_t*
oakc_find_record_method(const struct oak_registered_record_t* sd,
                                const char* name,
                                usize len,
                                int static_only);

/* If `recv_ty` is a known record, sets `*out_sd` and returns the field index.
 * Returns -1 if the type is not a record, or the field name is not found
 * (in the latter case `*out_sd` is still the matching record). */
int oakc_record_field_index(
    const struct oak_compiler_t* c,
    struct oak_type_t recv_ty,
    const char* field_name,
    usize field_len,
    const struct oak_registered_record_t** out_sd);

/* Resolves a member for codegen; emits errors and returns -1 on failure. */
int oakc_require_record_field(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* recv,
    const struct oak_ast_node_t* fname_ident,
    int is_assignment,
    const struct oak_registered_record_t** out_sd);

void oakc_register_program_records(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* prog);

/* Register native types from `opts` into the compiler's record and type
 * registries before any source-level passes run.  Must be called before
 * oakc_register_program_records so that Oak source can reference
 * native type names (e.g. in function parameter types). */
void oakc_register_native_types(
    struct oak_compiler_t* c, const struct oak_compile_options_t* opts);

/* Register native functions and methods from `opts`.  Must be called after
 * oakc_register_native_types so that receiver type ids are already
 * in the record registry.  Global fns go into c->fns; methods are appended
 * to the matching oak_registered_record_t. */
void oakc_register_native_fns(struct oak_compiler_t* c,
                                      const struct oak_compile_options_t* opts);

/* Register native enums from `opts` into the compiler's enum registry.
 * Each variant is interned as an integer constant in the current chunk and
 * inserted into c->enums.  Must be called before
 * oakc_register_program_enums so that user code can reference the
 * native enum's variants. */
void oakc_register_native_enums(
    struct oak_compiler_t* c, const struct oak_compile_options_t* opts);

/* ---------- oak_compiler_fn_decl.c / oak_compiler_fn_register.c / oak_compiler_fn_body.c / oak_compiler_fn_argcheck.c ---------- */

const struct oak_ast_node_t*
oakc_fn_param_list(const struct oak_ast_node_t* decl);

const struct oak_ast_node_t*
oakc_fn_name_node(const struct oak_ast_node_t* decl);

const struct oak_ast_node_t*
oakc_fn_self_param(const struct oak_ast_node_t* decl);

int oakc_self_is_mut(const struct oak_ast_node_t* sp);

const struct oak_ast_node_t*
oakc_fn_block(const struct oak_ast_node_t* decl);

int oakc_param_is_mut(const struct oak_ast_node_t* param);

const struct oak_ast_node_t*
oakc_fn_param_ident(const struct oak_ast_node_t* param);

const struct oak_ast_node_t*
oakc_fn_param_type_node(const struct oak_ast_node_t* param);

const struct oak_ast_node_t*
oakc_fn_param_at(const struct oak_ast_node_t* decl, int index);

const struct oak_ast_node_t*
oakc_fn_return_type_node(const struct oak_ast_node_t* decl);

int oakc_count_fn_params(const struct oak_ast_node_t* decl);

void oakc_register_program_fns(struct oak_compiler_t* c,
                                             const struct oak_ast_node_t* prog);

void oakc_register_program_methods(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* prog);

/* Register a single FN_DECL or METHOD_DECL as an instance/static method on
 * `sd`.  raw_item may be an ATTR_DECL wrapping `item`; pass null if no
 * attributes are available (native registrations, trait methods). */
void oakc_register_method_on_record(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* raw_item,
                                    const struct oak_ast_node_t* item,
                                    struct oak_registered_record_t* sd);

const struct oak_registered_fn_t* oakc_find_fn(
    struct oak_compiler_t* c, const char* name, usize len);

void oakc_compile_return(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* node);

/* Compile the body of a single fn or method declaration.
 * `recv` is null for free functions; non-null binds an implicit `self` local
 * with that record type. */
void oakc_compile_fn_body(struct oak_compiler_t* c,
                          const struct oak_ast_node_t* decl,
                          const struct oak_registered_record_t* recv);

void oakc_compile_fn_bodies(struct oak_compiler_t* c);

void oakc_compile_method_bodies(struct oak_compiler_t* c);

void oakc_check_fn_args(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* fn);

void oakc_check_method_args(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* m);

/* Validate argument types and aliasing directly against an AST decl node.
 * Used for virtual dispatch where only the trait sig_decl is available. */
void oakc_check_args_against_decl(struct oak_compiler_t* c,
                                   const struct oak_ast_node_t* call,
                                   const struct oak_ast_node_t* decl);

/* ---------- oak_compiler_stmt.c ---------- */

void oak_compiler_compile_block(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* block);

void oak_compiler_compile_stmt_if(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* node);

void oakc_compile_while(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* node);

void oakc_compile_for_from(struct oak_compiler_t* c,
                                        const struct oak_ast_node_t* node);

void oakc_compile_for_in(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* node);

/* ---------- oak_compiler_call_arg.c / oak_compiler_call_method.c / oak_compiler_calls.c ---------- */

const struct oak_ast_node_t*
oak_compiler_fn_call_arg_expr_at(const struct oak_ast_node_t* call,
                                 usize index);

void oak_compiler_compile_fn_call(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* node);

void oakc_compile_method_call(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* node,
                                      const struct oak_ast_node_t* callee);

void oakc_compile_call_arg(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* arg);

void oak_compiler_compile_call_args_after_callee(struct oak_compiler_t* c,
                                                 const struct oak_ast_node_t* call);

/* ---------- oak_compiler_expr.c ---------- */

usize oakc_child_count(const struct oak_ast_node_t* node);

int oakc_is_int_literal(const struct oak_ast_node_t* node,
                                    int value);

u8 oakc_op_for_node(enum oak_node_kind_t kind);
u8 oakc_binop_for_node(enum oak_node_kind_t kind);

void oak_compiler_compile_node(struct oak_compiler_t* c,
                               const struct oak_ast_node_t* node);

/* ---------- oak_compiler_expr_binary.c ---------- */

void oak_compiler_reject_binary_void(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* node);

void oak_compiler_reject_binary_enum_misuse(struct oak_compiler_t* c,
                                            const struct oak_ast_node_t* node);

void oak_compiler_compile_binary_op(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* node);

void oak_compiler_compile_binary_and(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* node);

void oak_compiler_compile_binary_or(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* node);

/* ---------- oak_compiler_expr_assign.c ---------- */

void oak_compiler_compile_stmt_assignment(struct oak_compiler_t* c,
                                          const struct oak_ast_node_t* node);

void oak_compiler_compile_compound_assign(struct oak_compiler_t* c,
                                          const struct oak_ast_node_t* node);

void oak_compiler_compile_let_assignment(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* node);

/* ---------- oak_compiler_expr_member.c ---------- */

void oak_compiler_compile_member_access(struct oak_compiler_t* c,
                                        const struct oak_ast_node_t* node);

/* ---------- oak_compiler_expr_collection.c ---------- */

void oak_compiler_compile_array_literal(struct oak_compiler_t* c,
                                        const struct oak_ast_node_t* node);

void oak_compiler_compile_map_literal(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* node);

void oak_compiler_compile_cast(struct oak_compiler_t* c,
                               const struct oak_ast_node_t* node);

void oak_compiler_compile_record_literal(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* node);
