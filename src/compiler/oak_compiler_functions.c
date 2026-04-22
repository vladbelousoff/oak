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

int oak_compiler_fn_decl_has_receiver(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* prefix = oak_fn_decl_prefix(decl);
  const struct oak_list_entry_t* first = prefix->children.next;
  if (first == &prefix->children)
    return 0;
  const struct oak_ast_node_t* n =
      oak_container_of(first, struct oak_ast_node_t, link);
  return n->kind == OAK_NODE_FN_RECEIVER;
}

const struct oak_ast_node_t*
oak_compiler_fn_decl_param_list(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* tail = oak_fn_decl_params_tail(decl);
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &tail->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_FN_PARAM_LIST)
      return ch;
  }
  return null;
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
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &plist->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_FN_PARAM_SELF)
      return ch;
  }
  return null;
}

int oak_compiler_fn_param_self_is_mutable(const struct oak_ast_node_t* self_param)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &self_param->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_MUT_KEYWORD)
      return 1;
  }
  return 0;
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
oak_compiler_fn_param_type_ident(const struct oak_ast_node_t* param)
{
  int ident_index = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &param->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_IDENT)
    {
      if (ident_index == 1)
        return ch;
      ++ident_index;
    }
  }
  return null;
}

const struct oak_ast_node_t*
oak_compiler_fn_decl_param_at(const struct oak_ast_node_t* decl, const int index)
{
  const struct oak_ast_node_t* plist = oak_compiler_fn_decl_param_list(decl);
  if (!plist)
    return null;
  int i = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &plist->children)
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
  const struct oak_ast_node_t* tail = oak_fn_decl_params_tail(decl);
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &tail->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind != OAK_NODE_FN_RETURN_TYPE)
      continue;
    return ch->child;
  }
  return null;
}

int oak_compiler_count_fn_params(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* plist = oak_compiler_fn_decl_param_list(decl);
  if (!plist)
    return 0;
  int n = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &plist->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_FN_PARAM)
      ++n;
  }
  return n;
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

    const struct oak_ast_node_t* name_node = oak_compiler_fn_decl_name_node(item);
    const char* name = oak_token_text(name_node->token);
    const int name_len = oak_token_length(name_node->token);
    const int explicit_arity = oak_compiler_count_fn_params(item);

    const struct oak_ast_node_t* self_param = oak_compiler_fn_decl_self_param(item);

    if (oak_compiler_fn_decl_has_receiver(item))
    {
      const struct oak_ast_node_t* prefix = oak_fn_decl_prefix(item);
      const struct oak_list_entry_t* rpos = prefix->children.next;
      oak_assert(rpos != &prefix->children);
      const struct oak_ast_node_t* recv_node =
          oak_container_of(rpos, struct oak_ast_node_t, link);
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
            c,
            name_node->token,
            "method '%.*s' must declare 'self' or 'mut self' as its first"
            " parameter",
            name_len,
            name);
        return;
      }

      const char* sname = oak_token_text(recv_ident->token);
      const int sname_len = oak_token_length(recv_ident->token);
      struct oak_registered_struct_t* sd = null;
      for (int i = 0; i < c->struct_count; ++i)
      {
        struct oak_registered_struct_t* cand = &c->structs[i];
        if (cand->name_len == (usize)sname_len &&
            memcmp(cand->name, sname, (usize)sname_len) == 0)
        {
          sd = cand;
          break;
        }
      }
      if (!sd)
      {
        oak_compiler_error_at(c,
                          recv_ident->token,
                          "no such struct '%.*s' for method receiver",
                          sname_len,
                          sname);
        return;
      }

      for (int i = 0; i < sd->method_count; ++i)
      {
        const struct oak_struct_method_t* e = &sd->methods[i];
        if (e->name_len == (usize)name_len &&
            memcmp(e->name, name, (usize)name_len) == 0)
        {
          oak_compiler_error_at(c,
                            name_node->token,
                            "duplicate method '%.*s' on struct '%.*s'",
                            name_len,
                            name,
                            (int)sd->name_len,
                            sd->name);
          return;
        }
      }

      if (sd->method_count >= OAK_MAX_STRUCT_METHODS)
      {
        oak_compiler_error_at(c,
                          name_node->token,
                          "too many methods on struct '%.*s' (max %d)",
                          (int)sd->name_len,
                          sd->name,
                          OAK_MAX_STRUCT_METHODS);
        return;
      }

      const int total_arity = explicit_arity + 1;
      struct oak_obj_fn_t* fn_obj = oak_fn_new(0, total_arity);
      const u8 idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));

      struct oak_struct_method_t* slot = &sd->methods[sd->method_count++];
      slot->name = name;
      slot->name_len = (usize)name_len;
      slot->const_idx = idx;
      slot->arity = total_arity;
      slot->decl = item;
      continue;
    }

    if (self_param)
    {
      const struct oak_ast_node_t* first_child = oak_container_of(
          self_param->children.next, struct oak_ast_node_t, link);
      oak_compiler_error_at(c,
                        first_child->token,
                        "'self' parameter is only valid on methods (use"
                        " 'fn TypeName.%.*s(self, ...)' instead)",
                        name_len,
                        name);
      return;
    }

    for (int i = 0; i < c->fn_registry_count; ++i)
    {
      const struct oak_registered_fn_t* e = &c->fn_registry[i];
      if (e->name_len == (usize)name_len &&
          memcmp(e->name, name, (usize)name_len) == 0)
      {
        oak_compiler_error_at(
            c, name_node->token, "duplicate function '%.*s'", name_len, name);
        return;
      }
    }

    if (c->fn_registry_count >= OAK_MAX_USER_FNS)
    {
      oak_compiler_error_at(c,
                        null,
                        "too many functions in one program (max %d)",
                        OAK_MAX_USER_FNS);
      return;
    }

    struct oak_obj_fn_t* fn_obj = oak_fn_new(0, explicit_arity);
    const u8 idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));

    struct oak_registered_fn_t* slot = &c->fn_registry[c->fn_registry_count++];
    slot->name = name;
    slot->name_len = (usize)name_len;
    slot->const_idx = idx;
    slot->arity_min = explicit_arity;
    slot->arity_max = explicit_arity;
    slot->decl = item;
  }
}

