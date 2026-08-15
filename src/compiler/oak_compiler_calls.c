#include "internal/oak_compiler.h"

static void oak_compile_indirect_call(oak_compiler_t* c,
                                       const oak_ast_node_t* node,
                                       const oak_ast_node_t* callee)
{
  const oak_list_entry_t* first = node->children.next;
  const usize argc = oak_child_count(node) - 1;
  const oak_code_loc_t call_loc =
      callee->token ? oak_compiler_loc_from_token(callee->token)
                    : OAK_LOC_SYNTHETIC;

  oak_compiler_compile_node(c, callee);
  if (c->has_error)
    return;

  oak_list_entry_t* pos;
  for (pos = first->next; pos != &node->children; pos = pos->next)
  {
    const oak_ast_node_t* arg =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
    oak_compile_call_arg(c, arg);
  }

  OAK_COMPILER_EMIT_OP(c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)argc));
  c->scope.stack_depth -= (int)argc;
}

void oak_compiler_compile_fn_call(oak_compiler_t* c,
                                  const oak_ast_node_t* node)
{
  const oak_list_entry_t* first = node->children.next;
  if (first == &node->children)
  {
    oak_compiler_error_at(c, OAK_NULL, "malformed call (no callee)");
    return;
  }

  const oak_ast_node_t* callee =
      OAK_CONTAINER_OF(first, oak_ast_node_t, link);

  if (callee && callee->kind == OAK_NODE_MEMBER_ACCESS)
  {
    oak_compile_method_call(c, node, callee);
    return;
  }

  if (callee && callee->kind == OAK_NODE_IDENT)
  {
    const char* callee_name = oak_token_text(callee->token);

    const oak_registered_fn_t* entry = oak_find_fn(c, callee_name);

    if (entry)
    {
      const oak_code_loc_t call_loc =
          oak_compiler_loc_from_token(callee->token);
      const usize argc = oak_child_count(node) - 1;

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

      oak_check_fn_args(c, node, entry);
      if (c->has_error)
        return;

      if (entry->source_module_id != OAK_MODULE_ID_NONE)
      {
        OAK_COMPILER_EMIT_OP(c,
                             OAK_OP_GET_MODULE_FN,
                             call_loc,
                             OAK_ARG_U16(entry->source_module_id),
                             OAK_ARG_U16(entry->source_const_idx));
      }
      else
      {
        oak_compiler_emit_constant(c, entry->const_idx, call_loc);
      }

      oak_list_entry_t* pos;
      int arg_idx = 0;
      for (pos = first->next; pos != &node->children;
           pos = pos->next, ++arg_idx)
      {
        const oak_ast_node_t* arg =
            OAK_CONTAINER_OF(pos, oak_ast_node_t, link);

        int compiled = 0;
        if (entry->decl)
        {
          const oak_ast_node_t* param =
              oak_fn_param_at(entry->decl, arg_idx);
          if (param)
          {
            const oak_ast_node_t* type_node =
                oak_fn_param_type_node(param);
            if (type_node)
            {
              oak_type_t want;
              oak_lower_type_node(c, type_node, &want);
              oak_compile_call_arg_for_type(c, arg, want, call_loc);
              compiled = 1;
              if (c->has_error)
                return;
            }
          }
        }
        else if (entry->param_types && arg_idx < entry->arity &&
                 oak_type_is_known(&entry->param_types[arg_idx]))
        {
          oak_compile_call_arg_for_type(
              c, arg, entry->param_types[arg_idx], call_loc);
          compiled = 1;
          if (c->has_error)
            return;
        }
        if (!compiled)
          oak_compile_call_arg(c, arg);
      }

      OAK_COMPILER_EMIT_OP(
          c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)argc));
      c->scope.stack_depth -= (int)argc;
      return;
    }

    const int slot = oak_compiler_find_local(c, callee_name, OAK_NULL);
    if (slot >= 0)
    {
      oak_compile_indirect_call(c, node, callee);
      return;
    }

    oak_compiler_error_at(
        c, callee->token, "undefined function '%s'", callee_name);
    return;
  }

  oak_compile_indirect_call(c, node, callee);
}
