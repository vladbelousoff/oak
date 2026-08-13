#include "internal/oak_compiler.h"

const oak_ast_node_t* oak_unwrap_decl(const oak_ast_node_t* item)
{
  if (!item)
    return item;

  if (item->kind == OAK_NODE_EXPORT_DECL)
    return oak_unwrap_decl(item->child);

  if (item->kind != OAK_NODE_ATTR_DECL)
    return item;

  /* ATTR_DECL is a sequence node whose children are:
   *   ATTR ATTR* <declaration>
   * Walk the children and return the first non-ATTR child. */
  const oak_ast_node_t* decl = null;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &item->children)
  {
    const oak_ast_node_t* child =
        oak_container_of(pos, oak_ast_node_t, link);
    if (child->kind != OAK_NODE_ATTR)
      return oak_unwrap_decl(child);
  }
  return decl;
}

int oak_decl_is_exported(const oak_ast_node_t* item)
{
  if (!item)
    return 0;
  if (item->kind == OAK_NODE_EXPORT_DECL)
    return 1;
  if (item->kind != OAK_NODE_ATTR_DECL)
    return 0;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &item->children)
  {
    const oak_ast_node_t* child =
        oak_container_of(pos, oak_ast_node_t, link);
    if (child->kind != OAK_NODE_ATTR)
      return oak_decl_is_exported(child);
  }
  return 0;
}

const char** oak_extract_attrs(oak_allocator_t* allocator,
                                const oak_ast_node_t* item,
                                int* out_count)
{
  *out_count = 0;
  if (item && item->kind == OAK_NODE_EXPORT_DECL)
    return null;
  if (!item || item->kind != OAK_NODE_ATTR_DECL)
    return null;

  int count = 0;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &item->children)
  {
    const oak_ast_node_t* child =
        oak_container_of(pos, oak_ast_node_t, link);
    if (child->kind == OAK_NODE_ATTR)
      ++count;
  }

  if (count == 0)
    return null;

  const char** arr = OAK_ALLOC(allocator, (usize)count * sizeof(const char*));
  if (!arr)
    return null;

  int i = 0;
  oak_list_for_each(pos, &item->children)
  {
    const oak_ast_node_t* child =
        oak_container_of(pos, oak_ast_node_t, link);
    if (child->kind != OAK_NODE_ATTR)
      break;
    arr[i++] = oak_token_text(child->child->token);
  }

  *out_count = count;
  return arr;
}

const char** oak_alloc_attrs(oak_allocator_t* allocator,
                              const char* const* names,
                              int count)
{
  if (count <= 0)
    return null;
  const char** arr = OAK_ALLOC(allocator, (usize)count * sizeof(const char*));
  if (!arr)
    return null;
  for (int i = 0; i < count; ++i)
    arr[i] = names[i];
  return arr;
}

void oak_compiler_dispatch_attr_cbs(oak_compiler_t* c,
                                    const char** attrs,
                                    int attr_count,
                                    const char* decl_name,
                                    oak_attr_target_t target,
                                    const oak_attr_param_info_t* params,
                                    int param_count,
                                    const oak_attr_field_info_t* fields,
                                    int field_count,
                                    int const_index)
{
  oak_dispatch_compile_attr_cbs(c->opts, attrs, attr_count, decl_name, target,
                                params, param_count, fields, field_count,
                                const_index);

  /* Register whatever the callbacks just bound so the new types/methods are
   * visible to the code that follows. Both passes resume from their cursors,
   * so this only processes bindings added during this dispatch. */
  oak_compiler_report_bind_errors(c, c->opts);
  if (c->has_error)
    return;
  const usize records_before = oak_size(c->records.entries);
  oak_register_native_types(c, c->opts);
  if (!c->has_error)
    oak_register_native_fns(c, c->opts);
  if (c->has_error)
    return;

  /* The acyclicity analysis ran at step 3 of the pipeline, sized to the record
   * registry as it stood then. Anything bound just now is both unchecked by it
   * and indexed past the end of its matrix, so redo it at the new size. The
   * program root is only used to locate a field declaration for an error
   * message, and is not reachable here; a violation found on this pass reports
   * without a source location rather than not at all. */
  if (oak_size(c->records.entries) != records_before)
    oak_compiler_check_cycles(c, null);
}

void oak_apply_runtime_attr_hook(oak_compiler_t* c,
                                  oak_obj_fn_t* fn_obj,
                                  oak_obj_native_fn_t* native_obj,
                                  const char** attrs,
                                  int attr_count)
{
  if (!c->opts)
    return;
  oak_apply_attr_hooks(c->opts, fn_obj, native_obj, attrs, attr_count);
}
