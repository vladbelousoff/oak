#include "oak_compiler_internal.h"

oak_type_id_t oak_compiler_intern_type_token(struct oak_compiler_t* c,
                                             const struct oak_token_t* token)
{
  return oak_type_registry_intern(&c->type_registry,
                                  oak_token_text(token),
                                  (usize)oak_token_length(token));
}

int oak_compiler_local_type_get(struct oak_compiler_t* c,
                                const char* name,
                                const usize len,
                                struct oak_type_t* out)
{
  for (int i = c->local_count - 1; i >= 0; --i)
  {
    const struct oak_local_t* L = &c->locals[i];
    if (L->length == len && memcmp(L->name, name, len) == 0)
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
      out->id = OAK_TYPE_BOOL;
      return;
    case OAK_NODE_IDENT:
    {
      const char* name = oak_token_text(expr->token);
      const usize len = (usize)oak_token_length(expr->token);
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
        struct oak_type_t recv_ty;
        oak_compiler_infer_expr_static_type(c, recv, &recv_ty);
        const char* mn = oak_token_text(method->token);
        const usize mn_len = (usize)oak_token_length(method->token);
        if (oak_type_is_known(&recv_ty) && !recv_ty.is_array
            && !recv_ty.is_map)
        {
          const struct oak_registered_struct_t* sd =
              oak_compiler_find_struct_by_type_id(c, recv_ty.id);
          if (sd)
          {
            for (int i = 0; i < sd->method_count; ++i)
            {
              const struct oak_struct_method_t* sm = &sd->methods[i];
              if (sm->name_len == mn_len
                  && memcmp(sm->name, mn, mn_len) == 0)
              {
                if (sm->decl)
                {
                  const struct oak_ast_node_t* ret =
                      oak_compiler_fn_decl_return_type_node(sm->decl);
                  if (ret && ret->kind == OAK_NODE_IDENT)
                    out->id = oak_compiler_intern_type_token(c, ret->token);
                }
                return;
              }
            }
          }
          return;
        }
        const struct oak_method_binding_t* m = null;
        if (recv_ty.is_array)
          m = oak_compiler_find_array_method(c, mn, mn_len);
        else if (recv_ty.is_map)
          m = oak_compiler_find_map_method(c, mn, mn_len);
        if (m)
          out->id = m->return_type_id;
        return;
      }
      if (callee->kind != OAK_NODE_IDENT)
        return;
      const char* cn = oak_token_text(callee->token);
      const usize clen = (usize)oak_token_length(callee->token);
      const struct oak_registered_fn_t* fe =
          oak_compiler_find_registered_fn_entry(c, cn, clen);
      if (fe && !fe->decl && fe->name_len == 5u
          && memcmp(fe->name, "input", 5u) == 0)
      {
        out->id = OAK_TYPE_STRING;
        return;
      }
      if (!fe || !fe->decl)
        return;
      const struct oak_ast_node_t* ret =
          oak_compiler_fn_decl_return_type_node(fe->decl);
      if (ret && ret->kind == OAK_NODE_IDENT)
        out->id = oak_compiler_intern_type_token(c, ret->token);
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
        out->is_array = 1;
        return;
      }
      if (type_node->kind == OAK_NODE_TYPE_MAP)
      {
        const struct oak_ast_node_t* key = type_node->lhs;
        const struct oak_ast_node_t* val = type_node->rhs;
        if (!key || !val || key->kind != OAK_NODE_IDENT
            || val->kind != OAK_NODE_IDENT)
          return;
        out->key_id = oak_compiler_intern_type_token(c, key->token);
        out->id = oak_compiler_intern_type_token(c, val->token);
        out->is_map = 1;
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
          first_wrap->kind == OAK_NODE_ARRAY_LITERAL_ELEMENT
              ? first_wrap->child
              : first_wrap;
      struct oak_type_t elem_ty;
      oak_compiler_infer_expr_static_type(c, first_elem, &elem_ty);
      if (!oak_type_is_known(&elem_ty))
        return;
      out->id = elem_ty.id;
      out->is_array = 1;
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
      out->is_map = 1;
      return;
    }
    case OAK_NODE_INDEX_ACCESS:
    {
      struct oak_type_t coll_ty;
      oak_compiler_infer_expr_static_type(c, expr->lhs, &coll_ty);
      if (coll_ty.is_array && oak_type_is_known(&coll_ty))
      {
        out->id = coll_ty.id;
        return;
      }
      if (coll_ty.is_map && oak_type_is_known(&coll_ty))
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
          oak_compiler_find_struct_by_name(
              c,
              oak_token_text(name_node->token),
              (usize)oak_token_length(name_node->token));
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
      struct oak_type_t recv_ty;
      oak_compiler_infer_expr_static_type(c, recv, &recv_ty);
      if (!oak_type_is_known(&recv_ty))
        return;
      const struct oak_registered_struct_t* sd =
          oak_compiler_find_struct_by_type_id(c, recv_ty.id);
      if (!sd)
        return;
      const int idx =
          oak_compiler_find_struct_field(sd,
                                         oak_token_text(fname->token),
                                         (usize)oak_token_length(fname->token));
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
  return oak_type_registry_name(&c->type_registry, t.id);
}

/* Format a type name into a thread-local buffer for error messages. */
const char* oak_compiler_type_full_name(struct oak_compiler_t* c,
                                        const struct oak_type_t t)
{
  static _Thread_local char buf[128];
  if (t.is_map)
  {
    snprintf(buf,
             sizeof(buf),
             "[%s:%s]",
             oak_type_registry_name(&c->type_registry, t.key_id),
             oak_type_registry_name(&c->type_registry, t.id));
    return buf;
  }
  if (t.is_array)
  {
    snprintf(buf, sizeof(buf), "%s[]", oak_compiler_type_kind_name(c, t));
    return buf;
  }
  return oak_compiler_type_kind_name(c, t);
}
