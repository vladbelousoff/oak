#include "internal/oak_compiler.h"

void oakc_compile_return(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* node)
{
  if (c->scope.fn_depth == 0)
  {
    oak_compiler_error_at(c, null, "'return' outside of a function");
    return;
  }

  /* STMT_RETURN is UNARY: child = EXPR? */
  const struct oak_ast_node_t* expr = node->child;
  if (oak_type_is_void(&c->scope.declared_return_type))
  {
    if (expr)
    {
      oak_compiler_error_at(c,
                            expr->token ? expr->token : node->token,
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
    if (oak_type_is_known(&c->scope.declared_return_type))
    {
      struct oak_type_t got;
      oakc_infer_type(c, expr, &got);
      if (oak_type_is_known(&got) &&
          !oakc_type_accepts(&c->scope.declared_return_type, &got))
      {
        oak_compiler_error_at(
            c,
            expr->token ? expr->token : node->token,
            "return type mismatch: expected '%s', found '%s'",
            oakc_type_full_name(c, c->scope.declared_return_type),
            oakc_type_full_name(c, got));
      }
    }
    oak_compiler_compile_node(c, expr);
    if (c->has_error)
      return;
    oakc_emit_trait_coerce(c,
                           expr,
                           c->scope.declared_return_type,
                           OAK_LOC_SYNTHETIC);
    if (c->has_error)
      return;
    oakc_emit_weak_coerce(c,
                          expr,
                          c->scope.declared_return_type,
                          OAK_LOC_SYNTHETIC);
    if (c->has_error)
      return;
    oak_compiler_emit_op(c, OAK_OP_RETURN, OAK_LOC_SYNTHETIC);
    return;
  }

  const u16 z = oak_compiler_intern_constant(c, OAK_VALUE_I32(0));
  oak_compiler_emit_constant(c, z, OAK_LOC_SYNTHETIC);
  oak_compiler_emit_op(c, OAK_OP_RETURN, OAK_LOC_SYNTHETIC);
}

/* If `recv` is non-null, the fn is treated as a method: an
 * implicit `self` local is installed at slot 0 with the receiver's static
 * type, and explicit parameters start at slot 1. */
void oakc_compile_fn_body(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* decl,
    const struct oak_registered_record_t* recv)
{
  const struct oak_ast_node_t* body = oakc_fn_block(decl);
  if (!body || body->kind != OAK_NODE_BLOCK)
  {
    oak_compiler_error_at(c, decl->token, "function has no body");
    return;
  }

  c->scope.fn_depth++;
  c->scope.local_count = 0;
  c->scope.scope_depth = 0;
  c->scope.stack_depth = 0;
  c->scope.current_loop = null;

  /* Return type: omitted `->` means void. */
  oak_type_clear(&c->scope.declared_return_type);
  const struct oak_ast_node_t* ret_type_node =
      oakc_fn_return_type_node(decl);
  if (ret_type_node)
  {
    oakc_lower_type_node(
        c, ret_type_node, &c->scope.declared_return_type);
    if (oak_type_is_void(&c->scope.declared_return_type))
    {
      oak_compiler_error_at(
          c,
          ret_type_node->token,
          "omit the return type for a function with no value; 'void' is not "
          "allowed after '->'");
      c->scope.fn_depth--;
      return;
    }
  }
  else
    c->scope.declared_return_type.id = OAK_TYPE_VOID;

  int slot = 0;
  if (recv)
  {
    const struct oak_ast_node_t* sp = oakc_fn_self_param(decl);
    oak_assert(sp != null);
    struct oak_type_t self_ty;
    oak_type_clear(&self_ty);
    self_ty.id = recv->type_id;
    oak_compiler_add_local(c,
                           "self",
                           4u,
                           slot++,
                           oakc_self_is_mut(sp),
                           self_ty);
  }

  const struct oak_ast_node_t* plist = oakc_fn_param_list(decl);
  if (!plist || plist->kind != OAK_NODE_FN_PARAM_LIST)
  {
    oak_compiler_error_at(c, decl->token, "malformed function declaration");
    c->scope.fn_depth--;
    return;
  }

  /* FN_PARAM_LIST is BINARY: lhs = FN_PARAM_SELF?, rhs = FN_PARAMS. */
  const struct oak_ast_node_t* params = plist->rhs;
  if (!params)
  {
    oak_compiler_error_at(c, decl->token, "malformed function declaration");
    c->scope.fn_depth--;
    return;
  }

  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &params->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind != OAK_NODE_FN_PARAM)
      continue;
    const struct oak_ast_node_t* id = oakc_fn_param_ident(ch);
    if (!id)
    {
      oak_compiler_error_at(c, ch->token, "malformed function parameter");
      c->scope.fn_depth--;
      return;
    }
    const struct oak_ast_node_t* type_id = oakc_fn_param_type_node(ch);
    struct oak_type_t param_type;
    oak_type_clear(&param_type);
    if (type_id)
      oakc_lower_type_node(c, type_id, &param_type);
    oak_compiler_add_local(c,
                           oak_token_text(id->token),
                           oak_token_length(id->token),
                           slot++,
                           oakc_param_is_mut(ch),
                           param_type);
  }

  c->scope.stack_depth = slot;

  oak_compiler_compile_block(c, body);

  const u16 z = oak_compiler_intern_constant(c, OAK_VALUE_I32(0));
  oak_compiler_emit_constant(c, z, OAK_LOC_SYNTHETIC);
  oak_compiler_emit_op(c, OAK_OP_RETURN, OAK_LOC_SYNTHETIC);

  /* Clear the return type so it doesn't apply outside this fn. */
  oak_type_clear(&c->scope.declared_return_type);
  c->scope.fn_depth--;
}

void oakc_compile_fn_bodies(struct oak_compiler_t* c)
{
  for (int i = 0; i < c->fns.entries.count; ++i)
  {
    const struct oak_registered_fn_t* e = &c->fns.entries.items[i];
    if (!e->decl)
      continue;
    if (!oakc_fn_block(e->decl))
    {
      if (c->allow_bodyless_fns)
        continue;
      oak_compiler_error_at(c, e->decl->token, "function has no body");
      return;
    }
    struct oak_value_t fn_val = c->chunk->constants[e->const_idx];
    struct oak_obj_fn_t* fn_obj = oak_as_fn(fn_val);
    fn_obj->code_offset = c->chunk->count;
    oakc_compile_fn_body(c, e->decl, null);
    if (c->has_error)
      return;
  }
}

void oakc_compile_method_bodies(struct oak_compiler_t* c)
{
  for (int s = 0; s < c->records.entries.count; ++s)
  {
    const struct oak_registered_record_t* sd = &c->records.entries.items[s];
    for (int m = 0; m < sd->methods.count; ++m)
    {
      const struct oak_registered_fn_t* me = &sd->methods.items[m];
      if (!me->decl)
        continue;
      if (!oakc_fn_block(me->decl))
      {
        if (c->allow_bodyless_fns)
          continue;
        oak_compiler_error_at(c, me->decl->token, "function has no body");
        return;
      }
      struct oak_value_t fn_val = c->chunk->constants[me->const_idx];
      struct oak_obj_fn_t* fn_obj = oak_as_fn(fn_val);
      fn_obj->code_offset = c->chunk->count;
      oakc_compile_fn_body(
          c, me->decl, me->is_static ? null : sd);
      if (c->has_error)
        return;
    }
  }
}
