#include "internal/oak_compiler.h"
#include "oak_compiler_modules.h"

/* Compile `receiver.method(args...)`. Method calls are dispatched purely
 * statically based on the receiver's compile-time type. The method's
 * native function is pushed as a constant, the receiver is compiled as
 * an implicit first argument, and finally OP_CALL with the full arity
 * is emitted. */
void oakc_compile_method_call(struct oak_compiler_t* c,
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
  const usize user_argc = oakc_child_count(node) - 1;
  const char* mname = oak_token_text(method->token);
  const usize mname_len = oak_token_length(method->token);

  if (receiver->kind == OAK_NODE_MEMBER_ACCESS && receiver->lhs &&
      receiver->rhs && receiver->lhs->kind == OAK_NODE_IDENT &&
      receiver->rhs->kind == OAK_NODE_IDENT)
  {
    const struct oak_ast_node_t* alias_node = receiver->lhs;
    const struct oak_ast_node_t* type_node = receiver->rhs;
    const struct oak_module_t* dep = null;
    if (oak_compiler_module_export_record(c,
                                          oak_token_text(alias_node->token),
                                          oak_token_length(alias_node->token),
                                          oak_token_text(type_node->token),
                                          oak_token_length(type_node->token),
                                          &dep))
    {
      const struct oak_registered_record_t* sd =
          oakc_records_find(&c->records,
                                           oak_token_text(type_node->token),
                                           oak_token_length(type_node->token));
      const struct oak_registered_fn_t* sm =
          oakc_find_record_method(sd, mname, mname_len, 1);
      if (!sm)
      {
        oak_compiler_error_at(c,
                              method->token,
                              "record '%s.%.*s' has no static method '%s'",
                              dep->dotted_name,
                              (int)oak_token_length(type_node->token),
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
      oakc_check_method_args(c, node, sm);
      CHECK_ERROR(c);
      oak_compiler_emit_constant(c, sm->const_idx, call_loc);
      {
        const struct oak_list_entry_t* _first = node->children.next;
        int _ai = 0;
        for (struct oak_list_entry_t* _p = _first->next;
             _p != &node->children;
             _p = _p->next, ++_ai)
        {
          const struct oak_ast_node_t* _arg =
              oak_container_of(_p, struct oak_ast_node_t, link);
          int _compiled = 0;
          if (sm->decl)
          {
            const struct oak_ast_node_t* _param =
                oakc_fn_param_at(sm->decl, _ai);
            if (_param)
            {
              const struct oak_ast_node_t* _tnode =
                  oakc_fn_param_type_node(_param);
              if (_tnode)
              {
                struct oak_type_t _want;
                oakc_lower_type_node(c, _tnode, &_want);
                oakc_compile_call_arg_for_type(c, _arg, _want, call_loc);
                _compiled = 1;
                if (c->has_error)
                  return;
              }
            }
          }
          if (!_compiled)
            oakc_compile_call_arg(c, _arg);
        }
      }
      oak_compiler_emit_op(
          c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)sm->arity));
      c->scope.stack_depth -= sm->arity;
      return;
    }
  }

  if (receiver->kind == OAK_NODE_IDENT)
  {
    const char* rname = oak_token_text(receiver->token);
    const usize rlen = oak_token_length(receiver->token);

    /* alias.fn(args) — cross-module call. */
    const struct oak_module_t* dep = null;
    const struct oak_module_export_fn_t* exp =
        oak_compiler_module_export_fn(c, rname, rlen, mname, mname_len, &dep);
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
      oak_compiler_compile_call_args_after_callee(c, node);
      oak_compiler_emit_op(c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)user_argc));
      c->scope.stack_depth -= (int)user_argc;
      return;
    }

    /* TypeName.method(args) — static method: receiver is a type name, not a
     * variable (mod_id < 0 means the name is not an import alias). */
    struct oak_type_t local_ty;
    oak_type_clear(&local_ty);
    if (!oakc_local_type_get(c, rname, rlen, &local_ty))
    {
      const struct oak_registered_record_t* sd =
          oakc_records_find(&c->records, rname, rlen);
      if (sd)
      {
        const struct oak_registered_fn_t* sm =
            oakc_find_record_method(sd, mname, mname_len, 1);
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
          oakc_check_method_args(c, node, sm);
          CHECK_ERROR(c);
          oak_compiler_emit_constant(c, sm->const_idx, call_loc);
          {
            const struct oak_list_entry_t* _first = node->children.next;
            int _ai = 0;
            for (struct oak_list_entry_t* _p = _first->next;
                 _p != &node->children;
                 _p = _p->next, ++_ai)
            {
              const struct oak_ast_node_t* _arg =
                  oak_container_of(_p, struct oak_ast_node_t, link);
              int _compiled = 0;
              if (sm->decl)
              {
                const struct oak_ast_node_t* _param =
                    oakc_fn_param_at(sm->decl, _ai);
                if (_param)
                {
                  const struct oak_ast_node_t* _tnode =
                      oakc_fn_param_type_node(_param);
                  if (_tnode)
                  {
                    struct oak_type_t _want;
                    oakc_lower_type_node(c, _tnode, &_want);
                    oakc_compile_call_arg_for_type(c, _arg, _want, call_loc);
                    _compiled = 1;
                    if (c->has_error)
                      return;
                  }
                }
              }
              if (!_compiled)
                oakc_compile_call_arg(c, _arg);
            }
          }
          oak_compiler_emit_op(
              c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)sm->arity));
          c->scope.stack_depth -= sm->arity;
          return;
        }
      }
    }
  }

  struct oak_type_t recv_ty;
  oakc_infer_type(c, receiver, &recv_ty);

  /* Virtual dispatch through a trait object. */
  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_TRAIT)
  {
    const struct oak_registered_trait_t* tr =
        oakc_trait_find_by_id(&c->traits, recv_ty.id);
    if (!tr)
    {
      oak_compiler_error_at(
          c, method->token, "unknown trait type for receiver");
      return;
    }
    const int slot = oakc_trait_method_slot(tr, mname, mname_len);
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
    const struct oak_ast_node_t* self_p = oakc_fn_self_param(tm->sig_decl);
    if (self_p && oakc_self_is_mut(self_p) &&
        !oak_compiler_expr_is_mutable_place(c, receiver))
    {
      oak_compiler_error_at(c,
                            receiver->token,
                            "cannot call mutable method on an immutable "
                            "receiver");
      return;
    }
    oakc_check_args_against_decl(c, node, tm->sig_decl);
    if (c->has_error)
      return;
    const u8 total_arity = (u8)tm->arity;
    oak_compiler_compile_node(c, receiver);
    oak_compiler_compile_call_args_after_callee(c, node);
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
        oakc_records_find_by_id(&c->records, recv_ty.id);
    if (sd)
    {
      const struct oak_registered_fn_t* sm =
          oakc_find_record_method(sd, mname, mname_len, 0);
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

        oakc_check_method_args(c, node, sm);
        if (c->has_error)
          return;

        if (sm->decl)
        {
          const struct oak_ast_node_t* self_p =
              oakc_fn_self_param(sm->decl);
          if (self_p && oakc_self_is_mut(self_p) &&
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
        {
          const struct oak_list_entry_t* _first = node->children.next;
          int _ai = 0;
          for (struct oak_list_entry_t* _p = _first->next;
               _p != &node->children;
               _p = _p->next, ++_ai)
          {
            const struct oak_ast_node_t* _arg =
                oak_container_of(_p, struct oak_ast_node_t, link);
            int _compiled = 0;
            if (sm->decl)
            {
              const struct oak_ast_node_t* _param =
                  oakc_fn_param_at(sm->decl, _ai);
              if (_param)
              {
                const struct oak_ast_node_t* _tnode =
                    oakc_fn_param_type_node(_param);
                if (_tnode)
                {
                  struct oak_type_t _want;
                  oakc_lower_type_node(c, _tnode, &_want);
                  oakc_compile_call_arg_for_type(c, _arg, _want, call_loc);
                  _compiled = 1;
                  if (c->has_error)
                    return;
                }
              }
            }
            if (!_compiled)
              oakc_compile_call_arg(c, _arg);
          }
        }
        oak_compiler_emit_op(
            c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)sm->arity));
        c->scope.stack_depth -= sm->arity;
        return;
      }

      const struct oak_method_binding_t* bm =
          oakc_find_record_builtin_method(c, mname, mname_len);
      if (!bm)
      {
        oak_compiler_error_at(
            c, method->token, "no method '%s' on record '%s'", mname, sd->name);
        return;
      }
      const int expected_user_argc = bm->total_arity - 1;
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
      if (bm->mutates_receiver &&
          !oak_compiler_expr_is_mutable_place(c, receiver))
      {
        oak_compiler_error_at(c,
                              receiver->token,
                              "cannot call mutable method on an immutable "
                              "receiver");
        return;
      }
      oak_compiler_emit_constant(c, bm->const_idx, call_loc);
      oak_compiler_compile_node(c, receiver);
      oak_compiler_compile_call_args_after_callee(c, node);
      oak_compiler_emit_op(
          c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)bm->total_arity));
      c->scope.stack_depth -= bm->total_arity;
      return;
    }
  }

  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR &&
      recv_ty.id == OAK_TYPE_STRING)
  {
    const struct oak_method_binding_t* sm =
        oakc_find_string_method(c, mname, mname_len);
    if (!sm)
    {
      oak_compiler_error_at(
          c, method->token, "no method '%s' on string", mname);
      return;
    }
    const int expected_user_argc = sm->total_arity - 1;
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

    if (sm->validate_args)
    {
      sm->validate_args(c, node, recv_ty, method->token);
      if (c->has_error)
        return;
    }

    oak_compiler_emit_constant(c, sm->const_idx, call_loc);
    oak_compiler_compile_node(c, receiver);
    oak_compiler_compile_call_args_after_callee(c, node);

    oak_compiler_emit_op(
        c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)sm->total_arity));
    c->scope.stack_depth -= sm->total_arity;
    return;
  }

  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR &&
      recv_ty.id == OAK_TYPE_BOOL)
  {
    const struct oak_method_binding_t* sm =
        oakc_find_bool_method(c, mname, mname_len);
    if (!sm)
    {
      oak_compiler_error_at(c, method->token, "no method '%s' on bool", mname);
      return;
    }
    const int expected_user_argc = sm->total_arity - 1;
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
    oak_compiler_emit_constant(c, sm->const_idx, call_loc);
    oak_compiler_compile_node(c, receiver);
    oak_compiler_compile_call_args_after_callee(c, node);
    oak_compiler_emit_op(
        c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)sm->total_arity));
    c->scope.stack_depth -= sm->total_arity;
    return;
  }

  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR &&
      recv_ty.id == OAK_TYPE_NUMBER)
  {
    const struct oak_method_binding_t* sm =
        oakc_find_number_method(c, mname, mname_len);
    if (!sm)
    {
      oak_compiler_error_at(
          c, method->token, "no method '%s' on number", mname);
      return;
    }
    const int expected_user_argc = sm->total_arity - 1;
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
    oak_compiler_emit_constant(c, sm->const_idx, call_loc);
    oak_compiler_compile_node(c, receiver);
    oak_compiler_compile_call_args_after_callee(c, node);
    oak_compiler_emit_op(
        c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)sm->total_arity));
    c->scope.stack_depth -= sm->total_arity;
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
          ? oakc_find_map_method(c, mname, mname_len)
          : oakc_find_array_method(c, mname, mname_len);
  if (!m)
  {
    oak_compiler_error_at(c,
                          method->token,
                          "no method '%s' on %s '%s'",
                          mname,
                          recv_ty.kind == OAK_TYPE_KIND_MAP ? "map"
                                                            : "array of",
                          oakc_type_full_name(c, recv_ty));
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
          ? oakc_trait_find_by_id(&c->traits, recv_ty.id)
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
      oakc_compile_call_arg(c, _arg);
      const struct oak_ast_node_t* _expr =
          _arg->kind == OAK_NODE_FN_CALL_ARG ? _arg->child : _arg;
      oakc_emit_trait_coerce(c, _expr, _want, call_loc);
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
