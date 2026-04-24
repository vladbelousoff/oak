#include "oak_compiler_internal.h"

static const struct oak_ast_node_t* oak_fn_decl_proto(const struct oak_ast_node_t* decl)
{
  return decl->lhs;
}

static const struct oak_ast_node_t* oak_fn_decl_head(const struct oak_ast_node_t* decl)
{
  return oak_fn_decl_proto(decl)->lhs;
}

static const struct oak_ast_node_t* oak_fn_decl_prefix(const struct oak_ast_node_t* decl)
{
  return oak_fn_decl_head(decl)->lhs;
}

static const struct oak_ast_node_t* oak_fn_decl_params_tail(const struct oak_ast_node_t* decl)
{
  return oak_fn_decl_proto(decl)->rhs;
}

static const struct oak_ast_node_t*
oak_fn_param_list_regular_params(const struct oak_ast_node_t* plist)
{
  /* FN_PARAM_LIST is BINARY: lhs = FN_PARAM_SELF? , rhs = FN_PARAMS. */
  return plist->rhs;
}

int oak_compiler_fn_decl_has_receiver(const struct oak_ast_node_t* decl)
{
  /* FN_PREFIX is UNARY: child = FN_RECEIVER? (null when absent). */
  return oak_fn_decl_prefix(decl)->child != null;
}

const struct oak_ast_node_t*
oak_compiler_fn_decl_param_list(const struct oak_ast_node_t* decl)
{
  /* FN_PARAMS_AND_RET is BINARY: lhs = FN_PARAM_LIST, rhs = FN_RETURN_TYPE?. */
  return oak_fn_decl_params_tail(decl)->lhs;
}

const struct oak_ast_node_t*
oak_compiler_fn_decl_name_node(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* head = oak_fn_decl_head(decl);
  oak_assert(head->rhs != null);
  return head->rhs;
}

const struct oak_ast_node_t*
oak_compiler_fn_decl_self_param(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* plist = oak_compiler_fn_decl_param_list(decl);
  if (!plist)
    return null;
  /* FN_PARAM_LIST is BINARY: lhs = FN_PARAM_SELF? (null when absent). */
  return plist->lhs;
}

int oak_compiler_fn_param_self_is_mutable(const struct oak_ast_node_t* self_param)
{
  /* FN_PARAM_SELF is BINARY: lhs = MUT_KEYWORD? (non-null iff mutable). */
  return self_param->lhs != null;
}

const struct oak_ast_node_t*
oak_compiler_fn_decl_block(const struct oak_ast_node_t* decl)
{
  return decl->rhs;
}

int oak_compiler_fn_param_is_mutable(const struct oak_ast_node_t* param)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &param->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_MUT_KEYWORD)
      return 1;
  }
  return 0;
}

const struct oak_ast_node_t*
oak_compiler_fn_param_ident(const struct oak_ast_node_t* param)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &param->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_IDENT)
      return ch;
  }
  return null;
}

const struct oak_ast_node_t*
oak_compiler_fn_param_type_node(const struct oak_ast_node_t* param)
{
  int ident_seen = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &param->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_MUT_KEYWORD)
      continue;
    if (ch->kind == OAK_NODE_IDENT)
    {
      if (!ident_seen)
      {
        ident_seen = 1;
        continue;
      }
      return ch;
    }
    if (ch->kind == OAK_NODE_TYPE_ARRAY || ch->kind == OAK_NODE_TYPE_MAP)
      return ch;
  }
  return null;
}

const struct oak_ast_node_t*
oak_compiler_fn_decl_param_at(const struct oak_ast_node_t* decl, const int index)
{
  const struct oak_ast_node_t* plist = oak_compiler_fn_decl_param_list(decl);
  if (!plist)
    return null;
  const struct oak_ast_node_t* params = oak_fn_param_list_regular_params(plist);
  if (!params)
    return null;
  int i = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &params->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_FN_PARAM)
    {
      if (i == index)
        return ch;
      ++i;
    }
  }
  return null;
}

