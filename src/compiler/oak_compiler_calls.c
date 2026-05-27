#include "internal/oak_compiler.h"

static void oakc_compile_indirect_call(struct oak_compiler_t* c,
                                       const struct oak_ast_node_t* node,
                                       const struct oak_ast_node_t* callee)
{
  const struct oak_list_entry_t* first = node->children.next;
  const usize argc = oakc_child_count(node) - 1;
  const struct oak_code_loc_t call_loc =
      callee->token ? oak_compiler_loc_from_token(callee->token)
                    : OAK_LOC_SYNTHETIC;

  oak_compiler_compile_node(c, callee);
  if (c->has_error)
    return;

  struct oak_list_entry_t* pos;
  for (pos = first->next; pos != &node->children; pos = pos->next)
  {
    const struct oak_ast_node_t* arg =
        oak_container_of(pos, struct oak_ast_node_t, link);
    oakc_compile_call_arg(c, arg);
  }

  oak_compiler_emit_op(c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)argc));
  c->scope.stack_depth -= (int)argc;
}

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
    oakc_compile_method_call(c, node, callee);
    return;
  }

  if (callee && callee->kind == OAK_NODE_IDENT)
  {
    const int callee_len = oak_token_size(callee->token);
    const char* callee_name = oak_token_text(callee->token);

    const struct oak_registered_fn_t* entry =
        oakc_find_fn(c, callee_name, callee_len);

    if (entry)
    {
      const struct oak_code_loc_t call_loc =
          oak_compiler_loc_from_token(callee->token);
      const usize argc = oakc_child_count(node) - 1;

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

      oakc_check_fn_args(c, node, entry);
      if (c->has_error)
        return;

      if (entry->source_module_id != OAK_MODULE_ID_NONE)
      {
        oak_compiler_emit_op(c,
                             OAK_OP_GET_MODULE_FN,
                             call_loc,
                             OAK_ARG_U16(entry->source_module_id),
                             OAK_ARG_U16(entry->source_const_idx));
      }
      else
      {
        oak_compiler_emit_constant(c, entry->const_idx, call_loc);
      }

      struct oak_list_entry_t* pos;
      int arg_idx = 0;
      for (pos = first->next; pos != &node->children;
           pos = pos->next, ++arg_idx)
      {
        const struct oak_ast_node_t* arg =
            oak_container_of(pos, struct oak_ast_node_t, link);

        int compiled = 0;
        if (entry->decl)
        {
          const struct oak_ast_node_t* param =
              oakc_fn_param_at(entry->decl, arg_idx);
          if (param)
          {
            const struct oak_ast_node_t* type_node =
                oakc_fn_param_type_node(param);
            if (type_node)
            {
              struct oak_type_t want;
              oakc_lower_type_node(c, type_node, &want);
              oakc_compile_call_arg_for_type(c, arg, want, call_loc);
              compiled = 1;
              if (c->has_error)
                return;
            }
          }
        }
        else if (entry->param_types && arg_idx < entry->arity &&
                 oak_type_is_known(&entry->param_types[arg_idx]))
        {
          oakc_compile_call_arg_for_type(
              c, arg, entry->param_types[arg_idx], call_loc);
          compiled = 1;
          if (c->has_error)
            return;
        }
        if (!compiled)
          oakc_compile_call_arg(c, arg);
      }

      oak_compiler_emit_op(
          c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)argc));
      c->scope.stack_depth -= (int)argc;
      return;
    }

    const int slot = oak_compiler_find_local(c, callee_name, null);
    if (slot >= 0)
    {
      oakc_compile_indirect_call(c, node, callee);
      return;
    }

    oak_compiler_error_at(
        c, callee->token, "undefined function '%s'", callee_name);
    return;
  }

  oakc_compile_indirect_call(c, node, callee);
}
