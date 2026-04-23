#include "oak_compiler_internal.h"

void oak_compiler_compile_fn_call_arg(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* arg)
{
  if (arg->kind == OAK_NODE_FN_CALL_ARG)
    oak_compiler_compile_node(c, arg->child);
  else
    oak_compiler_compile_node(c, arg);
}

/* Children: callee, then each argument. */
static void compile_call_args_after_callee(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* call)
{
  const struct oak_list_entry_t* first = call->children.next;
  for (struct oak_list_entry_t* pos = first->next; pos != &call->children;
       pos = pos->next)
  {
    const struct oak_ast_node_t* arg =
        oak_container_of(pos, struct oak_ast_node_t, link);
    oak_compiler_compile_fn_call_arg(c, arg);
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

/* Compile `receiver.method(args...)`. Method calls are dispatched purely
 * statically based on the receiver's compile-time type. The method's
 * native function is pushed as a constant, the receiver is compiled as
 * an implicit first argument, and finally OP_CALL with the full arity
 * is emitted. */
void oak_compiler_compile_method_call(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* node,
                                const struct oak_ast_node_t* callee)
{
  const struct oak_ast_node_t* receiver = callee->lhs;
  const struct oak_ast_node_t* method = callee->rhs;
  if (!receiver || !method || method->kind != OAK_NODE_IDENT)
  {
    oak_compiler_error_at(
        c, callee->token, "method call requires 'receiver.name(...)' form");
    return;
  }

  const struct oak_code_loc_t call_loc = oak_compiler_loc_from_token(method->token);
  const usize user_argc = oak_compiler_ast_child_count(node) - 1;
  const char* mname = oak_token_text(method->token);
  const usize mname_len = oak_token_length(method->token);

  struct oak_type_t recv_ty;
  oak_compiler_infer_expr_static_type(c, receiver, &recv_ty);

  /* Struct method calls dispatch to a regular user function whose first
   * parameter is the receiver (`self`). */
  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR)
  {
    const struct oak_registered_struct_t* sd =
        oak_compiler_find_struct_by_type_id(c, recv_ty.id);
    if (sd)
    {
      const struct oak_struct_method_t* sm = null;
      for (int i = 0; i < sd->method_count; ++i)
      {
        const struct oak_struct_method_t* cand = &sd->methods[i];
        if (oak_name_eq(cand->name, cand->name_len, mname, mname_len))
        {
          sm = cand;
          break;
        }
      }
      if (!sm)
      {
        oak_compiler_error_at(
            c, method->token, "no method '%s' on struct '%s'", mname, sd->name);
        return;
      }
      const int expected_user = sm->arity - 1;
      if ((int)user_argc != expected_user)
      {
        oak_compiler_error_at(
            c, method->token, "method '%s' expects %d arguments, got %zu", mname,
            expected_user, user_argc);
        return;
      }

      oak_compiler_validate_struct_method_call_arg_types(c, node, sm);
      if (c->has_error)
        return;

      if (sm->decl)
      {
        const struct oak_ast_node_t* self_p =
            oak_compiler_fn_decl_self_param(sm->decl);
        if (self_p && oak_compiler_fn_param_self_is_mutable(self_p) &&
            !oak_compiler_expr_is_mutable_place(c, receiver))
        {
          oak_compiler_error_at(c,
                            receiver->token,
                            "cannot call mutable method on an immutable "
                            "receiver");
          return;
        }
      }

      oak_compiler_emit_constant(c, sm->const_idx, call_loc);
      oak_compiler_compile_node(c, receiver);
      compile_call_args_after_callee(c, node);

      oak_compiler_emit_op_arg(c, OAK_OP_CALL, (u8)sm->arity, call_loc);
      c->stack_depth -= sm->arity;
      return;
    }
  }

  if (recv_ty.kind == OAK_TYPE_KIND_SCALAR || !oak_type_is_known(&recv_ty))
  {
    oak_compiler_error_at(
        c, receiver->token ? receiver->token : method->token,
        "method '.%s' requires a typed array, map, or struct receiver", mname);
    return;
  }

  const struct oak_method_binding_t* m =
      recv_ty.kind == OAK_TYPE_KIND_MAP
          ? oak_compiler_find_map_method(c, mname, mname_len)
          : oak_compiler_find_array_method(c, mname, mname_len);
  if (!m)
  {
    oak_compiler_error_at(
        c, method->token, "no method '%s' on %s '%s'", mname,
        recv_ty.kind == OAK_TYPE_KIND_MAP ? "map" : "array of",
        oak_compiler_type_full_name(c, recv_ty));
    return;
  }

  const int expected_user_argc = m->total_arity - 1;
  if ((int)user_argc != expected_user_argc)
  {
    oak_compiler_error_at(
        c, method->token, "method '%s' expects %d arguments, got %zu", mname,
        expected_user_argc, user_argc);
    return;
  }

  if (m->validate_args)
  {
    m->validate_args(c, node, recv_ty, method->token);
    if (c->has_error)
      return;
  }

  oak_compiler_emit_constant(c, m->const_idx, call_loc);
  oak_compiler_compile_node(c, receiver);
  compile_call_args_after_callee(c, node);

  oak_compiler_emit_op_arg(c, OAK_OP_CALL, (u8)m->total_arity, call_loc);
  c->stack_depth -= m->total_arity;
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
    oak_compiler_compile_method_call(c, node, callee);
    return;
  }

  if (!callee || callee->kind != OAK_NODE_IDENT)
  {
    oak_compiler_error_at(
        c, callee ? callee->token : null, "callee must be an identifier");
    return;
  }

  const struct oak_code_loc_t call_loc = oak_compiler_loc_from_token(callee->token);
  const usize argc = oak_compiler_ast_child_count(node) - 1;
  const usize callee_len = oak_token_length(callee->token);
  const char* callee_name = oak_token_text(callee->token);

  const struct oak_registered_fn_t* entry =
      oak_compiler_find_registered_fn_entry(c, callee_name, callee_len);
  if (!entry)
  {
    oak_compiler_error_at(
        c, callee->token, "undefined function '%s'", callee_name);
    return;
  }

  if ((int)argc < entry->arity_min || (int)argc > entry->arity_max)
  {
    if (entry->arity_min == entry->arity_max)
    {
      oak_compiler_error_at(
          c, callee->token, "function '%s' expects %d arguments, got %zu",
          callee_name, entry->arity_min, argc);
    }
    else
    {
      oak_compiler_error_at(
          c, callee->token, "function '%s' expects %d to %d arguments, got %zu",
          callee_name, entry->arity_min, entry->arity_max, argc);
    }
    return;
  }

  oak_compiler_validate_user_fn_call_arg_types(c, node, entry);
  if (c->has_error)
    return;

  oak_compiler_emit_constant(c, entry->const_idx, call_loc);

  struct oak_list_entry_t* pos;
  for (pos = first->next; pos != &node->children; pos = pos->next)
  {
    const struct oak_ast_node_t* arg =
        oak_container_of(pos, struct oak_ast_node_t, link);
    oak_compiler_compile_fn_call_arg(c, arg);
  }

  oak_compiler_emit_op_arg(c, OAK_OP_CALL, (u8)argc, call_loc);
  c->stack_depth -= argc;
}
