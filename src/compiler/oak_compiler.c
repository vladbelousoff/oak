#include "internal/oak_compiler.h"
#include "oak_allocator.h"

void oak_compile_ex(const oak_ast_node_t* root,
                    const oak_compile_options_t* opts,
                    oak_compile_result_t* out)
{
  oak_compiler_t compiler = { 0 };
  oak_allocator_t* allocator =
      (opts && opts->allocator) ? opts->allocator : &oak_system_allocator;
  oak_chunk_t* chunk = oak_compiler_init(&compiler, out, allocator);

  oak_compiler_configure(&compiler, opts);
  if (!opts || opts->emit_debug_info)
    oak_chunk_enable_debug(chunk, opts ? opts->source_name : null);

  if (!root || root->kind != OAK_NODE_PROGRAM)
  {
    oak_compiler_error_at(&compiler, null, "expected a program root");
    oak_chunk_free(chunk);
    oak_compiler_teardown(&compiler);
    return;
  }

  if (!oak_compiler_register_native_options(&compiler, opts))
  {
    oak_chunk_free(chunk);
    oak_compiler_teardown(&compiler);
    return;
  }

  oak_compiler_compile_program(&compiler, root);
  oak_compiler_teardown(&compiler);
  if (out->error_count > 0)
  {
    oak_chunk_free(chunk);
    return;
  }

  out->chunk = chunk;
}

void oak_compile_result_free(oak_compile_result_t* result)
{
  if (result && result->chunk)
  {
    oak_chunk_free(result->chunk);
    result->chunk = null;
  }
}
