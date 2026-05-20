#include "internal/oak_compiler.h"
#include "oak_allocator.h"

#include <stdarg.h>
#include <stdio.h>

/* ---------- Diagnostics ---------- */

struct oak_code_loc_t oak_compiler_loc_from_token(const struct oak_token_t* t)
{
  return (struct oak_code_loc_t){
    .line = oak_token_line(t),
    .column = oak_token_column(t),
  };
}

void oak_compiler_error_at(struct oak_compiler_t* c,
                           const struct oak_token_t* token,
                           const char* fmt,
                           ...)
{
  if (c->result->error_count < OAK_MAX_DIAGNOSTICS)
  {
    struct oak_diagnostic_t* d = &c->result->errors[c->result->error_count++];
    d->line = token ? oak_token_line(token) : 0;
    d->column = token ? oak_token_column(token) : 0;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->message, sizeof(d->message), fmt, ap);
    va_end(ap);
  }
  c->has_error = 1;
}

/* ---------- Compiler lifecycle helpers ---------- */

static struct oak_chunk_t* compiler_init(struct oak_compiler_t* c,
                                         struct oak_compile_result_t* out,
                                         struct oak_allocator_t* allocator)
{
  c->allocator = allocator;

  struct oak_chunk_t* chunk =
      OAK_ALLOC(allocator, sizeof(struct oak_chunk_t));
  oak_chunk_init(chunk, allocator);

  struct oak_type_t no_return_type;
  oak_type_clear(&no_return_type);

  c->chunk = chunk;
  c->result = out;
  c->has_error = 0;
  c->scope = (struct oak_scope_ctx_t){
    .local_count = 0,
    .scope_depth = 0,
    .stack_depth = 0,
    .declared_return_type = no_return_type,
    .current_loop = null,
    .fn_depth = 0,
  };

  oak_type_registry_init(&c->types, allocator);
  oak_fn_registry_init(&c->fns, allocator);
  oak_record_registry_init(&c->records, allocator);
  oak_enum_registry_init(&c->enums, allocator);
  oak_htable_init(&c->module_scope_names, allocator);
  oak_trait_registry_init(&c->traits, allocator);
  c->user_record_start = 0;
  c->user_enum_start = -1;
  c->user_trait_start = 0;

  return chunk;
}

static void compiler_teardown(struct oak_compiler_t* c)
{
  oak_htable_free(&c->module_scope_names);
  oak_fn_registry_free(&c->fns);
  oak_record_registry_free(&c->records);
  oak_enum_registry_free(&c->enums);
  oak_trait_registry_free(&c->traits);
  oak_type_registry_free(&c->types);
}

/* Only direct module-scope `let` items (not lets nested in if/while/for). */
static void collect_module_scope_names(struct oak_compiler_t* c,
                                       const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (!item || item->kind != OAK_NODE_STMT_LET_ASSIGNMENT)
      continue;
    const struct oak_ast_node_t* assign = item->rhs;
    if (!assign || assign->kind != OAK_NODE_STMT_ASSIGNMENT)
      continue;
    const struct oak_ast_node_t* ident = assign->lhs;
    if (!ident || ident->kind != OAK_NODE_IDENT)
      continue;
    const char* name = oak_token_text(ident->token);
    const usize name_len = oak_token_length(ident->token);
    if (oak_htable_get(&c->module_scope_names, name, name_len) < 0)
      oak_htable_insert(&c->module_scope_names, name, name_len, 1);
  }
}

static void compile_program_items(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind == OAK_NODE_FN_DECL)
      continue;
    /* Record and enum declarations are processed in pre-passes; no code. */
    if (item->kind == OAK_NODE_RECORD_DECL)
      continue;
    if (item->kind == OAK_NODE_RECORD_DECL_EMPTY)
      continue;
    if (item->kind == OAK_NODE_ENUM_DECL)
      continue;
    /* Imports are resolved by the module loader and the imports pre-pass;
     * they emit no top-level code. */
    if (item->kind == OAK_NODE_IMPORT_SELECTIVE)
      continue;
    if (item->kind == OAK_NODE_IMPORT_WILDCARD)
      continue;
    /* Trait and method declarations are processed in pre-passes; no top-level code. */
    if (item->kind == OAK_NODE_TRAIT_DECL)
      continue;
    if (item->kind == OAK_NODE_METHOD_DECL)
      continue;
    /* Attributed declarations (@Attr fn/record/enum) are handled by pre-passes. */
    if (item->kind == OAK_NODE_ATTR_DECL)
      continue;
    oak_compiler_compile_node(c, item);
    /* Recover after a top-level statement error so subsequent items are also
     * checked and all errors are reported in a single compilation pass. */
    if (c->has_error)
    {
      c->has_error = 0;
      c->scope.stack_depth = 0; /* top-level scope has no stack state */
    }
  }
}

/* Defined in oak_compiler_imports.c */
void resolve_new_style_imports(struct oak_compiler_t* c,
                               const struct oak_ast_node_t* program);
void populate_module_exports(struct oak_compiler_t* c);

