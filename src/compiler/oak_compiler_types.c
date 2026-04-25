#include "oak_compiler_internal.h"

void oak_compiler_type_node_to_type(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* type_node,
                                    struct oak_type_t* out)
{
  oak_type_clear(out);
  if (!type_node)
    return;
  if (type_node->kind == OAK_NODE_IDENT)
  {
    out->id = oak_compiler_intern_type_token(c, type_node->token);
    return;
  }
  if (type_node->kind == OAK_NODE_TYPE_ARRAY)
  {
    const struct oak_ast_node_t* elem = type_node->child;
    if (!elem || elem->kind != OAK_NODE_IDENT)
      return;
    out->id = oak_compiler_intern_type_token(c, elem->token);
    out->kind = OAK_TYPE_KIND_ARRAY;
    return;
  }
  if (type_node->kind == OAK_NODE_TYPE_MAP)
  {
    const struct oak_ast_node_t* key = type_node->lhs;
    const struct oak_ast_node_t* val = type_node->rhs;
    if (!key || !val || key->kind != OAK_NODE_IDENT ||
        val->kind != OAK_NODE_IDENT)
      return;
    out->key_id = oak_compiler_intern_type_token(c, key->token);
    out->id = oak_compiler_intern_type_token(c, val->token);
    out->kind = OAK_TYPE_KIND_MAP;
    return;
  }
}

oak_type_id_t oak_compiler_intern_type_token(struct oak_compiler_t* c,
                                             const struct oak_token_t* token)
{
  return oak_type_registry_intern(
      &c->types, oak_token_text(token), oak_token_length(token));
}

int oak_compiler_local_type_get(struct oak_compiler_t* c,
                                const char* name,
                                const usize len,
                                struct oak_type_t* out)
{
  for (int i = c->scope.local_count - 1; i >= 0; --i)
  {
    const struct oak_local_t* L = &c->scope.locals[i];
    if (oak_name_eq(L->name, L->length, name, len))
    {
      *out = L->type;
      return 1;
    }
  }
  return 0;
}

void oak_compiler_infer_expr_static_type(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* expr,
                                         struct oak_type_t* out)
{
  oak_type_clear(out);
  if (!expr)
    return;

