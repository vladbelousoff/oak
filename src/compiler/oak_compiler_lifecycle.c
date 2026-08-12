#include "internal/oak_compiler.h"

oak_chunk_t* oak_compiler_init(oak_compiler_t* c,
                                       oak_compile_result_t* out,
                                       oak_allocator_t* allocator)
{
  c->allocator = allocator;

  oak_chunk_t* chunk =
      OAK_ALLOC(allocator, sizeof(oak_chunk_t));
  oak_chunk_init(chunk, allocator);

  oak_type_t no_return_type;
  oak_type_clear(&no_return_type);

  c->chunk = chunk;
  c->result = out;
  c->has_error = 0;
  c->scope = (oak_scope_ctx_t){
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
  c->module_scope_names = oak_hash_set_new(allocator);
  oak_interface_registry_init(&c->interfaces, allocator);
  oak_symbol_registry_init(&c->symbols, allocator);
  c->native_types_cursor = 0;
  c->native_global_fns_cursor = 0;
  c->native_fns_cursor = 0;
  c->cycle_reach = null;
  c->cycle_reach_count = 0;

  return chunk;
}

void oak_compiler_configure(oak_compiler_t* c,
                             const oak_compile_options_t* opts)
{
  c->opts = opts;
  c->allow_bodyless_fns = opts ? opts->allow_bodyless_fns : 0;

  if (opts && opts->module_registry && opts->current_module)
  {
    c->module_registry = opts->module_registry;
    c->current_module = opts->current_module;
    c->chunk->module_id = opts->current_module->module_id;
    oak_type_registry_set_owner(&c->types, opts->current_module->module_id);
  }
}

void oak_compiler_teardown(oak_compiler_t* c)
{
  oak_compiler_free_cycles(c);
  oak_destroy(c->module_scope_names);
  oak_fn_registry_free(&c->fns);
  oak_record_registry_free(&c->records);
  oak_enum_registry_free(&c->enums);
  oak_interface_registry_free(&c->interfaces);
  oak_symbol_registry_free(&c->symbols);
  oak_type_registry_free(&c->types);
}

void oak_compiler_move_types_to_module(oak_compiler_t* c)
{
  if (!c->current_module)
    return;

  c->current_module->types = c->types;
  c->types.entries = null;
}
