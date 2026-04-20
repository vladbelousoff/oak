#include "oak_compiler_internal.h"

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
    /* Struct declarations are processed in a pre-pass; they don't emit code. */
    if (item->kind == OAK_NODE_STRUCT_DECL)
      continue;
    oak_compiler_compile_node(c, item);
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
  /* Structs must be registered before functions so that function parameter
   * types can refer to user-defined structs. */
  oak_compiler_register_program_structs(c, program);
  if (c->has_error)
    return;
  oak_compiler_register_program_functions(c, program);
  if (c->has_error)
    return;
  compile_program_items(c, program);
  if (c->has_error)
    return;
  oak_compiler_emit_op(c, OAK_OP_HALT, OAK_LOC_SYNTHETIC);
  oak_compiler_compile_function_bodies(c);
}

struct oak_chunk_t* oak_compile(const struct oak_ast_node_t* root)
{
  struct oak_chunk_t* chunk =
      oak_alloc(sizeof(struct oak_chunk_t), OAK_SRC_LOC);
  oak_chunk_init(chunk);

  struct oak_compiler_t compiler = {
    .chunk = chunk,
    .local_count = 0,
    .scope_depth = 0,
    .stack_depth = 0,
    .has_error = 0,
    .current_loop = null,
    .function_depth = 0,
    .fn_registry_count = 0,
    .array_method_count = 0,
    .map_method_count = 0,
  };
  oak_type_registry_init(&compiler.type_registry);

  if (!root || root->kind != OAK_NODE_PROGRAM)
  {
    oak_compiler_error_at(&compiler, null, "expected a program root");
    oak_chunk_free(chunk);
    return null;
  }

  compile_program(&compiler, root);

  if (compiler.has_error)
  {
    oak_chunk_free(chunk);
    return null;
  }

  return chunk;
}
