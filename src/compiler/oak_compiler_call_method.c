#include "internal/oak_compiler.h"
#include "oak_compiler_modules.h"

static void emit_method_fn(struct oak_compiler_t* c,
                           const struct oak_registered_fn_t* sm,
                           struct oak_code_loc_t loc)
{
  if (sm->source_module_id != OAK_MODULE_ID_NONE)
    oak_compiler_emit_op(c, OAK_OP_GET_MODULE_FN, loc,
                         OAK_ARG_U16(sm->source_module_id),
                         OAK_ARG_U16(sm->source_const_idx));
  else
    oak_compiler_emit_constant(c, sm->const_idx, loc);
}

/* Compile a call to a builtin method binding (string, bool, number, record).
 * If binding is NULL, emits a compile error. Always returns 1 (handled). */
static int try_compile_builtin_method_call(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* node,
    const struct oak_ast_node_t* receiver,
    const struct oak_ast_node_t* method,
    const struct oak_method_binding_t* binding,
    const char* type_label,
    const char* mname,
    usize user_argc,
    struct oak_code_loc_t call_loc)
{
  if (!binding)
  {
    oak_compiler_error_at(
        c, method->token, "no method '%s' on %s", mname, type_label);
    return 1;
  }
  const int expected_user_argc = binding->total_arity - 1;
  if ((int)user_argc != expected_user_argc)
  {
    oak_compiler_error_at(c,
                          method->token,
                          "method '%s' expects %d arguments, got %zu",
                          mname,
                          expected_user_argc,
                          user_argc);
    return 1;
  }
  if (binding->validate_args)
  {
    struct oak_type_t recv_ty;
    oak_infer_type(c, receiver, &recv_ty);
    binding->validate_args(c, node, recv_ty, method->token);
    if (c->has_error)
      return 1;
  }
  if (binding->mutates_receiver &&
      !oak_compiler_expr_is_mutable_place(c, receiver))
  {
    oak_compiler_error_at(c,
                          receiver->token,
                          "cannot call mutable method on an immutable "
                          "receiver");
    return 1;
  }
  oak_compiler_emit_constant(c, binding->const_idx, call_loc);
  oak_compiler_compile_node(c, receiver);
  oak_compiler_compile_call_args_after_callee(c, node);
  oak_compiler_emit_op(
      c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)binding->total_arity));
  c->scope.stack_depth -= binding->total_arity;
  return 1;
}

/* Compile call arguments with type coercion. Tries AST decl first, then
 * falls back to lowered param_types, then generic compilation.
 * self_offset: 0 for static methods, 1 for instance/trait methods. */
static void compile_typed_call_args(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* call,
                                    const struct oak_ast_node_t* decl,
                                    const struct oak_type_t* param_types,
                                    int arity,
                                    int self_offset,
                                    struct oak_code_loc_t loc)
{
  const struct oak_list_entry_t* first = call->children.next;
  int ai = 0;
  for (struct oak_list_entry_t* p = first->next;
       p != &call->children;
       p = p->next, ++ai)
  {
    const struct oak_ast_node_t* arg =
        oak_container_of(p, struct oak_ast_node_t, link);
    int compiled = 0;
    if (decl)
    {
      const struct oak_ast_node_t* param = oak_fn_param_at(decl, ai);
      if (param)
      {
        const struct oak_ast_node_t* tnode = oak_fn_param_type_node(param);
        if (tnode)
        {
          struct oak_type_t want;
          oak_lower_type_node(c, tnode, &want);
          oak_compile_call_arg_for_type(c, arg, want, loc);
          compiled = 1;
          if (c->has_error)
            return;
        }
      }
    }
    if (!compiled && param_types)
    {
      const int slot = ai + self_offset;
      if (slot < arity && oak_type_is_known(&param_types[slot]))
      {
        oak_compile_call_arg_for_type(c, arg, param_types[slot], loc);
        compiled = 1;
        if (c->has_error)
          return;
      }
    }
    if (!compiled)
      oak_compile_call_arg(c, arg);
  }
}

/* Compile `receiver.method(args...)`. Method calls are dispatched purely
 * statically based on the receiver's compile-time type. The method's
 * native function is pushed as a constant, the receiver is compiled as
 * an implicit first argument, and finally OP_CALL with the full arity
 * is emitted. */