  switch (expr->kind)
  {
    case OAK_NODE_INT:
    case OAK_NODE_FLOAT:
    case OAK_NODE_UNARY_NEG:
    case OAK_NODE_BINARY_ADD:
    case OAK_NODE_BINARY_SUB:
    case OAK_NODE_BINARY_MUL:
    case OAK_NODE_BINARY_DIV:
    case OAK_NODE_BINARY_MOD:
      out->id = OAK_TYPE_NUMBER;
      return;
    case OAK_NODE_STRING:
      out->id = OAK_TYPE_STRING;
      return;
    case OAK_NODE_UNARY_NOT:
    case OAK_NODE_BINARY_EQ:
    case OAK_NODE_BINARY_NEQ:
    case OAK_NODE_BINARY_LESS:
    case OAK_NODE_BINARY_LESS_EQ:
    case OAK_NODE_BINARY_GREATER:
    case OAK_NODE_BINARY_GREATER_EQ:
    case OAK_NODE_BINARY_AND:
    case OAK_NODE_BINARY_OR:
      out->id = OAK_TYPE_BOOL;
      return;
    case OAK_NODE_IDENT:
    {
      const char* name = oak_token_text(expr->token);
      const usize len = oak_token_length(expr->token);
      struct oak_type_t local_ty;
      oak_type_clear(&local_ty);
      if (oak_compiler_local_type_get(c, name, len, &local_ty))
        *out = local_ty;
      return;
    }
    case OAK_NODE_SELF:
    {
      struct oak_type_t local_ty;
      oak_type_clear(&local_ty);
      if (oak_compiler_local_type_get(c, "self", 4u, &local_ty))
        *out = local_ty;
      return;
    }
    case OAK_NODE_FN_CALL:
    {
      const struct oak_list_entry_t* first = expr->children.next;
      if (first == &expr->children)
        return;
      const struct oak_ast_node_t* callee =
          oak_container_of(first, struct oak_ast_node_t, link);
      if (!callee)
        return;
      if (callee->kind == OAK_NODE_MEMBER_ACCESS)
      {
        const struct oak_ast_node_t* recv = callee->lhs;
        const struct oak_ast_node_t* method = callee->rhs;
        if (!recv || !method || method->kind != OAK_NODE_IDENT)
          return;
        const char* mn = oak_token_text(method->token);
        const usize mn_len = oak_token_length(method->token);
        if (recv->kind == OAK_NODE_IDENT)
        {
          const char* rname = oak_token_text(recv->token);
          const usize rlen = oak_token_length(recv->token);
          struct oak_type_t local_ty;
          oak_type_clear(&local_ty);
          if (!oak_compiler_local_type_get(c, rname, rlen, &local_ty))
          {
            const struct oak_registered_struct_t* sd =
                oak_compiler_find_struct_by_name(c, rname, rlen);
            if (sd)
            {
              const struct oak_registered_fn_t* sm =
                  oak_compiler_find_struct_static_method(sd, mn, mn_len);
              if (sm)
              {
                out->id = sm->return_type_id;
                out->kind = sm->return_kind;
                if (out->id == OAK_TYPE_VOID)
                  out->kind = OAK_TYPE_KIND_SCALAR;
                return;
              }
            }
          }
        }
        struct oak_type_t recv_ty;
        oak_compiler_infer_expr_static_type(c, recv, &recv_ty);
        if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR)
        {
          const struct oak_registered_struct_t* sd =
              oak_compiler_find_struct_by_type_id(c, recv_ty.id);
          if (sd)
          {
          for (int i = 0; i < sd->method_count; ++i)
          {
            const struct oak_registered_fn_t* sm = &sd->methods[i];
            if (oak_name_eq(sm->name, sm->name_len, mn, mn_len))
            {
              if (sm->decl)
              {
                const struct oak_ast_node_t* retn =
                    oak_compiler_fn_decl_return_type_node(sm->decl);
                if (retn)
                  oak_compiler_type_node_to_type(c, retn, out);
                else
                  out->id = OAK_TYPE_VOID;
              }
              else
              {
                /* Native method: use the pre-declared return type. */
                out->id = sm->return_type_id;
                out->kind = sm->return_kind;
                if (out->id == OAK_TYPE_VOID)
                  out->kind = OAK_TYPE_KIND_SCALAR;
              }
              return;
            }
          }
          }
          return;
        }
        const struct oak_method_binding_t* m = null;
        if (recv_ty.kind == OAK_TYPE_KIND_ARRAY)
          m = oak_compiler_find_array_method(c, mn, mn_len);
        else if (recv_ty.kind == OAK_TYPE_KIND_MAP)
          m = oak_compiler_find_map_method(c, mn, mn_len);
        if (m)
          out->id = m->return_type_id;
        return;
      }
      if (callee->kind != OAK_NODE_IDENT)
        return;
      const char* cn = oak_token_text(callee->token);
      const usize clen = oak_token_length(callee->token);
      const struct oak_registered_fn_t* fe =
          oak_compiler_find_registered_fn_entry(c, cn, clen);
      if (fe && !fe->decl)
      {
        if (fe->name_len == 5u && memcmp(fe->name, "print", 5u) == 0)
        {
          out->id = OAK_TYPE_VOID;
          return;
        }
        if (fe->name_len == 7u && memcmp(fe->name, "println", 7u) == 0)
        {
          out->id = OAK_TYPE_VOID;
          return;
        }
        /* General native function: use the pre-declared return type. */
        out->id = fe->return_type_id;
        out->kind = fe->return_kind;
        if (out->id == OAK_TYPE_VOID)
          out->kind = OAK_TYPE_KIND_SCALAR;
        return;
      }
      if (!fe)
        return;
      const struct oak_ast_node_t* retn =
          oak_compiler_fn_decl_return_type_node(fe->decl);
      if (retn)
        oak_compiler_type_node_to_type(c, retn, out);
      else
        out->id = OAK_TYPE_VOID;
      return;
    }
    case OAK_NODE_EXPR_CAST:
    {
      const struct oak_ast_node_t* type_node = expr->rhs;
      if (!type_node)
        return;
      if (type_node->kind == OAK_NODE_TYPE_ARRAY)
      {
        const struct oak_ast_node_t* elem = type_node->child;
        if (!elem || elem->kind != OAK_NODE_IDENT)
          return;
        out->id = oak_compiler_intern_type_token(c, elem->token);
        out->kind = OAK_TYPE_KIND_ARRAY;
        return;
      }
      if (type_node->kind == OAK_NODE_TYPE_MAP)
      {
        const struct oak_ast_node_t* key = type_node->lhs;
        const struct oak_ast_node_t* val = type_node->rhs;
        if (!key || !val || key->kind != OAK_NODE_IDENT ||
            val->kind != OAK_NODE_IDENT)
          return;
        out->key_id = oak_compiler_intern_type_token(c, key->token);
        out->id = oak_compiler_intern_type_token(c, val->token);
        out->kind = OAK_TYPE_KIND_MAP;
        return;
      }
      if (type_node->kind == OAK_NODE_IDENT)
        out->id = oak_compiler_intern_type_token(c, type_node->token);
      return;
    }
    case OAK_NODE_EXPR_ARRAY_LITERAL:
    {
      const struct oak_list_entry_t* first = expr->children.next;
      if (first == &expr->children)
        return;
      const struct oak_ast_node_t* first_wrap =
          oak_container_of(first, struct oak_ast_node_t, link);
      const struct oak_ast_node_t* first_elem =
          first_wrap->kind == OAK_NODE_ARRAY_LITERAL_ELEMENT ? first_wrap->child
                                                             : first_wrap;
      struct oak_type_t elem_ty;
      oak_compiler_infer_expr_static_type(c, first_elem, &elem_ty);
      if (!oak_type_is_known(&elem_ty))
        return;
      out->id = elem_ty.id;
      out->kind = OAK_TYPE_KIND_ARRAY;
      return;
    }
    case OAK_NODE_EXPR_MAP_LITERAL:
    {
      const struct oak_ast_node_t* first_entry = expr->lhs;
      if (!first_entry || first_entry->kind != OAK_NODE_MAP_LITERAL_ENTRY)
        return;
      struct oak_type_t key_ty;
      struct oak_type_t val_ty;
      oak_compiler_infer_expr_static_type(c, first_entry->lhs, &key_ty);
      oak_compiler_infer_expr_static_type(c, first_entry->rhs, &val_ty);
      if (!oak_type_is_known(&key_ty) || !oak_type_is_known(&val_ty))
        return;
      out->key_id = key_ty.id;
      out->id = val_ty.id;
      out->kind = OAK_TYPE_KIND_MAP;
      return;
    }
    case OAK_NODE_INDEX_ACCESS:
    {
      struct oak_type_t coll_ty;
      oak_compiler_infer_expr_static_type(c, expr->lhs, &coll_ty);
      if (coll_ty.kind == OAK_TYPE_KIND_ARRAY && oak_type_is_known(&coll_ty))
      {
        out->id = coll_ty.id;
        return;
      }
      if (coll_ty.kind == OAK_TYPE_KIND_MAP && oak_type_is_known(&coll_ty))
      {
        out->id = coll_ty.id;
        return;
      }
      return;
    }
    case OAK_NODE_EXPR_STRUCT_LITERAL:
    {
      const struct oak_ast_node_t* name_node = expr->lhs;
      if (!name_node || name_node->kind != OAK_NODE_IDENT)
        return;
      const struct oak_registered_struct_t* sd =
          oak_compiler_find_struct_by_name(c,
                                           oak_token_text(name_node->token),
                                           oak_token_length(name_node->token));
      if (!sd)
        return;
      out->id = sd->type_id;
      return;
    }
    case OAK_NODE_MEMBER_ACCESS:
    {
      const struct oak_ast_node_t* recv = expr->lhs;
      const struct oak_ast_node_t* fname = expr->rhs;
      if (!recv || !fname || fname->kind != OAK_NODE_IDENT)
        return;
      /* Enum variant access: EnumName.Variant yields a number. */
      if (recv->kind == OAK_NODE_IDENT)
      {
        const char* recv_name = oak_token_text(recv->token);
        const usize recv_len = oak_token_length(recv->token);
        if (oak_compiler_is_enum_name(c, recv_name, recv_len))
        {
          out->id = OAK_TYPE_NUMBER;
          return;
        }
      }
      struct oak_type_t recv_ty;
      oak_compiler_infer_expr_static_type(c, recv, &recv_ty);
      const struct oak_registered_struct_t* sd = null;
      const int idx =
          oak_compiler_struct_field_index(c,
                                          recv_ty,
                                          oak_token_text(fname->token),
                                          oak_token_length(fname->token),
                                          &sd);
      if (idx < 0)
        return;
      *out = sd->fields[idx].type;
      return;
    }
    default:
      return;
  }
}