const struct oak_ast_node_t*
oak_compiler_fn_decl_return_type_node(const struct oak_ast_node_t* decl)
{
  /* FN_PARAMS_AND_RET is BINARY: rhs = FN_RETURN_TYPE? (null when absent).
   * FN_RETURN_TYPE is UNARY: child = TYPE_NAME. */
  const struct oak_ast_node_t* tail = oak_fn_decl_params_tail(decl);
  if (!tail->rhs)
    return null;
  return tail->rhs->child;
}

int oak_compiler_count_fn_params(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* plist = oak_compiler_fn_decl_param_list(decl);
  if (!plist)
    return 0;
  const struct oak_ast_node_t* params = oak_fn_param_list_regular_params(plist);
  if (!params)
    return 0;
  int n = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &params->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_FN_PARAM)
      ++n;
  }
  return n;
}

static void register_regular_fn_decl(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* item)
{
  const struct oak_ast_node_t* name_node = oak_compiler_fn_decl_name_node(item);
  const char* name = oak_token_text(name_node->token);
  const usize name_len = oak_token_length(name_node->token);
  const int explicit_arity = oak_compiler_count_fn_params(item);
  const struct oak_ast_node_t* self_param = oak_compiler_fn_decl_self_param(item);

  if (self_param)
  {
    const struct oak_ast_node_t* first_child =
        self_param->lhs ? self_param->lhs : self_param->rhs;
    oak_compiler_error_at(
        c, first_child->token,
        "'self' parameter is only valid on methods (use"
        " 'fn TypeName.%s(self, ...)' instead)",
        name);
    return;
  }

  for (int i = 0; i < c->fn_registry_count; ++i)
  {
    const struct oak_registered_fn_t* e = &c->fn_registry[i];
    if (oak_name_eq(e->name, e->name_len, name, name_len))
    {
      oak_compiler_error_at(
          c, name_node->token, "duplicate function '%s'", name);
      return;
    }
  }

  if (c->fn_registry_count >= OAK_MAX_USER_FNS)
  {
    oak_compiler_error_at(
        c, null, "too many functions in one program (max %d)", OAK_MAX_USER_FNS);
    return;
  }

  struct oak_obj_fn_t* fn_obj = oak_fn_new(0, explicit_arity);
  const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));

  struct oak_registered_fn_t* slot = &c->fn_registry[c->fn_registry_count++];
  slot->name = name;
  slot->name_len = name_len;
  slot->const_idx = idx;
  slot->arity_min = explicit_arity;
  slot->arity_max = explicit_arity;
  slot->decl = item;
}

static void register_method_decl(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* item)
{
  const struct oak_ast_node_t* name_node = oak_compiler_fn_decl_name_node(item);
  const char* name = oak_token_text(name_node->token);
  const usize name_len = oak_token_length(name_node->token);
  const int explicit_arity = oak_compiler_count_fn_params(item);
  const struct oak_ast_node_t* self_param = oak_compiler_fn_decl_self_param(item);

  const struct oak_ast_node_t* recv_node = oak_fn_decl_prefix(item)->child;
  oak_assert(recv_node != null);
  const struct oak_ast_node_t* recv_ident = recv_node->child;
  if (!recv_ident || recv_ident->kind != OAK_NODE_IDENT)
  {
    oak_compiler_error_at(
        c, recv_node->token, "method receiver must be a type name");
    return;
  }

  if (!self_param)
  {
    oak_compiler_error_at(
        c, name_node->token,
        "method '%s' must declare 'self' or 'mut self' as its first"
        " parameter",
        name);
    return;
  }

  const char* sname = oak_token_text(recv_ident->token);
  const usize sname_len = oak_token_length(recv_ident->token);
  struct oak_registered_struct_t* sd = null;
  for (int i = 0; i < c->struct_count; ++i)
  {
    struct oak_registered_struct_t* cand = &c->structs[i];
    if (oak_name_eq(cand->name, cand->name_len, sname, sname_len))
    {
      sd = cand;
      break;
    }
  }
  if (!sd)
  {
    oak_compiler_error_at(
        c, recv_ident->token, "no such struct '%s' for method receiver",
        sname);
    return;
  }

  for (int i = 0; i < sd->method_count; ++i)
  {
    const struct oak_struct_method_t* e = &sd->methods[i];
    if (oak_name_eq(e->name, e->name_len, name, name_len))
    {
      oak_compiler_error_at(
          c, name_node->token, "duplicate method '%s' on struct '%s'", name,
          sd->name);
      return;
    }
  }

  if (sd->method_count >= OAK_MAX_STRUCT_METHODS)
  {
    oak_compiler_error_at(
        c, name_node->token, "too many methods on struct '%s' (max %d)",
        sd->name, OAK_MAX_STRUCT_METHODS);
    return;
  }

  const int total_arity = explicit_arity + 1;
  struct oak_obj_fn_t* fn_obj = oak_fn_new(0, total_arity);
  const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));

  struct oak_struct_method_t* slot = &sd->methods[sd->method_count++];
  slot->name = name;
  slot->name_len = name_len;
  slot->const_idx = idx;
  slot->arity = total_arity;
  slot->decl = item;
}

