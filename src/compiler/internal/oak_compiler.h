#pragma once

/* Granular sub-headers — include only what you need in new code. */
#include "oak_defs.h"
#include "oak_emit.h"
#include "oak_enum_registry.h"
#include "oak_fn_registry.h"
#include "oak_method_table.h"
#include "oak_record_registry.h"
#include "oak_state.h"

/* Public and shared system headers still needed by all compiler files. */
#include "oak_bind.h"
#include "oak_chunk_impl.h"
#include "oak_count_of.h"
#include "oak_log.h"
#include "oak_allocator.h"
#include "oak_module_impl.h"
#include "oak_str.h"
#include "oak_type.h"
#include "oak_value_impl.h"
#include <oak_compiler.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>


oak_code_loc_t oak_compiler_loc_from_token(const oak_token_t* t);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
void oak_compiler_error_at(oak_compiler_t* c,
                           const oak_token_t* token,
                           const char* fmt,
                           ...);


oak_chunk_t* oak_compiler_init(oak_compiler_t* c,
                                       oak_compile_result_t* out,
                                       oak_allocator_t* allocator);

void oak_compiler_configure(oak_compiler_t* c,
                             const oak_compile_options_t* opts);

void oak_compiler_teardown(oak_compiler_t* c);

void oak_compiler_move_types_to_module(oak_compiler_t* c);


int oak_compiler_declare_symbol(oak_compiler_t* c,
                                const oak_token_t* token,
                                const char* name,
                                oak_symbol_kind_t kind,
                                int payload_index,
                                u16 owner_module_id,
                                int is_imported);

void oak_compiler_mark_symbol_exported(oak_compiler_t* c,
                                       const char* name);


int oak_compiler_register_native_options(oak_compiler_t* c,
                                 const oak_compile_options_t* opts);

void oak_compiler_compile_program(oak_compiler_t* c,
                          const oak_ast_node_t* program);


void oak_resolve_new_style_imports(oak_compiler_t* c,
                                    const oak_ast_node_t* program);

void oak_populate_module_exports(oak_compiler_t* c);


int oak_compiler_find_local(const oak_compiler_t* c,
                            const char* name,
                            int* out_is_mutable);

/* True if `name` is bound at module scope (top-level `let` in this program). */
int oak_is_module_scope(const oak_compiler_t* c,
                        const char* name);

void oak_compiler_add_local(oak_compiler_t* c,
                            const char* name,
                            int slot,
                            int is_mutable,
                            oak_type_t type);

void oak_compiler_begin_scope(oak_compiler_t* c);

void oak_compiler_end_scope(oak_compiler_t* c);

int oak_compile_assign_target(oak_compiler_t* c,
                                       const oak_ast_node_t* lhs,
                                       const char* non_ident_msg);

int oak_compiler_expr_is_mutable_place(const oak_compiler_t* c,
                                       const oak_ast_node_t* expr);

int oak_reject_immutable_ref_for_mutable_storage(
    oak_compiler_t* c,
    const oak_ast_node_t* expr,
    oak_type_t ty,
    const oak_token_t* err_tok,
    const char* target);

/* Returns the local-table index (NOT slot) of the local with the given slot,
 * or -1 if no such local is in scope. */
int oak_local_at_slot(const oak_compiler_t* c, int slot);

/* If `expr` is a bare identifier or `self` referring to a live local, returns
 * its local-table index. Returns -1 otherwise. */
int oak_ident_local(const oak_compiler_t* c,
                                            const oak_ast_node_t* expr);

/* Returns the local-table index of the root-binding of a place expression
 * (walks through .field / [idx] chains), or -1 if not a place. */
int oak_place_root_local(const oak_compiler_t* c,
                                            const oak_ast_node_t* expr);

int oak_expr_is_reference_place(const oak_compiler_t* c,
                                 const oak_ast_node_t* expr);


void oak_lower_type_node(oak_compiler_t* c,
                                    const oak_ast_node_t* type_node,
                                    oak_type_t* out);

int oak_type_accepts(const oak_type_t* want,
                      const oak_type_t* got);

