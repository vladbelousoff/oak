#include "internal/oak_compiler.h"

void oak_compiler_compile_fn_call(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* node)
{
  const struct oak_list_entry_t* first = node->children.next;
  if (first == &node->children)
  {
    oak_compiler_error_at(c, null, "malformed call (no callee)");
    return;
  }

  const struct oak_ast_node_t* callee =
      oak_container_of(first, struct oak_ast_node_t, link);

  if (callee && callee->kind == OAK_NODE_MEMBER_ACCESS)
  {
    oc_compile_method_call(c, node, callee);
    return;
  }

  if (!callee || callee->kind != OAK_NODE_IDENT)
  {
    oak_compiler_error_at(
        c, callee ? callee->token : null, "callee must be an identifier");
    return;
  }

  const struct oak_code_loc_t call_loc =
      oak_compiler_loc_from_token(callee->token);
  const usize argc = oc_child_count(node) - 1;
  const usize callee_len = oak_token_length(callee->token);
  const char* callee_name = oak_token_text(callee->token);

  const struct oak_registered_fn_t* entry =
      oc_find_fn(c, callee_name, callee_len);
  if (!entry)
  {
    oak_compiler_error_at(
        c, callee->token, "undefined function '%s'", callee_name);
    return;
  }

  if ((int)argc != entry->arity)
  {
    oak_compiler_error_at(c,
                          callee->token,
                          "function '%s' expects %d arguments, got %zu",
                          callee_name,
                          entry->arity,
                          argc);
    return;
  }

  oc_check_fn_args(c, node, entry);
  if (c->has_error)
    return;

  oak_compiler_emit_constant(c, entry->const_idx, call_loc);

  struct oak_list_entry_t* pos;
  for (pos = first->next; pos != &node->children; pos = pos->next)
  {
    const struct oak_ast_node_t* arg =
        oak_container_of(pos, struct oak_ast_node_t, link);
    oc_compile_call_arg(c, arg);
  }

  oak_compiler_emit_op(c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)argc));
  c->scope.stack_depth -= (int)argc;
}