const struct oak_registered_fn_t* oak_compiler_find_registered_fn_entry(
    struct oak_compiler_t* c, const char* name, const usize len)
{
  for (int i = 0; i < c->fn_registry_count; ++i)
  {
    const struct oak_registered_fn_t* e = &c->fn_registry[i];
    if (e->name_len == len && memcmp(e->name, name, len) == 0)
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

  struct oak_ast_node_t* expr = null;
  oak_ast_node_unpack(node, &expr);
  if (expr)
    oak_compiler_compile_node(c, expr);
  else
  {
    const u8 z = oak_compiler_intern_constant(c, OAK_VALUE_I32(0));
    oak_compiler_emit_op_arg(c, OAK_OP_CONSTANT, z, OAK_LOC_SYNTHETIC);
  }
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

  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &plist->children)
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
    const struct oak_ast_node_t* type_id = oak_compiler_fn_param_type_ident(ch);
    struct oak_type_t param_type;
    oak_type_clear(&param_type);
    if (type_id && type_id->kind == OAK_NODE_IDENT)
      param_type.id = oak_compiler_intern_type_token(c, type_id->token);
    oak_compiler_add_local(c,
              oak_token_text(id->token),
              (usize)oak_token_length(id->token),
              slot++,
              oak_compiler_fn_param_is_mutable(ch),
              param_type);
  }

  c->stack_depth = slot;

  oak_compiler_compile_block(c, body);

  const u8 z = oak_compiler_intern_constant(c, OAK_VALUE_I32(0));
  oak_compiler_emit_op_arg(c, OAK_OP_CONSTANT, z, OAK_LOC_SYNTHETIC);
  oak_compiler_emit_op(c, OAK_OP_RETURN, OAK_LOC_SYNTHETIC);

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


