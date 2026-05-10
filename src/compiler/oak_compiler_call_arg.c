#include "internal/oak_compiler.h"

void oc_compile_call_arg(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* arg)
{
  if (arg->kind == OAK_NODE_FN_CALL_ARG)
    oak_compiler_compile_node(c, arg->child);
  else
    oak_compiler_compile_node(c, arg);
}

/* Children: callee, then each argument. */
void oak_compiler_compile_call_args_after_callee(struct oak_compiler_t* c,
                                                 const struct oak_ast_node_t* call)
{
  const struct oak_list_entry_t* first = call->children.next;
  for (struct oak_list_entry_t* pos = first->next; pos != &call->children;
       pos = pos->next)
  {
    const struct oak_ast_node_t* arg =
        oak_container_of(pos, struct oak_ast_node_t, link);
    oc_compile_call_arg(c, arg);
  }
}

const struct oak_ast_node_t*
oak_compiler_fn_call_arg_expr_at(const struct oak_ast_node_t* call,
                                 const usize index)
{
  const struct oak_list_entry_t* first = call->children.next;
  if (first == &call->children)
    return null;
  const struct oak_list_entry_t* pos = first->next;
  usize i = 0;
  for (; pos != &call->children; pos = pos->next, ++i)
  {
    if (i != index)
      continue;
    const struct oak_ast_node_t* arg =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (arg->kind == OAK_NODE_FN_CALL_ARG)
      return arg->child;
    return arg;
  }
  return null;
}
