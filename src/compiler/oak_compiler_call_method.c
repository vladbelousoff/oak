#include "internal/oak_compiler.h"
#include "internal/oak_compiler_modules.h"

static void emit_method_fn(oak_compiler_t* c,
                           const oak_registered_fn_t* sm,
                           oak_code_loc_t loc)
{
  if (sm->source_module_id != OAK_MODULE_ID_NONE)
    OAK_COMPILER_EMIT_OP(c, OAK_OP_GET_MODULE_FN, loc,
                         OAK_ARG_U16(sm->source_module_id),
                         OAK_ARG_U16(sm->source_const_idx));
  else
    oak_compiler_emit_constant(c, sm->const_idx, loc);
}

static const oak_ast_node_t*
method_name_node(const oak_ast_node_t* method)
{
  if (!method)
    return OAK_NULL;
  if (method->kind == OAK_NODE_IDENT)
    return method;
  return OAK_NULL;
}

static int reject_method_arity(oak_compiler_t* c,
                               const oak_ast_node_t* method,
                               const char* mname,
                               int expected,
                               usize actual)
{
  if ((int)actual == expected)
    return 0;
  oak_compiler_error_at(c,
                        method->token,
                        "method '%s' expects %d arguments, got %zu",
                        mname,
                        expected,
                        actual);
  return 1;
}

static int reject_immutable_method_receiver(
    oak_compiler_t* c,
    const oak_ast_node_t* receiver,
    int method_is_mut)
{
  if (!method_is_mut || oak_compiler_expr_is_mutable_place(c, receiver))
    return 0;
  oak_compiler_error_at(c,
                        receiver->token,
                        "cannot call mutable method on an immutable receiver");
  return 1;
}

static int method_self_is_mut(const oak_ast_node_t* decl,
                              int lowered_self_is_mut)
{
  if (!decl)
    return lowered_self_is_mut;
  return oak_fn_self_is_mut(decl);
}

static void compile_builtin_call_args(oak_compiler_t* c,
                                      const oak_ast_node_t* node,
                                      const oak_type_t* recv_ty,
                                      oak_code_loc_t call_loc)
{
  const oak_registered_interface_t* elem_tr =
      recv_ty && recv_ty->kind == OAK_TYPE_KIND_ARRAY
          ? oak_interface_find_by_id(&c->interfaces, recv_ty->id)
          : OAK_NULL;
  if (!elem_tr)
  {
    oak_compiler_compile_call_args_after_callee(c, node);
    return;
  }

  const oak_type_t want = { .id = elem_tr->interface_id,
                                   .kind = OAK_TYPE_KIND_INTERFACE };
  const oak_list_entry_t* first = node->children.next;
  for (oak_list_entry_t* p = first->next;
       p != &node->children;
       p = p->next)
  {
    const oak_ast_node_t* arg =
        OAK_CONTAINER_OF(p, oak_ast_node_t, link);
    oak_compile_call_arg(c, arg);
    const oak_ast_node_t* expr =
        arg->kind == OAK_NODE_FN_CALL_ARG ? arg->child : arg;
    oak_emit_interface_coerce(c, expr, want, call_loc);
    if (c->has_error)
      return;
  }
}

/* Compile a call to a builtin method binding (string, bool, number, record).
 * If binding is NULL, emits a compile error. Always returns 1 (handled). */
static int try_compile_builtin_method_call(
    oak_compiler_t* c,
    const oak_ast_node_t* node,
    const oak_ast_node_t* receiver,
    const oak_ast_node_t* method,
    const oak_method_binding_t* binding,
    const char* type_label,
    const char* mname,
    usize user_argc,
    oak_code_loc_t call_loc,
    const oak_type_t* known_recv_ty)
{
  if (!binding)
  {
    oak_compiler_error_at(
        c, method->token, "no method '%s' on %s", mname, type_label);
    return 1;
  }
  const int expected_user_argc = binding->total_arity - 1;
  if (reject_method_arity(c, method, mname, expected_user_argc, user_argc))
    return 1;
  if (binding->validate_args)
  {
    oak_type_t recv_ty;
    if (known_recv_ty)
      recv_ty = *known_recv_ty;
    else
      oak_infer_type(c, receiver, &recv_ty);
    binding->validate_args(c, node, recv_ty, method->token);
    if (c->has_error)
      return 1;
  }
  if (reject_immutable_method_receiver(
          c, receiver, binding->mutates_receiver))
    return 1;
  oak_compiler_emit_constant(c, binding->const_idx, call_loc);
  oak_compiler_compile_node(c, receiver);
  compile_builtin_call_args(c, node, known_recv_ty, call_loc);
  if (c->has_error)
    return 1;
  OAK_COMPILER_EMIT_OP(
      c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)binding->total_arity));
  c->scope.stack_depth -= binding->total_arity;
  return 1;
}

/* Compile call arguments with type coercion. Tries AST decl first, then
 * falls back to lowered param_types.
 * self_offset: 0 for static methods, 1 for instance/interface methods. */