void
oak_compiler_validate_user_fn_call_arg_types(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* call,
                                const struct oak_registered_fn_t* fn)
{
  if (!fn->decl)
    return;
  const struct oak_list_entry_t* first = call->children.next;
  struct oak_list_entry_t* pos = first->next;
  usize i = 0;
  for (; pos != &call->children; pos = pos->next, ++i)
  {
    const struct oak_ast_node_t* arg_wrap =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* arg_expr = arg_wrap;
    if (arg_wrap->kind == OAK_NODE_FN_CALL_ARG)
      arg_expr = arg_wrap->child;

    const struct oak_ast_node_t* param = oak_compiler_fn_decl_param_at(fn->decl, (int)i);
    if (!param)
    {
      oak_compiler_error_at(c, null, "internal error: missing parameter %zu", i);
      return;
    }
    const struct oak_ast_node_t* want_type_node = oak_compiler_fn_param_type_ident(param);
    if (!want_type_node || want_type_node->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, param->token, "malformed function parameter (expected type name)");
      return;
    }
    const struct oak_type_t want = {
      .id = oak_compiler_intern_type_token(c, want_type_node->token),
    };

    struct oak_type_t got;
    oak_compiler_infer_expr_static_type(c, arg_expr, &got);

    if (!oak_type_is_known(&got))
      continue;

    if (!oak_type_equal(&want, &got))
    {
      const struct oak_token_t* err_tok = arg_expr->token;
      if (!err_tok && arg_wrap->kind == OAK_NODE_FN_CALL_ARG &&
          arg_wrap->child && arg_wrap->child->token)
        err_tok = arg_wrap->child->token;
      oak_compiler_error_at(c,
                        err_tok,
                        "argument %zu: expected type '%s', found '%s'",
                        i + 1,
                        oak_compiler_type_full_name(c, want),
                        oak_compiler_type_full_name(c, got));
    }

    if (oak_compiler_fn_param_is_mutable(param) &&
        oak_type_is_refcounted(&want) &&
        !oak_compiler_expr_is_mutable_place(c, arg_expr))
    {
      const struct oak_token_t* err_tok = arg_expr->token;
      if (!err_tok && arg_wrap->kind == OAK_NODE_FN_CALL_ARG &&
          arg_wrap->child && arg_wrap->child->token)
        err_tok = arg_wrap->child->token;
      oak_compiler_error_at(c,
                        err_tok,
                        "argument %zu: cannot pass an immutable value to a "
                        "mutable parameter",
                        i + 1);
    }
  }
}

/* Type-check explicit arguments of a struct method call against the method's
 * declared parameters. The receiver itself is not validated here (it has
 * already been checked to be the right struct type by the caller). */
void
oak_compiler_validate_struct_method_call_arg_types(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* call,
                                      const struct oak_struct_method_t* m)
{
  if (!m->decl)
    return;
  const struct oak_list_entry_t* first = call->children.next;
  struct oak_list_entry_t* pos = first->next;
  int i = 0;
  for (; pos != &call->children; pos = pos->next, ++i)
  {
    const struct oak_ast_node_t* arg_wrap =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* arg_expr = arg_wrap;
    if (arg_wrap->kind == OAK_NODE_FN_CALL_ARG)
      arg_expr = arg_wrap->child;

    const struct oak_ast_node_t* param = oak_compiler_fn_decl_param_at(m->decl, i);
    if (!param)
    {
      oak_compiler_error_at(c, null, "internal error: missing parameter %d", i);
      return;
    }
    const struct oak_ast_node_t* want_type_node = oak_compiler_fn_param_type_ident(param);
    if (!want_type_node || want_type_node->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, param->token, "malformed function parameter (expected type name)");
      return;
    }
    const struct oak_type_t want = {
      .id = oak_compiler_intern_type_token(c, want_type_node->token),
    };

    struct oak_type_t got;
    oak_compiler_infer_expr_static_type(c, arg_expr, &got);
    if (!oak_type_is_known(&got))
      continue;
    if (!oak_type_equal(&want, &got))
    {
      const struct oak_token_t* err_tok = arg_expr->token;
      if (!err_tok && arg_wrap->kind == OAK_NODE_FN_CALL_ARG &&
          arg_wrap->child && arg_wrap->child->token)
        err_tok = arg_wrap->child->token;
      oak_compiler_error_at(c,
                        err_tok,
                        "argument %d: expected type '%s', found '%s'",
                        i + 1,
                        oak_compiler_type_full_name(c, want),
                        oak_compiler_type_full_name(c, got));
    }

    if (oak_compiler_fn_param_is_mutable(param) &&
        oak_type_is_refcounted(&want) &&
        !oak_compiler_expr_is_mutable_place(c, arg_expr))
    {
      const struct oak_token_t* err_tok = arg_expr->token;
      if (!err_tok && arg_wrap->kind == OAK_NODE_FN_CALL_ARG &&
          arg_wrap->child && arg_wrap->child->token)
        err_tok = arg_wrap->child->token;
      oak_compiler_error_at(c,
                        err_tok,
                        "argument %d: cannot pass an immutable value to a "
                        "mutable parameter",
                        i + 1);
    }
  }
}
