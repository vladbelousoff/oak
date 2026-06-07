#include "internal/oak_compiler.h"

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
    const int name_len = oak_token_size(ident->token);
    if (oak_htable_get(&c->module_scope_names, name, name_len) < 0)
      oak_htable_insert(&c->module_scope_names, name, name_len, 1);
  }
}

static int item_emits_top_level_code(const struct oak_ast_node_t* item)
{
  switch (item->kind)
  {
  case OAK_NODE_FN_DECL:
  case OAK_NODE_RECORD_DECL:
  case OAK_NODE_RECORD_DECL_EMPTY:
  case OAK_NODE_ENUM_DECL:
  case OAK_NODE_IMPORT_SELECTIVE:
  case OAK_NODE_IMPORT_WILDCARD:
  case OAK_NODE_TRAIT_DECL:
  case OAK_NODE_METHOD_DECL:
  case OAK_NODE_ATTR_DECL:
    return 0;
  default:
    return 1;
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
    if (!item_emits_top_level_code(item))
      continue;

    oak_compiler_compile_node(c, item);
    if (c->has_error)
    {
      c->has_error = 0;
      c->scope.stack_depth = 0;
    }
  }
}

static void register_builtin_symbols(struct oak_compiler_t* c)
{
  oak_register_native_builtins(c);
  CHECK_ERROR(c);
  oak_register_array_methods(c);
  CHECK_ERROR(c);
  oak_register_map_methods(c);
  CHECK_ERROR(c);
  oak_register_string_methods(c);
  CHECK_ERROR(c);
  oak_register_bool_methods(c);
  CHECK_ERROR(c);
  oak_register_number_methods(c);
  CHECK_ERROR(c);
  oak_register_record_methods(c);
}

static void register_type_symbols(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* program)
{
  oak_resolve_new_style_imports(c, program);
  CHECK_ERROR(c);

  c->user_enum_start = oak_dynarr_count(c->enums.variants);
  oak_register_program_enums(c, program);
  CHECK_ERROR(c);

  c->user_record_start = oak_dynarr_count(c->records.entries);
  oak_register_program_records(c, program);
  CHECK_ERROR(c);

  c->user_trait_start = oak_dynarr_count(c->traits.traits);
  oak_register_program_traits(c, program);
}

static void register_callable_symbols(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* program)
{
  oak_register_program_fns(c, program);
  CHECK_ERROR(c);
  oak_register_program_methods(c, program);
  CHECK_ERROR(c);
  oak_register_method_decls(c, program);
}

static void emit_deferred_bodies(struct oak_compiler_t* c,
                                 const struct oak_ast_node_t* program)
{
  for (int i = 0; i < c->scope.local_count; ++i)
    oak_chunk_end_debug_local(c->chunk, c->scope.locals[i].slot);
  oak_compiler_emit_op(c, OAK_OP_HALT, OAK_LOC_SYNTHETIC);
  oak_compile_fn_bodies(c);
  CHECK_ERROR(c);
  oak_compile_method_bodies(c);
  CHECK_ERROR(c);
  oak_compile_method_decl_bodies(c, program);
}

int oak_compiler_register_native_options(struct oak_compiler_t* c,
                                 const struct oak_compile_options_t* opts)
{
  if (!opts)
    return 1;

  if (oak_dynarr_count(opts->native_types) > 0)
  {
    oak_register_native_types(c, opts);
    if (c->has_error)
      return 0;
  }

  if (oak_dynarr_count(opts->native_fns) > 0 || oak_dynarr_count(opts->native_global_fns) > 0)
  {
    oak_register_native_fns(c, opts);
    if (c->has_error)
      return 0;
  }

  if (oak_dynarr_count(opts->native_enums) > 0)
  {
    oak_register_native_enums(c, opts);
    if (c->has_error)
      return 0;
  }

  return 1;
}

void oak_compiler_compile_program(struct oak_compiler_t* c,
                          const struct oak_ast_node_t* program)
{
  register_builtin_symbols(c);
  CHECK_ERROR(c);

  register_type_symbols(c, program);
  CHECK_ERROR(c);

  register_callable_symbols(c, program);
  CHECK_ERROR(c);

  collect_module_scope_names(c, program);
  compile_program_items(c, program);
  CHECK_ERROR(c);

  emit_deferred_bodies(c, program);
  CHECK_ERROR(c);

  oak_populate_module_exports(c);
  oak_compiler_move_types_to_module(c);
}