void oak_compile_method_call(struct oak_compiler_t* c,
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

  const struct oak_code_loc_t call_loc =
      oak_compiler_loc_from_token(method->token);
  const usize user_argc = oak_child_count(node) - 1;
  const char* mname = oak_token_text(method->token);

  if (receiver->kind == OAK_NODE_MEMBER_ACCESS && receiver->lhs &&
      receiver->rhs && receiver->lhs->kind == OAK_NODE_IDENT &&
      receiver->rhs->kind == OAK_NODE_IDENT)
  {
    const struct oak_ast_node_t* alias_node = receiver->lhs;
    const struct oak_ast_node_t* type_node = receiver->rhs;
    const struct oak_module_t* dep = null;
    if (oak_compiler_module_export_record(c,
                                          oak_token_text(alias_node->token),
                                          oak_token_text(type_node->token),
                                          &dep))
    {
      const struct oak_registered_record_t* sd =
          oak_records_find(&c->records,
                                           oak_token_text(type_node->token),
                                           oak_token_size(type_node->token));
      const struct oak_registered_fn_t* sm =
          oak_find_record_method(sd, mname, 1);
      if (!sm)
      {
        oak_compiler_error_at(c,
                              method->token,
                              "record '%s.%.*s' has no static method '%s'",
                              dep->dotted_name,
                              oak_token_size(type_node->token),
                              oak_token_text(type_node->token),
                              mname);
        return;
      }
      if ((int)user_argc != sm->arity)
      {
        oak_compiler_error_at(c,
                              method->token,
                              "method '%s' expects %d arguments, got %zu",
                              mname,
                              sm->arity,
                              user_argc);
        return;
      }
      oak_check_method_args(c, node, sm);
      CHECK_ERROR(c);
      emit_method_fn(c, sm, call_loc);
      compile_typed_call_args(c, node, sm->decl,
                              sm->param_types, sm->arity, 0, call_loc);
      if (c->has_error)
        return;
      oak_compiler_emit_op(
          c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)sm->arity));
      c->scope.stack_depth -= sm->arity;
      return;
    }
  }

  if (receiver->kind == OAK_NODE_IDENT)
  {
    const char* rname = oak_token_text(receiver->token);
    const int rlen = oak_token_size(receiver->token);

    /* alias.fn(args) — cross-module call. */
    const struct oak_module_t* dep = null;
    const struct oak_module_export_fn_t* exp =
        oak_compiler_module_export_fn(c, rname, mname, &dep);
    if (dep && !exp)
    {
      oak_compiler_error_at(c,
                            method->token,
                            "module '%s' has no exported function '%s'",
                            dep->dotted_name,
                            mname);
      return;
    }
    if (exp)
    {
      if ((int)user_argc != exp->arity)
      {
        oak_compiler_error_at(c,
                              method->token,
                              "function '%s.%s' expects %d arguments, got %zu",
                              dep->dotted_name,
                              mname,
                              exp->arity,
                              user_argc);
        return;
      }
      oak_compiler_emit_op(c,
                           OAK_OP_GET_MODULE_FN,
                           call_loc,
                           OAK_ARG_U16(dep->module_id),
                           OAK_ARG_U16(exp->const_idx));
      compile_typed_call_args(c, node, null,
                              exp->param_types, exp->arity, 0, call_loc);
      if (c->has_error)
        return;
      oak_compiler_emit_op(c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)user_argc));
      c->scope.stack_depth -= (int)user_argc;
      return;
    }

    /* TypeName.method(args) — static method: receiver is a type name, not a
     * variable (mod_id < 0 means the name is not an import alias). */
    struct oak_type_t local_ty;
    oak_type_clear(&local_ty);
    if (!oak_local_type_get(c, rname, &local_ty))
    {
      const struct oak_registered_record_t* sd =
          oak_records_find(&c->records, rname, rlen);
      if (sd)
      {
        const struct oak_registered_fn_t* sm =
            oak_find_record_method(sd, mname, 1);
        if (sm)
        {
          if ((int)user_argc != sm->arity)
          {
            oak_compiler_error_at(c,
                                  method->token,
                                  "method '%s' expects %d arguments, got %zu",
                                  mname,
                                  sm->arity,
                                  user_argc);
            return;
          }
          oak_check_method_args(c, node, sm);
          CHECK_ERROR(c);
          emit_method_fn(c, sm, call_loc);
          compile_typed_call_args(c, node, sm->decl,
                                 sm->param_types, sm->arity, 0, call_loc);
          if (c->has_error)
            return;
          oak_compiler_emit_op(
              c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)sm->arity));
          c->scope.stack_depth -= sm->arity;
          return;
        }
      }
    }
  }

  struct oak_type_t recv_ty;
  oak_infer_type(c, receiver, &recv_ty);

  /* Virtual dispatch through a trait object. */
  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_TRAIT)
  {
    const struct oak_registered_trait_t* tr =
        oak_trait_find_by_id(&c->traits, recv_ty.id);
    if (!tr)
    {
      oak_compiler_error_at(
          c, method->token, "unknown trait type for receiver");
      return;
    }
    const int slot = oak_trait_method_slot(tr, mname);
    if (slot < 0)
    {
      oak_compiler_error_at(c,
                            method->token,
                            "trait '%s' has no method '%s'",
                            tr->name,
                            mname);
      return;
    }
    const int expected_user = tr->methods[slot].arity - 1;
    if ((int)user_argc != expected_user)
    {
      oak_compiler_error_at(c,
                            method->token,
                            "method '%s' expects %d arguments, got %zu",
                            mname,
                            expected_user,
                            user_argc);
      return;
    }
    const struct oak_trait_method_t* tm = &tr->methods[slot];
    int self_mut = 0;
    if (tm->sig_decl)
    {
      const struct oak_ast_node_t* self_p = oak_fn_self_param(tm->sig_decl);
      self_mut = self_p && oak_self_is_mut(self_p);
    }
    else
    {
      self_mut = tm->self_is_mut;
    }
    if (self_mut && !oak_compiler_expr_is_mutable_place(c, receiver))
    {
      oak_compiler_error_at(c,
                            receiver->token,
                            "cannot call mutable method on an immutable "
                            "receiver");
      return;
    }
    oak_check_trait_method_args(c, node, tm);
    if (c->has_error)
      return;
    const u8 total_arity = (u8)tm->arity;
    oak_compiler_compile_node(c, receiver);
    compile_typed_call_args(c, node, tm->sig_decl,
                            tm->param_types, tm->arity, 1, call_loc);
    if (c->has_error)
      return;
    oak_compiler_emit_op(c,
                         OAK_OP_CALL_VIRTUAL,
                         call_loc,
                         OAK_ARG_U8((u8)slot),
                         OAK_ARG_U8(total_arity));
    c->scope.stack_depth -= (int)(total_arity - 1u);
    return;
  }

  /* Record method calls dispatch to a regular user fn whose first
   * parameter is the receiver (`self`). */
  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR)
  {
    const struct oak_registered_record_t* sd =
        oak_records_find_by_id(&c->records, recv_ty.id);
    if (sd)
    {
      const struct oak_registered_fn_t* sm =
          oak_find_record_method(sd, mname, 0);
      if (sm)
      {
        const int expected_user = sm->arity - 1;
        if ((int)user_argc != expected_user)
        {
          oak_compiler_error_at(c,
                                method->token,
                                "method '%s' expects %d arguments, got %zu",
                                mname,
                                expected_user,
                                user_argc);
          return;
        }

        oak_check_method_args(c, node, sm);
        if (c->has_error)
          return;

        {
          int self_is_mut = 0;
          if (sm->decl)
          {
            const struct oak_ast_node_t* self_p =
                oak_fn_self_param(sm->decl);
            self_is_mut = self_p && oak_self_is_mut(self_p);
          }
          else if (sm->param_mut_flags && !sm->is_static)
            self_is_mut = sm->param_mut_flags[0];
          if (self_is_mut &&
              !oak_compiler_expr_is_mutable_place(c, receiver))
          {
            oak_compiler_error_at(c,
                                  receiver->token,
                                  "cannot call mutable method on an immutable "
                                  "receiver");
            return;
          }
        }

        emit_method_fn(c, sm, call_loc);
        oak_compiler_compile_node(c, receiver);
        compile_typed_call_args(c, node, sm->decl,
                                sm->param_types, sm->arity, 1, call_loc);
        if (c->has_error)
          return;
        oak_compiler_emit_op(
            c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)sm->arity));
        c->scope.stack_depth -= sm->arity;
        return;
      }

      const struct oak_method_binding_t* bm =
          oak_find_record_builtin_method(c, mname);
      if (!bm)
      {
        oak_compiler_error_at(
            c, method->token, "no method '%s' on record '%s'", mname, sd->name);
        return;
      }
      try_compile_builtin_method_call(
          c, node, receiver, method,
          bm, sd->name, mname, user_argc, call_loc);
      return;
    }
  }

  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR &&
      recv_ty.id == OAK_TYPE_STRING)
  {
    try_compile_builtin_method_call(
        c, node, receiver, method,
        oak_find_string_method(c, mname),
        "string", mname, user_argc, call_loc);
    return;
  }

  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR &&
      recv_ty.id == OAK_TYPE_BOOL)
  {
    try_compile_builtin_method_call(
        c, node, receiver, method,
        oak_find_bool_method(c, mname),
        "bool", mname, user_argc, call_loc);
    return;
  }

  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR &&
      recv_ty.id == OAK_TYPE_NUMBER)
  {
    try_compile_builtin_method_call(
        c, node, receiver, method,
        oak_find_number_method(c, mname),
        "number", mname, user_argc, call_loc);
    return;
  }

  if (recv_ty.kind == OAK_TYPE_KIND_SCALAR || !oak_type_is_known(&recv_ty))
  {
    oak_compiler_error_at(
        c,
        receiver->token ? receiver->token : method->token,
        "method '.%s' requires a typed array, map, string, or struct receiver",
        mname);
    return;
  }

  const struct oak_method_binding_t* m =
      recv_ty.kind == OAK_TYPE_KIND_MAP
          ? oak_find_map_method(c, mname)
          : oak_find_array_method(c, mname);
  if (!m)
  {
    oak_compiler_error_at(c,
                          method->token,
                          "no method '%s' on %s '%s'",
                          mname,
                          recv_ty.kind == OAK_TYPE_KIND_MAP ? "map"
                                                            : "array of",
                          oak_type_full_name(c, recv_ty));
    return;
  }

  const int expected_user_argc = m->total_arity - 1;
  if ((int)user_argc != expected_user_argc)
  {
    oak_compiler_error_at(c,
                          method->token,
                          "method '%s' expects %d arguments, got %zu",
                          mname,
                          expected_user_argc,
                          user_argc);
    return;
  }

  if (m->validate_args)
  {
    m->validate_args(c, node, recv_ty, method->token);
    if (c->has_error)
      return;
  }

  if (m->mutates_receiver && !oak_compiler_expr_is_mutable_place(c, receiver))
  {
    oak_compiler_error_at(c,
                          receiver->token,
                          "cannot call mutable method on an immutable "
                          "receiver");
    return;
  }

  oak_compiler_emit_constant(c, m->const_idx, call_loc);
  oak_compiler_compile_node(c, receiver);

  /* For arrays with a trait element type, coerce each user arg. */
  const struct oak_registered_trait_t* _elem_tr =
      recv_ty.kind == OAK_TYPE_KIND_ARRAY
          ? oak_trait_find_by_id(&c->traits, recv_ty.id)
          : null;
  if (_elem_tr)
  {
    const struct oak_type_t _want = { .id = _elem_tr->trait_id,
                                      .kind = OAK_TYPE_KIND_TRAIT };
    const struct oak_list_entry_t* _first = node->children.next;
    for (struct oak_list_entry_t* _p = _first->next;
         _p != &node->children;
         _p = _p->next)
    {
      const struct oak_ast_node_t* _arg =
          oak_container_of(_p, struct oak_ast_node_t, link);
      oak_compile_call_arg(c, _arg);
      const struct oak_ast_node_t* _expr =
          _arg->kind == OAK_NODE_FN_CALL_ARG ? _arg->child : _arg;
      oak_emit_trait_coerce(c, _expr, _want, call_loc);
      if (c->has_error)
        return;
    }
  }
  else
  {
    oak_compiler_compile_call_args_after_callee(c, node);
  }

  oak_compiler_emit_op(
      c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)m->total_arity));
  c->scope.stack_depth -= m->total_arity;
}