void oak_compiler_register_program_functions(struct oak_compiler_t* c,
                                             const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind != OAK_NODE_FN_DECL)
      continue;
    if (oak_compiler_fn_decl_has_receiver(item))
      continue;
    register_regular_fn_decl(c, item);
    if (c->has_error)
      return;
  }
}

void oak_compiler_register_program_methods(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind != OAK_NODE_FN_DECL)
      continue;
    if (!oak_compiler_fn_decl_has_receiver(item))
      continue;
    register_method_decl(c, item);
    if (c->has_error)
      return;
  }
}

const struct oak_registered_fn_t* oak_compiler_find_registered_fn_entry(
    struct oak_compiler_t* c, const char* name, const usize len)
{
  for (int i = 0; i < c->fn_registry_count; ++i)
  {
    const struct oak_registered_fn_t* e = &c->fn_registry[i];
    if (oak_name_eq(e->name, e->name_len, name, len))
      return e;
  }
  return null;
}

void oak_compiler_compile_stmt_return(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* node)
{
  if (c->function_depth == 0)
  {
    oak_compiler_error_at(c, null, "'return' outside of a function");
    return;
  }

  /* STMT_RETURN is UNARY: child = EXPR? */
  const struct oak_ast_node_t* expr = node->child;
  if (oak_type_is_void(&c->declared_return_type))
  {
    if (expr)
    {
      oak_compiler_error_at(
          c, expr->token ? expr->token : node->token,
          "void function cannot return a value");
      return;
    }
  }
  else
  {
    if (!expr)
    {
      oak_compiler_error_at(
          c, node->token, "missing return value (function returns a value)");
      return;
    }
    if (oak_type_is_known(&c->declared_return_type))
    {
      struct oak_type_t got;
      oak_compiler_infer_expr_static_type(c, expr, &got);
      if (oak_type_is_known(&got) && !oak_type_equal(&c->declared_return_type, &got))
      {
        oak_compiler_error_at(c,
                              expr->token ? expr->token : node->token,
                              "return type mismatch: expected '%s', found '%s'",
                              oak_compiler_type_full_name(c, c->declared_return_type),
                              oak_compiler_type_full_name(c, got));
      }
    }
    oak_compiler_compile_node(c, expr);
    if (c->has_error)
      return;
    oak_compiler_emit_op(c, OAK_OP_RETURN, OAK_LOC_SYNTHETIC);
    return;
  }

  const u16 z = oak_compiler_intern_constant(c, OAK_VALUE_I32(0));
  oak_compiler_emit_constant(c, z, OAK_LOC_SYNTHETIC);
  oak_compiler_emit_op(c, OAK_OP_RETURN, OAK_LOC_SYNTHETIC);
}

/* If `recv_struct` is non-null, the function is treated as a method: an
 * implicit `self` local is installed at slot 0 with the receiver's static
 * type, and explicit parameters start at slot 1. */