/* Like oak_type_is_refcounted, but also returns 0 for inline value types
 * (OAK_BIND_TYPE_VALUE), which share the user-scalar id range yet are
 * represented inline (OAK_TAG_NATIVE) and never participate in refcounting or
 * weak references. */
int oak_compiler_type_is_refcounted(oak_compiler_t* c,
                                    const oak_type_t* ty);

/* Fails compilation if the expression has no value: either it is typed as
 * void (a call to a void fn) or inference could not resolve it at all. The
 * cause is diagnosed from the AST first, so the message names the undefined
 * name, missing field or missing method whenever there is one -- see
 * oak_compiler_void_diag.c. */
void oak_reject_void(oak_compiler_t* c,
                                         const oak_ast_node_t* expr);

oak_type_id_t oak_intern_type_tok(oak_compiler_t* c,
                                             const oak_token_t* token);

int oak_local_type_get(oak_compiler_t* c,
                                const char* name,
                                oak_type_t* out);

void oak_infer_type(oak_compiler_t* c,
                                         const oak_ast_node_t* expr,
                                         oak_type_t* out);

const char* oak_type_kind_name(oak_compiler_t* c,
                                        oak_type_t t);

const char* oak_type_full_name(oak_compiler_t* c,
                                        oak_type_t t);


u16 oak_intern_native_const(oak_compiler_t* c,
                                        oak_native_fn_t impl,
                                        int arity,
                                        const char* name);

void oak_register_native_builtins(oak_compiler_t* c);

void oak_register_array_methods(oak_compiler_t* c);

void oak_register_map_methods(oak_compiler_t* c);

void oak_register_string_methods(oak_compiler_t* c);

const oak_method_binding_t* oak_find_array_method(
    oak_compiler_t* c, const char* name);

const oak_method_binding_t* oak_find_map_method(
    oak_compiler_t* c, const char* name);

const oak_method_binding_t* oak_find_string_method(
    oak_compiler_t* c, const char* name);

void oak_register_bool_methods(oak_compiler_t* c);
void oak_register_number_methods(oak_compiler_t* c);
void oak_register_record_methods(oak_compiler_t* c);

const oak_method_binding_t* oak_find_bool_method(
    oak_compiler_t* c, const char* name);

const oak_method_binding_t* oak_find_number_method(
    oak_compiler_t* c, const char* name);

const oak_method_binding_t* oak_find_record_builtin_method(
    oak_compiler_t* c, const char* name);


/* If item is OAK_NODE_ATTR_DECL, returns the actual declaration node.
 * Otherwise returns item unchanged. */
const oak_ast_node_t* oak_unwrap_decl(
    const oak_ast_node_t* item);

/* True when the declaration has an `export` wrapper, possibly inside an
 * attribute wrapper such as `@Native export fn ...`. */
int oak_decl_is_exported(const oak_ast_node_t* item);

/* Allocate and fill an array of attribute name strings from an ATTR_DECL
 * node's ATTR children.  *out_count is set to the number of attributes.
 * Returns a heap-allocated array (caller must free) or NULL if item is
 * not ATTR_DECL or has no attributes. */
const char** oak_extract_attrs(oak_allocator_t* allocator,
                                const oak_ast_node_t* item,
                                int* out_count);

/* Allocate a copy of a static attribute name list.  Used by native
 * registration so every attrs array is uniformly heap-owned. */
const char** oak_alloc_attrs(oak_allocator_t* allocator,
                              const char* const* names,
                              int count);

/* Fire on_decl callbacks for any registered attribute that matches an entry in
 * attrs[].  No-op when c->opts has no native_attrs or attr_count == 0.
 * params/fields provide structured metadata for FN/RECORD targets. */
void oak_compiler_dispatch_attr_cbs(oak_compiler_t* c,
                                    const char** attrs,
                                    int attr_count,
                                    const char* decl_name,
                                    oak_attr_target_t target,
                                    const oak_attr_param_info_t* params,
                                    int param_count,
                                    const oak_attr_field_info_t* fields,
                                    int field_count,
                                    int const_index);

/* Set fn_obj->attr_hook (or native_obj->attr_hook) to the on_call of the first
 * registered attribute that matches attrs[].  No-op if nothing matches. */