const char* oak_compiler_type_kind_name(struct oak_compiler_t* c,
                                        const struct oak_type_t t)
{
  return oak_type_registry_name(&c->types, t.id);
}

/* Format a type name into a thread-local buffer for error messages.
 * Uses a small ring of buffers so two calls in the same varargs list
 * (e.g. "expected '%s', found '%s'") each return a distinct pointer. */
const char* oak_compiler_type_full_name(struct oak_compiler_t* c,
                                        const struct oak_type_t t)
{
  static _Thread_local char bufs[4][128];
  static _Thread_local int slot = 0;
  char* buf = bufs[slot % 4];
  ++slot;
  if (t.kind == OAK_TYPE_KIND_MAP)
  {
    snprintf(buf,
             128,
             "[%s:%s]",
             oak_type_registry_name(&c->types, t.key_id),
             oak_type_registry_name(&c->types, t.id));
    return buf;
  }
  if (t.kind == OAK_TYPE_KIND_ARRAY)
  {
    snprintf(buf, 128, "%s[]", oak_compiler_type_kind_name(c, t));
    return buf;
  }
  return oak_compiler_type_kind_name(c, t);
}

void oak_compiler_reject_void_value_expr(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* expr)
{
  if (!expr)
    return;
  struct oak_type_t t;
  oak_compiler_infer_expr_static_type(c, expr, &t);
  if (oak_type_is_void(&t))
  {
    oak_compiler_error_at(
        c, expr->token, "this expression has no value (void)");
  }
}