void oak_compiler_compile_function_body(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* decl,
                                  const struct oak_registered_struct_t* recv)
{
  const struct oak_ast_node_t* body = oak_compiler_fn_decl_block(decl);
  if (!body || body->kind != OAK_NODE_BLOCK)
  {
    oak_compiler_error_at(c, decl->token, "function has no body");
    return;
  }

  c->function_depth++;
  c->local_count = 0;
  c->scope_depth = 0;
  c->stack_depth = 0;
  c->current_loop = null;

  /* Return type: omitted `->` means void. */
  oak_type_clear(&c->declared_return_type);
  const struct oak_ast_node_t* ret_type_node = oak_compiler_fn_decl_return_type_node(decl);
  if (ret_type_node)
  {
    oak_compiler_type_node_to_type(c, ret_type_node, &c->declared_return_type);
    if (oak_type_is_void(&c->declared_return_type))
    {
      oak_compiler_error_at(
          c, ret_type_node->token,
          "omit the return type for a function with no value; 'void' is not "
          "allowed after '->'");
      c->function_depth--;
      return;
    }
  }
  else
    c->declared_return_type.id = OAK_TYPE_VOID;

  int slot = 0;
  if (recv)
  {
    const struct oak_ast_node_t* sp = oak_compiler_fn_decl_self_param(decl);
    /* The presence of FN_PARAM_SELF was already checked when the method
     * was registered; treat its absence here as an internal error. */
    oak_assert(sp != null);
    struct oak_type_t self_ty;
    oak_type_clear(&self_ty);
    self_ty.id = recv->type_id;
    oak_compiler_add_local(c, "self", 4u, slot++, oak_compiler_fn_param_self_is_mutable(sp), self_ty);
  }

  const struct oak_ast_node_t* plist = oak_compiler_fn_decl_param_list(decl);
  if (!plist || plist->kind != OAK_NODE_FN_PARAM_LIST)
  {
    oak_compiler_error_at(c, decl->token, "malformed function declaration");
    c->function_depth--;
    return;
  }

  const struct oak_ast_node_t* params = oak_fn_param_list_regular_params(plist);
  if (!params)
  {
    oak_compiler_error_at(c, decl->token, "malformed function declaration");
    c->function_depth--;
    return;
  }

  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &params->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind != OAK_NODE_FN_PARAM)
      continue;
    const struct oak_ast_node_t* id = oak_compiler_fn_param_ident(ch);
    if (!id)
    {
      oak_compiler_error_at(c, ch->token, "malformed function parameter");
      c->function_depth--;
      return;
    }
    const struct oak_ast_node_t* type_id = oak_compiler_fn_param_type_node(ch);
    struct oak_type_t param_type;
    oak_type_clear(&param_type);
    if (type_id)
      oak_compiler_type_node_to_type(c, type_id, &param_type);
    oak_compiler_add_local(c,
              oak_token_text(id->token),
              oak_token_length(id->token),
              slot++,
              oak_compiler_fn_param_is_mutable(ch),
              param_type);
  }

  c->stack_depth = slot;

  oak_compiler_compile_block(c, body);

  const u16 z = oak_compiler_intern_constant(c, OAK_VALUE_I32(0));
  oak_compiler_emit_constant(c, z, OAK_LOC_SYNTHETIC);
  oak_compiler_emit_op(c, OAK_OP_RETURN, OAK_LOC_SYNTHETIC);

  /* Clear the return type so it doesn't apply outside this function. */
  oak_type_clear(&c->declared_return_type);
  c->function_depth--;
}

void oak_compiler_compile_function_bodies(struct oak_compiler_t* c)
{
  for (int i = 0; i < c->fn_registry_count; ++i)
  {
    const struct oak_registered_fn_t* e = &c->fn_registry[i];
    if (!e->decl)
      continue;
    struct oak_value_t fn_val = c->chunk->constants[e->const_idx];
    struct oak_obj_fn_t* fn_obj = oak_as_fn(fn_val);
    fn_obj->code_offset = c->chunk->count;
    oak_compiler_compile_function_body(c, e->decl, null);
    if (c->has_error)
      return;
  }
}

void oak_compiler_compile_method_bodies(struct oak_compiler_t* c)
{
  for (int s = 0; s < c->struct_count; ++s)
  {
    const struct oak_registered_struct_t* sd = &c->structs[s];
    for (int m = 0; m < sd->method_count; ++m)
    {
      const struct oak_struct_method_t* me = &sd->methods[m];
      if (!me->decl)
        continue;
      struct oak_value_t fn_val = c->chunk->constants[me->const_idx];
      struct oak_obj_fn_t* fn_obj = oak_as_fn(fn_val);
      fn_obj->code_offset = c->chunk->count;
      oak_compiler_compile_function_body(c, me->decl, sd);
      if (c->has_error)
        return;
    }
  }
}

