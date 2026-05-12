#include "internal/oak_compiler.h"

const struct oak_ast_node_t* oakc_unwrap_decl(const struct oak_ast_node_t* item)
{
  if (!item || item->kind != OAK_NODE_ATTR_DECL)
    return item;

  /* ATTR_DECL is a sequence node whose children are:
   *   ATTR ATTR* <declaration>
   * Walk the children and return the first non-ATTR child. */
  const struct oak_ast_node_t* decl = null;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &item->children)
  {
    const struct oak_ast_node_t* child =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (child->kind != OAK_NODE_ATTR)
    {
      decl = child;
      break;
    }
  }
  return decl;
}

const char** oakc_extract_attrs(const struct oak_ast_node_t* item,
                                int* out_count)
{
  *out_count = 0;
  if (!item || item->kind != OAK_NODE_ATTR_DECL)
    return null;

  int count = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &item->children)
  {
    const struct oak_ast_node_t* child =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (child->kind == OAK_NODE_ATTR)
      ++count;
  }

  if (count == 0)
    return null;

  const char** arr = oak_alloc((usize)count * sizeof(const char*), OAK_SRC_LOC);
  if (!arr)
    return null;

  int i = 0;
  oak_list_for_each(pos, &item->children)
  {
    const struct oak_ast_node_t* child =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (child->kind != OAK_NODE_ATTR)
      break;
    arr[i++] = oak_token_text(child->child->token);
  }

  *out_count = count;
  return arr;
}

const char** oakc_alloc_attrs(const char* const* names, int count)
{
  if (count <= 0)
    return null;
  const char** arr = oak_alloc((usize)count * sizeof(const char*), OAK_SRC_LOC);
  if (!arr)
    return null;
  for (int i = 0; i < count; ++i)
    arr[i] = names[i];
  return arr;
}

void oakc_dispatch_compile_attr_cbs(struct oak_compiler_t* c,
                                    const char** attrs,
                                    int attr_count,
                                    const char* decl_name,
                                    enum oak_attr_target_t target)
{
  oak_dispatch_compile_attr_cbs(c->opts, attrs, attr_count, decl_name, target);
}

void oakc_apply_runtime_attr_hook(struct oak_compiler_t* c,
                                  struct oak_obj_fn_t* fn_obj,
                                  struct oak_obj_native_fn_t* native_obj,
                                  const char** attrs,
                                  int attr_count)
{
  if (!c->opts)
    return;
  oak_apply_attr_hooks(c->opts, fn_obj, native_obj, attrs, attr_count);
}