void oak_apply_runtime_attr_hook(oak_compiler_t* c,
                                  oak_obj_fn_t* fn_obj,
                                  oak_obj_native_fn_t* native_obj,
                                  const char** attrs,
                                  int attr_count);


void oak_register_program_enums(oak_compiler_t* c,
                                         const oak_ast_node_t* prog);


void oak_register_program_interfaces(oak_compiler_t* c,
                                  const oak_ast_node_t* program);

int oak_record_satisfies_interface(oak_compiler_t* c,
                                const oak_registered_record_t* sd,
                                const oak_registered_interface_t* tr);

/* Resolve declared record-interface pairs and reject incomplete contracts.
 * Runs after all record method signatures are registered. */
void oak_validate_record_interfaces(oak_compiler_t* c);

u16 oak_get_or_build_vtable(oak_compiler_t* c,
                             const oak_registered_record_t* sd,
                             const oak_registered_interface_t* tr);

/* If `want` is an interface type and `arg_expr`'s concrete type satisfies it,
 * emit OAK_OP_MAKE_INTERFACE_OBJECT to wrap the top-of-stack value in an interface
 * object.  No-op when `want` is not an interface type. */
void oak_emit_interface_coerce(oak_compiler_t* c,
                            const oak_ast_node_t* arg_expr,
                            oak_type_t want,
                            oak_code_loc_t loc);

void oak_emit_weak_coerce(oak_compiler_t* c,
                           const oak_ast_node_t* arg_expr,
                           oak_type_t want,
                           oak_code_loc_t loc);


/* Computes strong-ownership reachability over all registered records, marks
 * write-once fields (cycle_locked), and rejects records that strongly own
 * interface objects. Must run after records, interfaces, imports, and native types
 * are registered, before any bodies are compiled. */
void oak_compiler_check_cycles(oak_compiler_t* c,
                               const oak_ast_node_t* program);

void oak_compiler_free_cycles(oak_compiler_t* c);

/* True if storing into a collection of type `coll` (push or indexed
 * assignment) could close a strong reference cycle: its element or key type
 * can reach a record that strongly owns a container of the same type. */
int oak_container_store_locked(oak_compiler_t* c,
                               const oak_type_t* coll);


int oak_record_field(const oak_registered_record_t* s,
                                   const char* name);

/* Look up a method by name on a record. If `static_only` is non-zero, only
 * static methods are returned; if zero, only instance methods. */
const oak_registered_fn_t*
oak_find_record_method(const oak_registered_record_t* sd,
                                const char* name,
                                int static_only);

/* If `recv_ty` is a known record, sets `*out_sd` and returns the field index.
 * Returns -1 if the type is not a record, or the field name is not found
 * (in the latter case `*out_sd` is still the matching record). */
int oak_record_field_index(
    const oak_compiler_t* c,
    oak_type_t recv_ty,
    const char* field_name,
    const oak_registered_record_t** out_sd);

/* Reports that `mname` is not an instance method of `sd`, pointing at a field
 * or static method of the same name when there is one. Shared so codegen and
 * the void-expression diagnosis speak with one voice. */
void oak_report_no_record_method(oak_compiler_t* c,
                                 const oak_token_t* token,
                                 const oak_registered_record_t* sd,
                                 const char* mname);

/* Resolves a member for codegen; emits errors and returns -1 on failure. */
int oak_require_record_field(
    oak_compiler_t* c,
    const oak_ast_node_t* recv,
    const oak_ast_node_t* fname_ident,
    int is_assignment,
    const oak_registered_record_t** out_sd);

void oak_register_program_records(oak_compiler_t* c,
                                           const oak_ast_node_t* prog);

/* Register native types from `opts` into the compiler's record and type
 * registries before any source-level passes run.  Must be called before
 * oak_register_program_records so that Oak source can reference
 * native type names (e.g. in function parameter types). */
void oak_register_native_types(
    oak_compiler_t* c, const oak_compile_options_t* opts);

/* Register native functions and methods from `opts`.  Must be called after
 * oak_register_native_types so that receiver type ids are already
 * in the record registry.  Global fns go into c->fns; methods are appended
 * to the matching oak_registered_record_t. */