static const struct oak_token_t*
arg_expr_error_token(const struct oak_ast_node_t* arg_expr,
                     const struct oak_ast_node_t* arg_wrap)
{
  const struct oak_token_t* err_tok = arg_expr->token;
  if (!err_tok && arg_wrap->kind == OAK_NODE_FN_CALL_ARG && arg_wrap->child &&
      arg_wrap->child->token)
    err_tok = arg_wrap->child->token;
  return err_tok;
}

/* call->children: callee, then each explicit argument. decl is the function or
 * method's AST (parameters match argument order, including implicit receiver
 * for methods when the caller validates it separately). */
static void
validate_call_arg_types_for_decl(struct oak_compiler_t* c,
                                 const struct oak_ast_node_t* call,
                                 const struct oak_ast_node_t* decl)
{
  if (!decl)
    return;
  const struct oak_list_entry_t* first = call->children.next;
  struct oak_list_entry_t* pos = first->next;
  for (usize i = 0; pos != &call->children; pos = pos->next, ++i)
  {
    const struct oak_ast_node_t* arg_wrap =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* arg_expr = arg_wrap;
    if (arg_wrap->kind == OAK_NODE_FN_CALL_ARG)
      arg_expr = arg_wrap->child;

    const struct oak_ast_node_t* param = oak_compiler_fn_decl_param_at(
        decl, (int)i);
    if (!param)
    {
      oak_compiler_error_at(c, null, "internal error: missing parameter %zu", i);
      return;
    }
    const struct oak_ast_node_t* want_type_node = oak_compiler_fn_param_type_node(param);
    if (!want_type_node)
    {
      oak_compiler_error_at(
          c, param->token, "malformed function parameter (expected type name)");
      return;
    }
    struct oak_type_t want;
    oak_compiler_type_node_to_type(c, want_type_node, &want);
    if (!oak_type_is_known(&want))
    {
      oak_compiler_error_at(
          c, param->token, "malformed function parameter type");
      return;
    }

    struct oak_type_t got;
    oak_compiler_infer_expr_static_type(c, arg_expr, &got);
    if (!oak_type_is_known(&got))
      continue;

    if (oak_type_is_void(&got))
    {
      const struct oak_token_t* err_tok = arg_expr_error_token(
          arg_expr, arg_wrap);
      oak_compiler_error_at(c, err_tok, "argument %zu cannot be void", i + 1u);
      return;
    }

    if (!oak_type_equal(&want, &got))
    {
      const struct oak_token_t* err_tok = arg_expr_error_token(
          arg_expr, arg_wrap);
      oak_compiler_error_at(c,
                            err_tok,
                            "argument %zu: expected type '%s', found '%s'",
                            i + 1u,
                            oak_compiler_type_full_name(c, want),
                            oak_compiler_type_full_name(c, got));
    }

    if (oak_compiler_fn_param_is_mutable(param) &&
        oak_type_is_refcounted(&want) &&
        !oak_compiler_expr_is_mutable_place(c, arg_expr))
    {
      const struct oak_token_t* err_tok = arg_expr_error_token(
          arg_expr, arg_wrap);
      oak_compiler_error_at(c,
                            err_tok,
                            "argument %zu: cannot pass an immutable value to a "
                            "mutable parameter",
                            i + 1u);
    }
  }
}

void
oak_compiler_validate_user_fn_call_arg_types(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* fn)
{
  if (!fn->decl)
    return;
  validate_call_arg_types_for_decl(c, call, fn->decl);
}

/* Type-check explicit arguments of a struct method call against the method's
 * declared parameters. The receiver itself is not validated here (it has
 * already been checked to be the right struct type by the caller). */
void
oak_compiler_validate_struct_method_call_arg_types(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_struct_method_t* m)
{
  if (!m->decl)
    return;
  validate_call_arg_types_for_decl(c, call, m->decl);
}