static void compile_typed_call_args(oak_compiler_t* c,
                                    const oak_ast_node_t* call,
                                    const oak_ast_node_t* decl,
                                    const oak_type_t* param_types,
                                    int arity,
                                    int self_offset,
                                    oak_code_loc_t loc)
{
  const oak_list_entry_t* first = call->children.next;
  int ai = 0;
  for (oak_list_entry_t* p = first->next;
       p != &call->children;
       p = p->next, ++ai)
  {
    const oak_ast_node_t* arg =
        OAK_CONTAINER_OF(p, oak_ast_node_t, link);
    int compiled = 0;
    if (decl)
    {
      const oak_ast_node_t* param = oak_fn_param_at(decl, ai);
      if (param)
      {
        const oak_ast_node_t* tnode = oak_fn_param_type_node(param);
        if (tnode)
        {
          oak_type_t want;
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

static void check_exported_fn_args(oak_compiler_t* c,
                                   const oak_ast_node_t* call,
                                   const oak_module_export_fn_t* exp)
{
  oak_registered_fn_t tmp = { 0 };
  tmp.arity = exp->arity;
  tmp.receiver_type_id = OAK_TYPE_VOID;
  tmp.param_types = exp->param_types;
  tmp.param_mut_flags = exp->param_mut_flags;
  oak_check_fn_args(c, call, &tmp);
}

static void compile_static_method_call(oak_compiler_t* c,
                                       const oak_ast_node_t* node,
                                       const oak_ast_node_t* method,
                                       const oak_registered_fn_t* sm,
                                       const char* mname,
                                       usize user_argc,
                                       oak_code_loc_t call_loc)
{
  if (reject_method_arity(c, method, mname, sm->arity, user_argc))
    return;
  oak_check_method_args(c, node, sm);
  if (c->has_error)
    return;
  emit_method_fn(c, sm, call_loc);
  compile_typed_call_args(
      c, node, sm->decl, sm->param_types, sm->arity, 0, call_loc);
  if (c->has_error)
    return;
  OAK_COMPILER_EMIT_OP(c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)sm->arity));
  c->scope.stack_depth -= sm->arity;
}

typedef const oak_method_binding_t* (*builtin_method_finder_t)(
    oak_compiler_t* c, const char* name);

typedef struct scalar_builtin_dispatch scalar_builtin_dispatch_t;
struct scalar_builtin_dispatch
{
  oak_type_id_t type_id;
  builtin_method_finder_t find;
  const char* label;
};

static const scalar_builtin_dispatch_t scalar_builtin_dispatch[] = {
  { OAK_TYPE_STRING, oak_find_string_method, "string" },
  { OAK_TYPE_BOOL, oak_find_bool_method, "bool" },
  { OAK_TYPE_NUMBER, oak_find_number_method, "number" },
};

/* Compile `receiver.method(args...)`. Method calls are dispatched purely
 * statically based on the receiver's compile-time type. The method's
 * native function is pushed as a constant, the receiver is compiled as
 * an implicit first argument, and finally OP_CALL with the full arity
 * is emitted. */
void oak_compile_method_call(oak_compiler_t* c,
                                      const oak_ast_node_t* node,
                                      const oak_ast_node_t* callee)
{
  const oak_ast_node_t* receiver = callee->lhs;
  const oak_ast_node_t* method = callee->rhs;
  const oak_ast_node_t* method_name = method_name_node(method);
  if (!receiver || !method_name)
  {
    oak_compiler_error_at(
        c, callee->token, "method call requires 'receiver.name(...)' form");
    return;
  }

  const oak_code_loc_t call_loc =
      oak_compiler_loc_from_token(method_name->token);
  const usize user_argc = oak_child_count(node) - 1;
  const char* mname = oak_token_text(method_name->token);

  if (receiver->kind == OAK_NODE_MEMBER_ACCESS && receiver->lhs &&
      receiver->rhs && receiver->lhs->kind == OAK_NODE_IDENT &&
      receiver->rhs->kind == OAK_NODE_IDENT)
  {
    const oak_ast_node_t* alias_node = receiver->lhs;
    const oak_ast_node_t* type_node = receiver->rhs;
    const oak_module_t* dep = OAK_NULL;
    if (oak_compiler_module_export_record(c,
                                          oak_token_text(alias_node->token),
                                          oak_token_text(type_node->token),
                                          &dep))
    {
      const oak_registered_record_t* sd =
          oak_records_find(&c->records, oak_token_text(type_node->token));
      const oak_registered_fn_t* sm =
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
      compile_static_method_call(
          c, node, method, sm, mname, user_argc, call_loc);
      return;
    }
  }

  if (receiver->kind == OAK_NODE_IDENT)
  {
    const char* rname = oak_token_text(receiver->token);

    /* alias.fn(args) — cross-module call. */
    const oak_module_t* dep = OAK_NULL;
    const oak_module_export_fn_t* exp =
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
      check_exported_fn_args(c, node, exp);
      if (c->has_error)
        return;
      OAK_COMPILER_EMIT_OP(c,
                           OAK_OP_GET_MODULE_FN,
                           call_loc,
                           OAK_ARG_U16(dep->module_id),
                           OAK_ARG_U16(exp->const_idx));
      compile_typed_call_args(c, node, OAK_NULL,
                              exp->param_types, exp->arity, 0, call_loc);
      if (c->has_error)
        return;
      OAK_COMPILER_EMIT_OP(c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)user_argc));
      c->scope.stack_depth -= (int)user_argc;
      return;
    }

    /* TypeName.method(args) — static method: receiver is a type name, not a
     * variable (mod_id < 0 means the name is not an import alias). */
    oak_type_t local_ty;
    oak_type_clear(&local_ty);
    if (!oak_local_type_get(c, rname, &local_ty))
    {
      const oak_registered_record_t* sd =
          oak_records_find(&c->records, rname);
      if (sd)
      {
        const oak_registered_fn_t* sm =
            oak_find_record_method(sd, mname, 1);
        if (sm)
        {
          compile_static_method_call(
              c, node, method, sm, mname, user_argc, call_loc);
          return;
        }
      }
    }
  }

  oak_type_t recv_ty;
  oak_infer_type(c, receiver, &recv_ty);

  /* Virtual dispatch through an interface object. */
  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_INTERFACE)
  {
    const oak_registered_interface_t* tr =
        oak_interface_find_by_id(&c->interfaces, recv_ty.id);
    if (!tr)
    {
      oak_compiler_error_at(
          c, method->token, "unknown interface type for receiver");
      return;
    }
    const int slot = oak_interface_method_slot(tr, mname);
    if (slot < 0)
    {
      oak_compiler_error_at(c,
                            method->token,
                            "interface '%s' has no method '%s'",
                            tr->name,
                            mname);
      return;
    }
    const oak_interface_method_t* tm =
        oak_cget(tr->methods, (usize)slot);
    const int expected_user = tm->arity - 1;
    if (reject_method_arity(c, method, mname, expected_user, user_argc))
      return;
    if (reject_immutable_method_receiver(
            c, receiver, method_self_is_mut(tm->sig_decl, tm->self_is_mut)))
      return;
    oak_check_interface_method_args(c, node, tm);
    if (c->has_error)
      return;
    const u8 total_arity = (u8)tm->arity;
    oak_compiler_compile_node(c, receiver);
    compile_typed_call_args(c, node, tm->sig_decl,
                            tm->param_types, tm->arity, 1, call_loc);
    if (c->has_error)
      return;
    OAK_COMPILER_EMIT_OP(c,
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
    const oak_registered_record_t* sd =
        oak_records_find_by_id(&c->records, recv_ty.id);
    if (sd)
    {
      const oak_registered_fn_t* sm =
          oak_find_record_method(sd, mname, 0);
      if (sm)
      {
        const int expected_user = sm->arity - 1;
        if (reject_method_arity(c, method, mname, expected_user, user_argc))
          return;

        oak_check_method_args(c, node, sm);
        if (c->has_error)
          return;

        const int lowered_self_is_mut =
            sm->param_mut_flags && !sm->is_static ? sm->param_mut_flags[0] : 0;
        if (reject_immutable_method_receiver(
                c,
                receiver,
                method_self_is_mut(sm->decl, lowered_self_is_mut)))
          return;

        emit_method_fn(c, sm, call_loc);
        oak_compiler_compile_node(c, receiver);
        compile_typed_call_args(c, node, sm->decl,
                                sm->param_types, sm->arity, 1, call_loc);
        if (c->has_error)
          return;
        OAK_COMPILER_EMIT_OP(
            c, OAK_OP_CALL, call_loc, OAK_ARG_U8((u8)sm->arity));
        c->scope.stack_depth -= sm->arity;
        return;
      }

      const oak_method_binding_t* bm =
          oak_find_record_builtin_method(c, mname);
      if (!bm)
      {
        oak_report_no_record_method(c, method->token, sd, mname);
        return;
      }
      try_compile_builtin_method_call(
          c, node, receiver, method,
          bm, sd->name, mname, user_argc, call_loc, OAK_NULL);
      return;
    }
  }

  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR)
  {
    for (usize i = 0;
         i < sizeof scalar_builtin_dispatch / sizeof scalar_builtin_dispatch[0];
         ++i)
    {
      const scalar_builtin_dispatch_t* dispatch =
          &scalar_builtin_dispatch[i];
      if (recv_ty.id != dispatch->type_id)
        continue;
      try_compile_builtin_method_call(c,
                                      node,
                                      receiver,
                                      method,
                                      dispatch->find(c, mname),
                                      dispatch->label,
                                      mname,
                                      user_argc,
                                      call_loc,
                                      OAK_NULL);
      return;
    }
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

  const oak_method_binding_t* m =
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

  try_compile_builtin_method_call(c,
                                  node,
                                  receiver,
                                  method,
                                  m,
                                  OAK_NULL,
                                  mname,
                                  user_argc,
                                  call_loc,
                                  &recv_ty);
}
