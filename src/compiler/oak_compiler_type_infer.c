#include "internal/oak_compiler.h"
#include "oak_compiler_modules.h"

/* Implemented in oak_compiler_type_infer_call.c */
void oakc_infer_fn_call_type(struct oak_compiler_t* c,
                            const struct oak_ast_node_t* expr,
                            struct oak_type_t* out);

void oakc_infer_type(struct oak_compiler_t* c,
                   const struct oak_ast_node_t* expr,
                   struct oak_type_t* out)
{
  oak_type_clear(out);
  if (!expr)
    return;

  switch (expr->kind)
  {
    case OAK_NODE_BINARY_ADD:
    {
      struct oak_type_t lt;
      struct oak_type_t rt;
      oakc_infer_type(c, expr->lhs, &lt);
      oakc_infer_type(c, expr->rhs, &rt);
      const int lk = oak_type_is_known(&lt);
      const int rk = oak_type_is_known(&rt);
      const int ls =
          lk && lt.kind == OAK_TYPE_KIND_SCALAR && lt.id == OAK_TYPE_STRING;
      const int rs =
          rk && rt.kind == OAK_TYPE_KIND_SCALAR && rt.id == OAK_TYPE_STRING;
      if (ls && rs)
      {
        out->id = OAK_TYPE_STRING;
        return;
      }
      out->id = OAK_TYPE_NUMBER;
      return;
    }
    case OAK_NODE_INT:
    case OAK_NODE_FLOAT:
    case OAK_NODE_UNARY_NEG:
    case OAK_NODE_BINARY_SUB:
    case OAK_NODE_BINARY_MUL:
    case OAK_NODE_BINARY_DIV:
    case OAK_NODE_BINARY_INT_DIV:
    case OAK_NODE_BINARY_MOD:
      out->id = OAK_TYPE_NUMBER;
      return;
    case OAK_NODE_STRING:
      out->id = OAK_TYPE_STRING;
      return;
    case OAK_NODE_NONE_LITERAL:
      out->id = OAK_TYPE_NONE;
      return;
    case OAK_NODE_TRUE:
    case OAK_NODE_FALSE:
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
      struct oak_type_t local_ty;
      oak_type_clear(&local_ty);
      if (oakc_local_type_get(c, name, &local_ty))
        *out = local_ty;
      return;
    }
    case OAK_NODE_SELF:
    {
      struct oak_type_t local_ty;
      oak_type_clear(&local_ty);
      if (oakc_local_type_get(c, "self", &local_ty))
        *out = local_ty;
      return;
    }
    case OAK_NODE_FN_CALL:
      oakc_infer_fn_call_type(c, expr, out);
      return;
    case OAK_NODE_EXPR_CAST:
    {
      const struct oak_ast_node_t* type_node = expr->rhs;
      if (!type_node)
        return;
      oakc_lower_type_node(c, type_node, out);
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
      oakc_infer_type(c, first_elem, &elem_ty);
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
      oakc_infer_type(c, first_entry->lhs, &key_ty);
      oakc_infer_type(c, first_entry->rhs, &val_ty);
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
      oakc_infer_type(c, expr->lhs, &coll_ty);
      if (oak_type_is_known(&coll_ty) &&
          (coll_ty.kind == OAK_TYPE_KIND_ARRAY ||
           coll_ty.kind == OAK_TYPE_KIND_MAP))
      {
        out->id = coll_ty.id;
        if (coll_ty.kind == OAK_TYPE_KIND_ARRAY &&
            oakc_trait_find_by_id(&c->traits, coll_ty.id))
          out->kind = OAK_TYPE_KIND_TRAIT;
      }
      return;
    }
    case OAK_NODE_EXPR_RECORD_LITERAL:
    {
      const struct oak_ast_node_t* path_node = expr->lhs;
      if (!path_node || path_node->kind != OAK_NODE_IMPORT_PATH)
        return;
      const struct oak_ast_node_t* type_seg = null;
      {
        struct oak_list_entry_t* pos;
        oak_list_for_each(pos, &path_node->children)
        {
          type_seg = oak_container_of(pos, struct oak_ast_node_t, link);
        }
      }
      if (!type_seg)
        return;
      const struct oak_registered_record_t* sd =
          oakc_records_find(&c->records,
                                           oak_token_text(type_seg->token),
                                           oak_token_size(type_seg->token));
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
      /* Cross-module enum variant: alias.EnumName.Variant → enum type. */
      {
        const struct oak_token_t* ename_tok = null;
        if (oak_compiler_match_module_member(c, recv, &ename_tok))
        {
          const char* ename = oak_token_text(ename_tok);
          const int ename_len = oak_token_size(ename_tok);
          if (oakc_is_enum_name(&c->enums, ename, ename_len))
          {
            const struct oak_enum_variant_t* ev =
                oakc_enums_find_qualified(
                    &c->enums, ename, oak_token_text(fname->token));
            out->id = ev ? ev->type_id : OAK_TYPE_VOID;
            return;
          }
        }
      }
      /* Local enum variant access: EnumName.Variant yields the enum's type. */
      if (recv->kind == OAK_NODE_IDENT)
      {
        const char* recv_name = oak_token_text(recv->token);
        const int recv_len = oak_token_size(recv->token);
        if (oakc_is_enum_name(&c->enums, recv_name, recv_len))
        {
          const struct oak_enum_variant_t* ev =
              oakc_enums_find_qualified(
                  &c->enums, recv_name, oak_token_text(fname->token));
          out->id = ev ? ev->type_id : OAK_TYPE_VOID;
          return;
        }
      }
      struct oak_type_t recv_ty;
      oakc_infer_type(c, recv, &recv_ty);
      const struct oak_registered_record_t* sd = null;
      const int idx =
          oakc_record_field_index(c,
                                recv_ty,
                                oak_token_text(fname->token),
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

const char* oakc_type_kind_name(struct oak_compiler_t* c,
                               const struct oak_type_t t)
{
  return oak_type_registry_name(&c->types, t.id);
}

/* Format a type name into a thread-local buffer for error messages.
 * Uses a small ring of buffers so two calls in the same varargs list
 * (e.g. "expected '%s', found '%s'") each return a distinct pointer. */
const char* oakc_type_full_name(struct oak_compiler_t* c,
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
             "[%s:%s]%s",
             oak_type_registry_name(&c->types, t.key_id),
             oak_type_registry_name(&c->types, t.id),
             t.is_weak ? " weak" : "");
    return buf;
  }
  if (t.kind == OAK_TYPE_KIND_ARRAY)
  {
    snprintf(buf, 128, "%s[]%s", oakc_type_kind_name(c, t),
             t.is_weak ? " weak" : "");
    return buf;
  }
  if (t.is_weak)
  {
    snprintf(buf, 128, "%s weak", oakc_type_kind_name(c, t));
    return buf;
  }
  return oakc_type_kind_name(c, t);
}

void oakc_reject_void(struct oak_compiler_t* c,
                    const struct oak_ast_node_t* expr)
{
  if (!expr)
    return;
  struct oak_type_t t;
  oakc_infer_type(c, expr, &t);
  if (oak_type_is_void(&t))
  {
    oak_compiler_error_at(
        c, expr->token, "this expression has no value (void)");
  }
}
