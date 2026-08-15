#include "internal/oak_compiler.h"

void oak_compile_return(oak_compiler_t* c,
                                      const oak_ast_node_t* node)
{
  if (c->scope.fn_depth == 0)
  {
    oak_compiler_error_at(c, OAK_NULL, "'return' outside of a function");
    return;
  }

  /* STMT_RETURN is UNARY: child = EXPR? */
  const oak_ast_node_t* expr = node->child;
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
      oak_type_t got;
      oak_infer_type(c, expr, &got);
      if (oak_type_is_known(&got) &&
          !oak_type_accepts(&c->scope.declared_return_type, &got))
      {
        oak_compiler_error_at(
            c,
            expr->token ? expr->token : node->token,
            "return type mismatch: expected '%s', found '%s'",
            oak_type_full_name(c, c->scope.declared_return_type),
            oak_type_full_name(c, got));
      }
    }
    oak_compiler_compile_node(c, expr);
    if (c->has_error)
      return;
    oak_emit_interface_coerce(c,
                           expr,
                           c->scope.declared_return_type,
                           OAK_LOC_SYNTHETIC);
    if (c->has_error)
      return;
    oak_emit_weak_coerce(c,
                          expr,
                          c->scope.declared_return_type,
                          OAK_LOC_SYNTHETIC);
    if (c->has_error)
      return;
    OAK_COMPILER_EMIT_OP(c, OAK_OP_RETURN, OAK_LOC_SYNTHETIC);
    return;
  }

  const u16 z = oak_compiler_intern_constant(c, OAK_VALUE_I32(0));
  oak_compiler_emit_constant(c, z, OAK_LOC_SYNTHETIC);
  OAK_COMPILER_EMIT_OP(c, OAK_OP_RETURN, OAK_LOC_SYNTHETIC);
}

/* If `recv` is non-null, the fn is treated as a method: an
 * implicit `self` local is installed at slot 0 with the receiver's static
 * type, and explicit parameters start at slot 1. */
void oak_compile_fn_body(
    oak_compiler_t* c,
    const oak_ast_node_t* decl,
    const oak_registered_record_t* recv)
{
  const oak_ast_node_t* body = oak_fn_block(decl);
  if (!body || body->kind != OAK_NODE_BLOCK)
  {
    oak_compiler_error_at(c, decl->token, "function has no body");
    return;
  }

  c->scope.fn_depth++;
  c->scope.local_count = 0;
  c->scope.scope_depth = 0;
  c->scope.stack_depth = 0;
  c->scope.current_loop = OAK_NULL;

  /* Return type: omitted `->` means void. */
  oak_type_clear(&c->scope.declared_return_type);
  const oak_ast_node_t* ret_type_node =
      oak_fn_return_type_node(decl);
  if (ret_type_node)
  {
    oak_lower_type_node(
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
    /* `self` is not written in the parameter list; the caller passing a
     * receiver is what puts it in scope, at slot 0 ahead of the params. */
    oak_type_t self_ty;
    oak_type_clear(&self_ty);
    self_ty.id = recv->type_id;
    oak_compiler_add_local(c,
                           "self",
                           slot++,
                           oak_fn_self_is_mut(decl),
                           self_ty);
  }

  const oak_ast_node_t* plist = oak_fn_param_list(decl);
  if (!plist || plist->kind != OAK_NODE_FN_PARAM_LIST)
  {
    oak_compiler_error_at(c, decl->token, "malformed function declaration");
    c->scope.fn_depth--;
    return;
  }

  /* FN_PARAM_LIST is UNARY: child = FN_PARAMS. */
  const oak_ast_node_t* params = plist->child;
  if (!params)
  {
    oak_compiler_error_at(c, decl->token, "malformed function declaration");
    c->scope.fn_depth--;
    return;
  }

  oak_list_entry_t* pos;
  OAK_LIST_FOR_EACH(pos, &params->children)
  {
    const oak_ast_node_t* ch =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
    if (ch->kind != OAK_NODE_FN_PARAM)
      continue;
    const oak_ast_node_t* id = oak_fn_param_ident(ch);
    if (!id)
    {
      oak_compiler_error_at(c, ch->token, "malformed function parameter");
      c->scope.fn_depth--;
      return;
    }
    const oak_ast_node_t* type_id = oak_fn_param_type_node(ch);
    oak_type_t param_type;
    oak_type_clear(&param_type);
    if (type_id)
      oak_lower_type_node(c, type_id, &param_type);
    oak_compiler_add_local(c,
                           oak_token_text(id->token),
                           slot++,
                           oak_param_is_mut(ch),
                           param_type);
  }

  c->scope.stack_depth = slot;

  oak_compiler_compile_block(c, body);

  const u16 z = oak_compiler_intern_constant(c, OAK_VALUE_I32(0));
  oak_compiler_emit_constant(c, z, OAK_LOC_SYNTHETIC);
  OAK_COMPILER_EMIT_OP(c, OAK_OP_RETURN, OAK_LOC_SYNTHETIC);

  for (int i = 0; i < c->scope.local_count; ++i)
    oak_chunk_end_debug_local(c->chunk, c->scope.locals[i].slot);

  /* Clear the return type so it doesn't apply outside this fn. */
  oak_type_clear(&c->scope.declared_return_type);
  c->scope.fn_depth--;
}

void oak_compile_fn_bodies(oak_compiler_t* c)
{
  /* Re-fetched each iteration rather than hoisted: compiling a body can
   * register further functions, which may reallocate the vector. */
  for (usize i = 0; i < oak_size(c->fns.entries); ++i)
  {
    const oak_registered_fn_t* e = oak_cget(c->fns.entries, i);
    if (!e->decl)
      continue;
    if (!oak_fn_block(e->decl))
    {
      if (c->allow_bodyless_fns)
        continue;
      oak_compiler_error_at(c, e->decl->token, "function has no body");
      return;
    }
    oak_value_t fn_val = oak_chunk_constant(c->chunk, (usize)e->const_idx);
    oak_obj_fn_t* fn_obj = oak_as_fn(fn_val);
    fn_obj->code_offset = oak_chunk_size(c->chunk);
    oak_compile_fn_body(c, e->decl, OAK_NULL);
    if (c->has_error)
      return;
  }
}

void oak_compile_method_bodies(oak_compiler_t* c)
{
  for (usize s = 0; s < oak_size(c->records.entries); ++s)
  {
    const oak_registered_record_t* sd = oak_cget(c->records.entries, s);
    for (usize m = 0; m < oak_size(sd->methods); ++m)
    {
      const oak_registered_fn_t* me = oak_cget(sd->methods, m);
      if (!me->decl)
        continue;
      if (!oak_fn_block(me->decl))
      {
        if (c->allow_bodyless_fns)
          continue;
        oak_compiler_error_at(c, me->decl->token, "function has no body");
        return;
      }
      oak_value_t fn_val = oak_chunk_constant(c->chunk, (usize)me->const_idx);
      oak_obj_fn_t* fn_obj = oak_as_fn(fn_val);
      fn_obj->code_offset = oak_chunk_size(c->chunk);
      oak_compile_fn_body(
          c, me->decl, me->is_static ? OAK_NULL : sd);
      if (c->has_error)
        return;
    }
  }
}
