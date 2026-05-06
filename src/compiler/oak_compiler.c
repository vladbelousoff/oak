#include "oak_compiler_internal.h"

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
                                         struct oak_compile_result_t* out)
{
  struct oak_chunk_t* chunk =
      oak_alloc(sizeof(struct oak_chunk_t), OAK_SRC_LOC);
  oak_chunk_init(chunk);

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

  oak_type_registry_init(&c->types);
  oak_fn_registry_init(&c->fns);
  oak_record_registry_init(&c->records);
  oak_enum_registry_init(&c->enums);
  oak_hash_table_init(&c->module_scope_names);
  c->user_record_start = 0;
  c->user_enum_start = -1;

  return chunk;
}

static void compiler_teardown(struct oak_compiler_t* c)
{
  oak_hash_table_free(&c->module_scope_names);
  oak_fn_registry_free(&c->fns);
  oak_record_registry_free(&c->records);
  oak_enum_registry_free(&c->enums);
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
    if (oak_hash_table_get(&c->module_scope_names, name, name_len) < 0)
      oak_hash_table_insert(&c->module_scope_names, name, name_len, 1);
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
    if (item->kind == OAK_NODE_ENUM_DECL)
      continue;
    /* Imports are resolved by the module loader and the imports pre-pass;
     * they emit no top-level code. */
    if (item->kind == OAK_NODE_IMPORT_DECL)
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

int oak_compiler_lookup_import_alias(const struct oak_compiler_t* c,
                                     const char* name,
                                     const usize name_len)
{
  if (!c->current_module)
    return -1;
  return oak_hash_table_get(&c->current_module->imports, name, name_len);
}

/* Pre-register enum variants from all imported modules so that cross-module
 * `alias.EnumName.Variant` expressions can be resolved.  Variants are stored
 * as small integers; we intern OAK_VALUE_I32(value) as constants in the
 * current chunk and register them in c->enums with the local const_idx.
 * Must run BEFORE oak_compiler_register_program_enums. */
static void register_imported_enums(struct oak_compiler_t* c)
{
  if (!c->module_registry || !c->current_module)
    return;
  const struct oak_module_t* mod = c->current_module;
  for (int di = 0; di < mod->import_modules.count; ++di)
  {
    const u16 dep_id = mod->import_modules.items[di];
    const struct oak_module_t* dep =
        oak_module_registry_get(c->module_registry, dep_id);
    if (!dep)
      continue;
    for (int ei = 0; ei < dep->exports_enum.count; ++ei)
    {
      const struct oak_module_export_enum_t* exp = &dep->exports_enum.items[ei];
      /* Skip if this enum name is already registered (diamond imports). */
      if (oak_enum_registry_is_enum_name(&c->enums, exp->name, exp->name_len))
        continue;
      for (int vi = 0; vi < exp->variant_count; ++vi)
      {
        const struct oak_module_export_enum_variant_t* v = &exp->variants[vi];
        /* Skip if the unqualified variant name already exists. */
        if (oak_enum_registry_find(&c->enums, v->name, v->name_len))
          continue;
        const u16 local_idx =
            oak_compiler_intern_constant(c, OAK_VALUE_I32(v->value));
        if (c->has_error)
          return;
        struct oak_enum_variant_t ev = {
          .name = v->name,
          .name_len = v->name_len,
          .enum_name = exp->name,
          .enum_name_len = exp->name_len,
          .const_idx = local_idx,
          .value = v->value,
        };
        oak_enum_registry_insert(&c->enums, &ev);
      }
    }
  }
}

/* For each imported module in the current module's dependency list, pre-register
 * its exported record types into the current compiler's type and record
 * registries.  This must run BEFORE oak_compiler_register_program_records so
 * that user-defined type IDs are assigned in a consistent topological order
 * across all modules. */
static void register_imported_records(struct oak_compiler_t* c)
{
  if (!c->module_registry || !c->current_module)
    return;
  const struct oak_module_t* mod = c->current_module;
  for (int di = 0; di < mod->import_modules.count; ++di)
  {
    const u16 dep_id = mod->import_modules.items[di];
    const struct oak_module_t* dep =
        oak_module_registry_get(c->module_registry, dep_id);
    if (!dep)
      continue;
    for (int ri = 0; ri < dep->exports_record.count; ++ri)
    {
      const struct oak_module_export_record_t* exp =
          &dep->exports_record.items[ri];
      /* Skip if already registered (diamond imports). */
      if (oak_record_registry_find_by_name(&c->records, exp->name, exp->name_len))
        continue;
      const oak_type_id_t tid =
          oak_type_registry_intern(&c->types, exp->name, exp->name_len);
      struct oak_registered_record_t proto = { 0 };
      proto.name = exp->name;
      proto.name_len = exp->name_len;
      proto.type_id = tid;
      proto.field_count = exp->field_count;
      oak_dynarr_init(&proto.methods.items, &proto.methods.count, &proto.methods.capacity);
      for (int fi = 0; fi < exp->field_count; ++fi)
      {
        proto.fields[fi].name = exp->fields[fi].name;
        proto.fields[fi].name_len = exp->fields[fi].name_len;
        oak_type_clear(&proto.fields[fi].type);
        proto.fields[fi].type.id = oak_type_registry_intern(
            &c->types, exp->fields[fi].type_name, exp->fields[fi].type_name_len);
      }
      oak_record_registry_insert(&c->records, &proto);
    }
  }
}

/* Populate the current module's export tables from the now-fully-populated
 * compiler registries.  All exports use the module's lexer-arena strings, so
 * they remain valid as long as the module is alive. */
static void populate_module_exports(struct oak_compiler_t* c)
{
  if (!c->current_module)
    return;
  struct oak_module_t* mod = c->current_module;
  for (int i = 0; i < c->fns.entries.count; ++i)
  {
    const struct oak_registered_fn_t* e = &c->fns.entries.items[i];
    /* Only export fns that come from the user's source (decl != null) AND are
     * global (no receiver).  Native fns and methods are not exposed cross-
     * module in v1. */
    if (!e->decl || e->receiver_type_id != OAK_TYPE_VOID)
      continue;
    struct oak_module_export_fn_t exp = {
      .name = e->name,
      .name_len = e->name_len,
      .const_idx = e->const_idx,
      .arity = e->arity,
      .return_type_node = oak_compiler_fn_decl_return_type_node(e->decl),
    };
    const int idx = mod->exports_fn.count;
    oak_dynarr_push(&mod->exports_fn.items, &mod->exports_fn.count, &mod->exports_fn.capacity, &exp, sizeof(exp));
    oak_hash_table_insert(
        &mod->exports_fn_by_name, e->name, e->name_len, idx);
  }
  /* Export user-defined records (those registered after native + imported ones,
   * i.e. with index >= c->user_record_start). */
  for (int i = c->user_record_start; i < c->records.entries.count; ++i)
  {
    const struct oak_registered_record_t* r = &c->records.entries.items[i];
    if (mod->exports_record.count >= OAK_MODULE_MAX_RECORD_FIELDS)
      break; /* guard; in practice record counts are small */
    struct oak_module_export_record_t exp = { 0 };
    exp.name = r->name;
    exp.name_len = r->name_len;
    exp.field_count = r->field_count > OAK_MODULE_MAX_RECORD_FIELDS
                          ? OAK_MODULE_MAX_RECORD_FIELDS
                          : r->field_count;
    for (int fi = 0; fi < exp.field_count; ++fi)
    {
      exp.fields[fi].name = r->fields[fi].name;
      exp.fields[fi].name_len = r->fields[fi].name_len;
      /* Resolve type_id back to a name via the type registry so the importing
       * module can re-intern it using its own registry. */
      if (r->fields[fi].type.id >= 0 &&
          r->fields[fi].type.id < c->types.count)
      {
        exp.fields[fi].type_name = c->types.entries[r->fields[fi].type.id].name;
        exp.fields[fi].type_name_len =
            c->types.entries[r->fields[fi].type.id].len;
      }
    }
    exp.layout_id = 0; /* populated on first cross-module new when needed */
    const int idx = mod->exports_record.count;
    oak_dynarr_push(&mod->exports_record.items, &mod->exports_record.count, &mod->exports_record.capacity, &exp, sizeof(exp));
    oak_hash_table_insert(
        &mod->exports_record_by_name, exp.name, exp.name_len, idx);
  }
  /* Export user-defined enums (those registered after native + imported ones).
   * We group variants by enum_name to produce one export per enum type. */
  if (c->user_enum_start >= 0)
  {
    for (int i = c->user_enum_start; i < c->enums.variants.count; ++i)
    {
      const struct oak_enum_variant_t* v = &c->enums.variants.items[i];
      /* Find or create the export entry for this enum type. */
      int eidx =
          oak_hash_table_get(&mod->exports_enum_by_name, v->enum_name, v->enum_name_len);
      if (eidx < 0)
      {
        struct oak_module_export_enum_t ee = { 0 };
        ee.name = v->enum_name;
        ee.name_len = v->enum_name_len;
        eidx = mod->exports_enum.count;
        oak_dynarr_push(&mod->exports_enum.items, &mod->exports_enum.count, &mod->exports_enum.capacity, &ee, sizeof(ee));
        oak_hash_table_insert(
            &mod->exports_enum_by_name, ee.name, ee.name_len, eidx);
      }
      struct oak_module_export_enum_t* ee = &mod->exports_enum.items[eidx];
      if (ee->variant_count < OAK_MODULE_MAX_ENUM_VARIANTS)
      {
        ee->variants[ee->variant_count].name = v->name;
        ee->variants[ee->variant_count].name_len = v->name_len;
        ee->variants[ee->variant_count].value = v->value;
        ++ee->variant_count;
      }
    }
  }
}

static void compile_program(struct oak_compiler_t* c,
                            const struct oak_ast_node_t* program)
{
  /* Step 1 — register all built-in functions and methods. */
  oak_compiler_register_native_builtins(c);     CHECK_ERROR(c);
  oak_compiler_register_array_methods(c);       CHECK_ERROR(c);
  oak_compiler_register_map_methods(c);         CHECK_ERROR(c);
  oak_compiler_register_string_methods(c);      CHECK_ERROR(c);
  oak_compiler_register_bool_methods(c);        CHECK_ERROR(c);
  oak_compiler_register_number_methods(c);      CHECK_ERROR(c);
  oak_compiler_register_record_builtin_methods(c); CHECK_ERROR(c);

  /* Step 2 — register types in topological order (imported before local) so
   * that IDs are consistent across all modules in the program.
   * Import alias resolution uses c->current_module->imports, which the loader
   * already populated; no extra pass is needed here. */
  register_imported_enums(c);
  CHECK_ERROR(c);
  c->user_enum_start = c->enums.variants.count;
  oak_compiler_register_program_enums(c, program);
  CHECK_ERROR(c);

  register_imported_records(c);
  CHECK_ERROR(c);
  c->user_record_start = c->records.entries.count;
  oak_compiler_register_program_records(c, program);
  CHECK_ERROR(c);

  /* Step 3 — register functions and methods, then emit code. */
  oak_compiler_register_program_functions(c, program); CHECK_ERROR(c);
  oak_compiler_register_program_methods(c, program);   CHECK_ERROR(c);

  collect_module_scope_names(c, program);
  compile_program_items(c, program);
  CHECK_ERROR(c);

  /* Step 4 — emit halt, compile deferred bodies, populate exports. */
  oak_compiler_emit_op(c, OAK_OP_HALT, OAK_LOC_SYNTHETIC);
  oak_compiler_compile_function_bodies(c); CHECK_ERROR(c);
  oak_compiler_compile_method_bodies(c);   CHECK_ERROR(c);
  populate_module_exports(c);
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
  struct oak_chunk_t* chunk = compiler_init(&compiler, out);
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
    oak_compiler_register_native_types(&compiler, opts);
    if (compiler.has_error)
    {
      oak_chunk_free(chunk);
      compiler_teardown(&compiler);
      return;
    }
  }

  /* Register native functions and methods after types (receiver ids need to
   * be in the record registry first). */
  if (opts && opts->native_fns.count > 0)
  {
    oak_compiler_register_native_fns(&compiler, opts);
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
