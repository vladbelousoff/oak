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
  const usize mn_len = oak_token_length(method->token);

  /* Case 1: mod.Type.method — cross-module static method. */
  if (recv->kind == OAK_NODE_MEMBER_ACCESS && recv->lhs && recv->rhs &&
      recv->lhs->kind == OAK_NODE_IDENT && recv->rhs->kind == OAK_NODE_IDENT)
  {
    const struct oak_module_t* dep = oak_compiler_module_for_alias(
        c,
        oak_token_text(recv->lhs->token),
        oak_token_length(recv->lhs->token));
    if (dep && oak_module_find_export_record(dep,
                                             oak_token_text(recv->rhs->token),
                                             oak_token_length(recv->rhs->token)))
    {
      const struct oak_registered_record_t* sd =
          oc_records_find(
              &c->records,
              oak_token_text(recv->rhs->token),
              oak_token_length(recv->rhs->token));
      const struct oak_registered_fn_t* sm =
          oc_find_record_method(sd, mn, mn_len, 1);
      if (sm)
      {
        if (sm->decl)
        {
          const struct oak_ast_node_t* retn = oc_fn_return_type_node(sm->decl);
          if (retn)
            oc_lower_type_node(c, retn, out);
          else
            out->id = OAK_TYPE_VOID;
        }
        else
        {
          out->id = sm->return_type_id;
          out->kind = sm->return_kind;
        }
        if (out->id == OAK_TYPE_VOID)
          out->kind = OAK_TYPE_KIND_SCALAR;
        return;
      }
    }
  }

  if (recv->kind == OAK_NODE_IDENT)
  {
    const char* rname = oak_token_text(recv->token);
    const usize rlen = oak_token_length(recv->token);

    /* Case 2: alias.fn — cross-module free function. */
    {
      const struct oak_module_export_fn_t* fexp =
          oak_compiler_module_export_fn(c, rname, rlen, mn, mn_len, null);
      if (fexp)
      {
        if (fexp->return_type_node)
          oc_lower_type_node(c, fexp->return_type_node, out);
        else
        {
          out->id = fexp->return_type_id;
          out->kind = fexp->return_kind;
        }
        if (out->id == OAK_TYPE_VOID)
          out->kind = OAK_TYPE_KIND_SCALAR;
        return;
      }
    }

    /* Case 3: Type.method — local static method (only if rname is not a local). */
    struct oak_type_t local_ty;
    oak_type_clear(&local_ty);
    if (!oc_local_type_get(c, rname, rlen, &local_ty))
    {
      const struct oak_registered_record_t* sd =
          oc_records_find(&c->records, rname, rlen);
      if (sd)
      {
        const struct oak_registered_fn_t* sm =
            oc_find_record_method(sd, mn, mn_len, 1);
        if (sm)
        {
          if (sm->decl)
          {
            const struct oak_ast_node_t* retn = oc_fn_return_type_node(sm->decl);
            if (retn)
              oc_lower_type_node(c, retn, out);
            else
              out->id = OAK_TYPE_VOID;
          }
          else
          {
            out->id = sm->return_type_id;
            out->kind = sm->return_kind;
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
  oc_infer_type(c, recv, &recv_ty);
  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR)
  {
    const struct oak_registered_record_t* sd =
        oc_records_find_by_id(&c->records, recv_ty.id);
    if (sd)
    {
      const struct oak_registered_fn_t* sm =
          oc_find_record_method(sd, mn, mn_len, 0);
      if (sm)
      {
        if (sm->decl)
        {
          const struct oak_ast_node_t* retn = oc_fn_return_type_node(sm->decl);
          if (retn)
            oc_lower_type_node(c, retn, out);
          else
            out->id = OAK_TYPE_VOID;
        }
        else
        {
          out->id = sm->return_type_id;
          out->kind = sm->return_kind;
          if (out->id == OAK_TYPE_VOID)
            out->kind = OAK_TYPE_KIND_SCALAR;
        }
        return;
      }
    }
    if (recv_ty.id == OAK_TYPE_STRING)
    {
      const struct oak_method_binding_t* sm =
          oc_find_string_method(c, mn, mn_len);
      if (sm)
        out->id = sm->return_type_id;
    }
    return;
  }
  const struct oak_method_binding_t* m = null;
  if (recv_ty.kind == OAK_TYPE_KIND_ARRAY)
    m = oc_find_array_method(c, mn, mn_len);
  else if (recv_ty.kind == OAK_TYPE_KIND_MAP)
    m = oc_find_map_method(c, mn, mn_len);
  if (m)
    out->id = m->return_type_id;
}

/* Infer the return type of an OAK_NODE_FN_CALL expression. */
void oc_infer_fn_call_type(struct oak_compiler_t* c,
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
  const usize clen = oak_token_length(callee->token);
  const struct oak_registered_fn_t* fe = oc_find_fn(c, cn, clen);
  if (fe && !fe->decl)
  {
    if (strcmp(fe->name, "print") == 0)
    {
      out->id = OAK_TYPE_VOID;
      return;
    }
    out->id = fe->return_type_id;
    out->kind = fe->return_kind;
    if (out->id == OAK_TYPE_VOID)
      out->kind = OAK_TYPE_KIND_SCALAR;
    return;
  }
  if (!fe)
    return;
  const struct oak_ast_node_t* retn = oc_fn_return_type_node(fe->decl);
  if (retn)
    oc_lower_type_node(c, retn, out);
  else
    out->id = OAK_TYPE_VOID;
}