void oak_register_native_fns(oak_compiler_t* c,
                                      const oak_compile_options_t* opts);

/* Register native enums from `opts` into the compiler's enum registry.
 * Each variant is interned as an integer constant in the current chunk and
 * inserted into c->enums.  Must be called before
 * oak_register_program_enums so that user code can reference the
 * native enum's variants. */
void oak_register_native_enums(
    oak_compiler_t* c, const oak_compile_options_t* opts);


const oak_ast_node_t*
oak_fn_param_list(const oak_ast_node_t* decl);

const oak_ast_node_t*
oak_fn_name_node(const oak_ast_node_t* decl);

/* The `mut` or `static` keyword node after `fn`, or null when the declaration
 * carries neither. A record member with no mode is an instance method with an
 * immutable receiver, which is why the two predicates below both answer 0 for
 * a null mode rather than being undefined on it. */
const oak_ast_node_t*
oak_fn_receiver_mode(const oak_ast_node_t* decl);

int oak_fn_is_static(const oak_ast_node_t* decl);

int oak_fn_self_is_mut(const oak_ast_node_t* decl);

const oak_ast_node_t*
oak_fn_block(const oak_ast_node_t* decl);

int oak_param_is_mut(const oak_ast_node_t* param);

const oak_ast_node_t*
oak_fn_param_ident(const oak_ast_node_t* param);

const oak_ast_node_t*
oak_fn_param_type_node(const oak_ast_node_t* param);

const oak_ast_node_t*
oak_fn_param_at(const oak_ast_node_t* decl, int index);

const oak_ast_node_t*
oak_fn_return_type_node(const oak_ast_node_t* decl);

int oak_count_fn_params(const oak_ast_node_t* decl);

void oak_register_program_fns(oak_compiler_t* c,
                                             const oak_ast_node_t* prog);

void oak_register_program_methods(oak_compiler_t* c,
                                           const oak_ast_node_t* prog);

/* Register a single FN_DECL or METHOD_DECL as an instance/static method on
 * `sd`.  raw_item may be an ATTR_DECL wrapping `item`; pass null if no
 * attributes are available (native registrations, interface methods). */
void oak_register_method_on_record(oak_compiler_t* c,
                                    const oak_ast_node_t* raw_item,
                                    const oak_ast_node_t* item,
                                    oak_registered_record_t* sd);

const oak_registered_fn_t* oak_find_fn(
    oak_compiler_t* c, const char* name);

void oak_compile_return(oak_compiler_t* c,
                                      const oak_ast_node_t* node);

/* Compile the body of a single fn or method declaration.
 * `recv` is null for free functions; non-null binds an implicit `self` local
 * with that record type. */
void oak_compile_fn_body(oak_compiler_t* c,
                          const oak_ast_node_t* decl,
                          const oak_registered_record_t* recv);

void oak_compile_fn_bodies(oak_compiler_t* c);

void oak_compile_method_bodies(oak_compiler_t* c);

void oak_check_fn_args(
    oak_compiler_t* c,
    const oak_ast_node_t* call,
    const oak_registered_fn_t* fn);

void oak_check_method_args(
    oak_compiler_t* c,
    const oak_ast_node_t* call,
    const oak_registered_fn_t* m);

/* Validate argument types directly against an AST decl node. */
void oak_check_args_against_decl(oak_compiler_t* c,
                                   const oak_ast_node_t* call,
                                   const oak_ast_node_t* decl);

/* Validate argument types for an interface method call.  Uses sig_decl when
 * available (local interface), falls back to param_types (imported interface). */
typedef struct oak_interface_method oak_interface_method_t;
void oak_check_interface_method_args(
    oak_compiler_t* c,
    const oak_ast_node_t* call,
    const oak_interface_method_t* tm);


void oak_compiler_compile_block(oak_compiler_t* c,
                                const oak_ast_node_t* block);

void oak_compiler_compile_stmt_if(oak_compiler_t* c,
                                  const oak_ast_node_t* node);

void oak_compile_while(oak_compiler_t* c,
                                     const oak_ast_node_t* node);

void oak_compile_for_from(oak_compiler_t* c,
                                        const oak_ast_node_t* node);

