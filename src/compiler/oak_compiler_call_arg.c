#include "internal/oak_compiler.h"

void oak_compile_call_arg(oak_compiler_t* c,
                                      const oak_ast_node_t* arg)
{
  if (arg->kind == OAK_NODE_FN_CALL_ARG)
    oak_compiler_compile_node(c, arg->child);
  else
    oak_compiler_compile_node(c, arg);
}

void oak_compile_call_arg_for_type(oak_compiler_t* c,
                                    const oak_ast_node_t* arg,
                                    oak_type_t want,
                                    oak_code_loc_t loc)
{
  oak_compile_call_arg(c, arg);
  const oak_ast_node_t* expr =
      arg->kind == OAK_NODE_FN_CALL_ARG ? arg->child : arg;
  oak_emit_interface_coerce(c, expr, want, loc);
  if (c->has_error)
    return;
  oak_emit_weak_coerce(c, expr, want, loc);
}

/* Children: callee, then each argument. */
void oak_compiler_compile_call_args_after_callee(oak_compiler_t* c,
                                                 const oak_ast_node_t* call)
{
  const oak_list_entry_t* first = call->children.next;
  for (oak_list_entry_t* pos = first->next; pos != &call->children;
       pos = pos->next)
  {
    const oak_ast_node_t* arg =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
    oak_compile_call_arg(c, arg);
  }
}

const oak_ast_node_t*
oak_compiler_fn_call_arg_expr_at(const oak_ast_node_t* call,
                                 const usize index)
{
  const oak_list_entry_t* first = call->children.next;
  if (first == &call->children)
    return OAK_NULL;
  const oak_list_entry_t* pos = first->next;
  usize i = 0;
  for (; pos != &call->children; pos = pos->next, ++i)
  {
    if (i != index)
      continue;
    const oak_ast_node_t* arg =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
    if (arg->kind == OAK_NODE_FN_CALL_ARG)
      return arg->child;
    return arg;
  }
  return OAK_NULL;
}
