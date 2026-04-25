#include "oak_compiler_internal.h"

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

static void compile_program(struct oak_compiler_t* c,
                            const struct oak_ast_node_t* program)
{
  oak_compiler_register_native_builtins(c);
  if (c->has_error)
    return;
  oak_compiler_register_array_methods(c);
  if (c->has_error)
    return;
  oak_compiler_register_map_methods(c);
  if (c->has_error)
    return;
  /* Enums are registered early so their variant names are available as
   * constant references in the rest of the program, including function
   * parameter defaults, record field initializers, etc. */
  oak_compiler_register_program_enums(c, program);
  if (c->has_error)
    return;
  /* Records must be registered before functions so that function parameter
   * types can refer to user-defined records. */
  oak_compiler_register_program_records(c, program);
  if (c->has_error)
    return;
  oak_compiler_register_program_functions(c, program);
  if (c->has_error)
    return;
  oak_compiler_register_program_methods(c, program);
  if (c->has_error)
    return;
  collect_module_scope_names(c, program);
  compile_program_items(c, program);
  if (c->has_error)
    return;
  oak_compiler_emit_op(c, OAK_OP_HALT, OAK_LOC_SYNTHETIC);
  oak_compiler_compile_function_bodies(c);
  if (c->has_error)
    return;
  oak_compiler_compile_method_bodies(c);
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

  if (!root || root->kind != OAK_NODE_PROGRAM)
  {
    oak_compiler_error_at(&compiler, null, "expected a program root");
    oak_chunk_free(chunk);
    compiler_teardown(&compiler);
    return;
  }

  /* Register native types before any source-level passes so Oak source can
   * reference native type names in function signatures, record fields, etc. */
  if (opts && opts->native_type_count > 0)
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
  if (opts && opts->native_fn_count > 0)
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