void oak_compile_for_in(oak_compiler_t* c,
                                      const oak_ast_node_t* node);


const oak_ast_node_t*
oak_compiler_fn_call_arg_expr_at(const oak_ast_node_t* call,
                                 usize index);

void oak_compiler_compile_fn_call(oak_compiler_t* c,
                                  const oak_ast_node_t* node);

void oak_compile_method_call(oak_compiler_t* c,
                                      const oak_ast_node_t* node,
                                      const oak_ast_node_t* callee);

void oak_compile_call_arg(oak_compiler_t* c,
                                      const oak_ast_node_t* arg);

void oak_compile_call_arg_for_type(oak_compiler_t* c,
                                    const oak_ast_node_t* arg,
                                    oak_type_t want,
                                    oak_code_loc_t loc);

void oak_compiler_compile_call_args_after_callee(oak_compiler_t* c,
                                                 const oak_ast_node_t* call);


void oak_compile_expr_fn(oak_compiler_t* c,
                          const oak_ast_node_t* node);


usize oak_child_count(const oak_ast_node_t* node);

int oak_is_int_literal(const oak_ast_node_t* node,
                                    int value);

u8 oak_op_for_node(oak_node_kind_t kind);
u8 oak_binop_for_node(oak_node_kind_t kind);

void oak_compiler_compile_node(oak_compiler_t* c,
                               const oak_ast_node_t* node);


void oak_compiler_reject_binary_void(oak_compiler_t* c,
                                     const oak_ast_node_t* node);

void oak_compiler_reject_binary_enum_misuse(oak_compiler_t* c,
                                            const oak_ast_node_t* node);

void oak_compiler_compile_binary_op(oak_compiler_t* c,
                                    const oak_ast_node_t* node);

void oak_compiler_compile_binary_and(oak_compiler_t* c,
                                     const oak_ast_node_t* node);

void oak_compiler_compile_binary_or(oak_compiler_t* c,
                                    const oak_ast_node_t* node);


void oak_compiler_compile_stmt_assignment(oak_compiler_t* c,
                                          const oak_ast_node_t* node);

void oak_compiler_compile_compound_assign(oak_compiler_t* c,
                                          const oak_ast_node_t* node);

void oak_compiler_compile_let_assignment(oak_compiler_t* c,
                                         const oak_ast_node_t* node);


void oak_compiler_compile_member_access(oak_compiler_t* c,
                                        const oak_ast_node_t* node);


void oak_compiler_compile_array_literal(oak_compiler_t* c,
                                        const oak_ast_node_t* node);

void oak_compiler_compile_map_literal(oak_compiler_t* c,
                                      const oak_ast_node_t* node);

void oak_compiler_compile_new_array(oak_compiler_t* c,
                                    const oak_ast_node_t* node);

void oak_compiler_compile_new_map(oak_compiler_t* c,
                                  const oak_ast_node_t* node);

void oak_compiler_compile_record_literal(oak_compiler_t* c,
                                         const oak_ast_node_t* node);


/* Builtin methods on the primitive types, implemented in
 * oak_compiler_method_builtins.c and installed by oak_compiler_method_table.c.
 *
 * Declared here rather than at the top of the table that uses them: C cannot
 * declare a function *through* the oak_native_fn_t typedef, so each signature
 * is written out, and a copy sitting in the consuming .c file silently drifts
 * from the callback contract instead of failing to compile. One copy, in the
 * header both sides include. */
void oak_compiler_report_bind_errors(oak_compiler_t* c,
                                     const oak_compile_options_t* opts);

#define OAK_BUILTIN_METHOD(name)                                               \
  oak_fn_call_result_t name(oak_native_call_t* call,                           \
                            const oak_value_t* args,                           \
                            usize argc,                                        \
                            oak_value_t* out)

OAK_BUILTIN_METHOD(builtin_size);
OAK_BUILTIN_METHOD(builtin_push);
OAK_BUILTIN_METHOD(builtin_has);
OAK_BUILTIN_METHOD(builtin_delete);
OAK_BUILTIN_METHOD(builtin_to_string);
OAK_BUILTIN_METHOD(builtin_string_format);
