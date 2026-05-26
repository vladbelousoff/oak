#include "internal/oak_compiler.h"
#include "oak_compiler_modules.h"

/* Infer the return type of a method call whose callee is a MEMBER_ACCESS node
 * (recv.method(...)).  Handles four cases in resolution order:
 *   1. mod.Type.method(...)  — cross-module static method
 *   2. alias.fn(...)         — cross-module free function
 *   3. Type.method(...)      — local static method (no local named Type)
 *   4. expr.method(...)      — instance method on record / builtin collection */
static void infer_method_call_type(struct oak_compiler_t* c,
                                   const struct oak_ast_node_t* callee,
                                   struct oak_type_t* out)
{
  const struct oak_ast_node_t* recv = callee->lhs;
  const struct oak_ast_node_t* method = callee->rhs;
  if (!recv || !method || method->kind != OAK_NODE_IDENT)
    return;
  const char* mn = oak_token_text(method->token);
  const usize mn_len = oak_token_size(method->token);

  /* Case 1: mod.Type.method — cross-module static method. */
  if (recv->kind == OAK_NODE_MEMBER_ACCESS && recv->lhs && recv->rhs &&
      recv->lhs->kind == OAK_NODE_IDENT && recv->rhs->kind == OAK_NODE_IDENT)
  {
    if (oak_compiler_module_export_record(c,
                                          oak_token_text(recv->lhs->token),
                                          oak_token_size(recv->lhs->token),
                                          oak_token_text(recv->rhs->token),
                                          oak_token_size(recv->rhs->token),
                                          null))
    {
      const struct oak_registered_record_t* sd =
          oakc_records_find(
              &c->records,
              oak_token_text(recv->rhs->token),
              oak_token_size(recv->rhs->token));
      const struct oak_registered_fn_t* sm =
          oakc_find_record_method(sd, mn, 1);
      if (sm)
      {
        if (sm->decl)
        {
          const struct oak_ast_node_t* retn = oakc_fn_return_type_node(sm->decl);
          if (retn)
            oakc_lower_type_node(c, retn, out);
          else
            out->id = OAK_TYPE_VOID;
        }
        else
          *out = sm->return_type;
        if (out->id == OAK_TYPE_VOID)
          out->kind = OAK_TYPE_KIND_SCALAR;
        return;
      }
    }
  }

  if (recv->kind == OAK_NODE_IDENT)
  {
    const char* rname = oak_token_text(recv->token);
    const usize rlen = oak_token_size(recv->token);

    /* Case 2: alias.fn — cross-module free function. */
    {
      const struct oak_module_export_fn_t* fexp =
          oak_compiler_module_export_fn(c, rname, rlen, mn, mn_len, null);
      if (fexp)
      {
        *out = fexp->return_type;
        if (out->id == OAK_TYPE_VOID)
          out->kind = OAK_TYPE_KIND_SCALAR;
        return;
      }
    }

    /* Case 3: Type.method — local static method (only if rname is not a local). */
    struct oak_type_t local_ty;
    oak_type_clear(&local_ty);
    if (!oakc_local_type_get(c, rname, &local_ty))
    {
      const struct oak_registered_record_t* sd =
          oakc_records_find(&c->records, rname, rlen);
      if (sd)
      {
        const struct oak_registered_fn_t* sm =
            oakc_find_record_method(sd, mn, 1);
        if (sm)
        {
          if (sm->decl)
          {
            const struct oak_ast_node_t* retn = oakc_fn_return_type_node(sm->decl);
            if (retn)
              oakc_lower_type_node(c, retn, out);
            else
              out->id = OAK_TYPE_VOID;
          }
          else
          {
            *out = sm->return_type;
            if (out->id == OAK_TYPE_VOID)
              out->kind = OAK_TYPE_KIND_SCALAR;
          }
          return;
        }
      }
    }
  }

  /* Case 4: expr.method — infer receiver type, then look up instance method. */
  struct oak_type_t recv_ty;
  oakc_infer_type(c, recv, &recv_ty);
  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR)
  {
    const struct oak_registered_record_t* sd =
        oakc_records_find_by_id(&c->records, recv_ty.id);
    if (sd)
    {
      const struct oak_registered_fn_t* sm =
          oakc_find_record_method(sd, mn, 0);
      if (sm)
      {
        if (sm->decl)
        {
          const struct oak_ast_node_t* retn = oakc_fn_return_type_node(sm->decl);
          if (retn)
            oakc_lower_type_node(c, retn, out);
          else
            out->id = OAK_TYPE_VOID;
        }
        else
        {
          *out = sm->return_type;
          if (out->id == OAK_TYPE_VOID)
            out->kind = OAK_TYPE_KIND_SCALAR;
        }
        return;
      }
    }
    if (recv_ty.id == OAK_TYPE_STRING)
    {
      const struct oak_method_binding_t* sm =
          oakc_find_string_method(c, mn);
      if (sm)
        out->id = sm->return_type_id;
    }
    return;
  }
  if (recv_ty.kind == OAK_TYPE_KIND_TRAIT)
  {
    const struct oak_registered_trait_t* tr =
        oakc_trait_find_by_id(&c->traits, recv_ty.id);
    if (tr)
    {
      const int slot = oakc_trait_method_slot(tr, mn);
      if (slot >= 0)
      {
        const struct oak_trait_method_t* tm = &tr->methods[slot];
        if (tm->sig_decl)
        {
          const struct oak_ast_node_t* retn =
              oakc_fn_return_type_node(tm->sig_decl);
          if (retn)
            oakc_lower_type_node(c, retn, out);
          else
            out->id = OAK_TYPE_VOID;
        }
        else if (oak_type_is_known(&tm->return_type))
        {
          *out = tm->return_type;
        }
        return;
      }
    }
    return;
  }
  const struct oak_method_binding_t* m = null;
  if (recv_ty.kind == OAK_TYPE_KIND_ARRAY)
    m = oakc_find_array_method(c, mn);
  else if (recv_ty.kind == OAK_TYPE_KIND_MAP)
    m = oakc_find_map_method(c, mn);
  if (m)
    out->id = m->return_type_id;
}

/* Infer the return type of an OAK_NODE_FN_CALL expression. */
void oakc_infer_fn_call_type(struct oak_compiler_t* c,
                            const struct oak_ast_node_t* expr,
                            struct oak_type_t* out)
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
    infer_method_call_type(c, callee, out);
    return;
  }

  if (callee->kind != OAK_NODE_IDENT)
    return;
  const char* cn = oak_token_text(callee->token);
  const usize clen = oak_token_size(callee->token);
  const struct oak_registered_fn_t* fe = oakc_find_fn(c, cn, clen);
  if (fe && !fe->decl)
  {
    if (strcmp(fe->name, "print") == 0)
    {
      out->id = OAK_TYPE_VOID;
      return;
    }
    *out = fe->return_type;
    if (out->id == OAK_TYPE_VOID)
      out->kind = OAK_TYPE_KIND_SCALAR;
    return;
  }
  if (!fe)
    return;
  const struct oak_ast_node_t* retn = oakc_fn_return_type_node(fe->decl);
  if (retn)
    oakc_lower_type_node(c, retn, out);
  else
    out->id = OAK_TYPE_VOID;
}
