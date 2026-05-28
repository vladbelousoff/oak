#include "internal/oak_compiler.h"

#include <stdio.h>

void oak_compile_expr_fn(struct oak_compiler_t* c,
                          const struct oak_ast_node_t* node)
{
  const int arity = oak_count_fn_params(node);

  char name_buf[32];
  snprintf(name_buf, sizeof(name_buf), "__anon_%d", c->anon_fn_count++);
  const int name_len = (int)strlen(name_buf);
  char* name_copy = OAK_ALLOC(c->allocator, (usize)(name_len + 1));
  memcpy(name_copy, name_buf, (usize)(name_len + 1));

  const u16 mid =
      c->current_module ? c->current_module->module_id : (u16)0xFFFFu;
  struct oak_obj_fn_t* fn_obj = oak_fn_new(c->allocator, 0, arity, mid);
  fn_obj->name = name_copy;
  const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));

  struct oak_registered_fn_t entry = { 0 };
  entry.name = name_copy;
  entry.name_len = name_len;
  entry.const_idx = idx;
  entry.arity = arity;
  entry.decl = node;
  entry.source_module_id = OAK_MODULE_ID_NONE;
  oak_fn_registry_insert(&c->fns, &entry);

  oak_compiler_emit_constant(c, idx, OAK_LOC_SYNTHETIC);
}