static void compile_program(struct oak_compiler_t* c,
                            const struct oak_ast_node_t* program)
{
  /* Step 1 — register all built-in functions and methods. */
  oakc_register_native_builtins(c);
  CHECK_ERROR(c);
  oakc_register_array_methods(c);
  CHECK_ERROR(c);
  oakc_register_map_methods(c);
  CHECK_ERROR(c);
  oakc_register_string_methods(c);
  CHECK_ERROR(c);
  oakc_register_bool_methods(c);
  CHECK_ERROR(c);
  oakc_register_number_methods(c);
  CHECK_ERROR(c);
  oakc_register_record_methods(c);
  CHECK_ERROR(c);

  /* Step 2 — register types in topological order (imported before local) so
   * that IDs are consistent across all modules in the program.
   * Selective and wildcard imports pull symbols into the local registries. */
  resolve_new_style_imports(c, program);
  CHECK_ERROR(c);
  c->user_enum_start = c->enums.variants.count;
  oakc_register_program_enums(c, program);
  CHECK_ERROR(c);

  c->user_record_start = c->records.entries.count;
  oakc_register_program_records(c, program);
  CHECK_ERROR(c);

  /* Step 2b — register traits (after records so trait method types resolve). */
  c->user_trait_start = c->traits.trait_count;
  oakc_register_program_traits(c, program);
  CHECK_ERROR(c);

  /* Step 3 — register functions and methods, then emit code. */
  oakc_register_program_fns(c, program);
  CHECK_ERROR(c);
  oakc_register_program_methods(c, program);
  CHECK_ERROR(c);
  oakc_register_method_decls(c, program);
  CHECK_ERROR(c);

  collect_module_scope_names(c, program);
  compile_program_items(c, program);
  CHECK_ERROR(c);

  /* Step 4 — emit halt, compile deferred bodies, populate exports. */
  oak_compiler_emit_op(c, OAK_OP_HALT, OAK_LOC_SYNTHETIC);
  oakc_compile_fn_bodies(c);
  CHECK_ERROR(c);
  oakc_compile_method_bodies(c);
  CHECK_ERROR(c);
  oakc_compile_method_decl_bodies(c, program);
  CHECK_ERROR(c);
  populate_module_exports(c);

  /* Move the type registry to the module so importing modules can translate
   * type IDs back to names.  Nulling the compiler copy prevents double-free. */
  if (c->current_module)
  {
    c->current_module->types = c->types;
    c->types.entries = null;
    c->types.count = 0;
    c->types.capacity = 0;
  }
}

void oak_compile(const struct oak_ast_node_t* root,
                 struct oak_compile_result_t* out)
{
  oak_compile_ex(root, null, out);
}

void oak_compile_ex(const struct oak_ast_node_t* root,
                    const struct oak_compile_options_t* opts,
                    struct oak_compile_result_t* out)
{
  struct oak_compiler_t compiler = { 0 };
  struct oak_allocator_t* allocator =
      (opts && opts->allocator) ? opts->allocator : &oak_system_allocator;
  struct oak_chunk_t* chunk = compiler_init(&compiler, out, allocator);
  compiler.opts = opts;
  const int want_debug = !opts || opts->emit_debug_info;
  if (want_debug)
    oak_chunk_enable_debug(chunk, opts ? opts->source_name : null);

  /* Module-system context: when a registry/current_module are supplied, the
   * compiler emits cross-module references and tags fn objects with the
   * current module's id. */
  if (opts && opts->module_registry && opts->current_module)
  {
    compiler.module_registry = opts->module_registry;
    compiler.current_module = opts->current_module;
    chunk->module_id = opts->current_module->module_id;
  }
  compiler.allow_bodyless_fns = opts ? opts->allow_bodyless_fns : 0;

  if (!root || root->kind != OAK_NODE_PROGRAM)
  {
    oak_compiler_error_at(&compiler, null, "expected a program root");
    oak_chunk_free(chunk);
    compiler_teardown(&compiler);
    return;
  }

  /* Register native types before any source-level passes so Oak source can
   * reference native type names in function signatures, record fields, etc. */
  if (opts && opts->native_types.count > 0)
  {
    oakc_register_native_types(&compiler, opts);
    if (compiler.has_error)
    {
      oak_chunk_free(chunk);
      compiler_teardown(&compiler);
      return;
    }
  }

  /* Register native functions and methods after types (receiver ids need to
   * be in the record registry first). */
  if (opts && (opts->native_fns.count > 0 || opts->native_global_fns.count > 0))
  {
    oakc_register_native_fns(&compiler, opts);
    if (compiler.has_error)
    {
      oak_chunk_free(chunk);
      compiler_teardown(&compiler);
      return;
    }
  }

  /* Register native enums before any source-level enum passes so user code
   * can reference their variants (e.g. FileMode.Read). */
  if (opts && opts->native_enums.count > 0)
  {
    oakc_register_native_enums(&compiler, opts);
    if (compiler.has_error)
    {
      oak_chunk_free(chunk);
      compiler_teardown(&compiler);
      return;
    }
  }

  compile_program(&compiler, root);

  compiler_teardown(&compiler);

  if (out->error_count > 0)
  {
    oak_chunk_free(chunk);
    return;
  }

  out->chunk = chunk;
}

void oak_compile_result_free(struct oak_compile_result_t* result)
{
  if (result && result->chunk)
  {
    oak_chunk_free(result->chunk);
    result->chunk = null;
  }
}
