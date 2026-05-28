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

  /* Case 1: mod.Type.method — cross-module static method. */
  if (recv->kind == OAK_NODE_MEMBER_ACCESS && recv->lhs && recv->rhs &&
      recv->lhs->kind == OAK_NODE_IDENT && recv->rhs->kind == OAK_NODE_IDENT)
  {
    if (oak_compiler_module_export_record(c,
                                          oak_token_text(recv->lhs->token),
                                          oak_token_text(recv->rhs->token),
                                          null))
    {
      const struct oak_registered_record_t* sd =
          oak_records_find(
              &c->records,
              oak_token_text(recv->rhs->token),
              oak_token_size(recv->rhs->token));
      const struct oak_registered_fn_t* sm =
          oak_find_record_method(sd, mn, 1);
      if (sm)
      {
        if (sm->decl)
        {
          const struct oak_ast_node_t* retn = oak_fn_return_type_node(sm->decl);
          if (retn)
            oak_lower_type_node(c, retn, out);
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
    const int rlen = oak_token_size(recv->token);

    /* Case 2: alias.fn — cross-module free function. */
    {
      const struct oak_module_export_fn_t* fexp =
          oak_compiler_module_export_fn(c, rname, mn, null);
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
    if (!oak_local_type_get(c, rname, &local_ty))
    {
      const struct oak_registered_record_t* sd =
          oak_records_find(&c->records, rname, rlen);
      if (sd)
      {
        const struct oak_registered_fn_t* sm =
            oak_find_record_method(sd, mn, 1);
        if (sm)
        {
          if (sm->decl)
          {
            const struct oak_ast_node_t* retn = oak_fn_return_type_node(sm->decl);
            if (retn)
              oak_lower_type_node(c, retn, out);
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
  oak_infer_type(c, recv, &recv_ty);
  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR)
  {
    const struct oak_registered_record_t* sd =
        oak_records_find_by_id(&c->records, recv_ty.id);
    if (sd)
    {
      const struct oak_registered_fn_t* sm =
          oak_find_record_method(sd, mn, 0);
      if (sm)
      {
        if (sm->decl)
        {
          const struct oak_ast_node_t* retn = oak_fn_return_type_node(sm->decl);
          if (retn)
            oak_lower_type_node(c, retn, out);
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
          oak_find_string_method(c, mn);
      if (sm)
        out->id = sm->return_type_id;
    }
    return;
  }
  if (recv_ty.kind == OAK_TYPE_KIND_TRAIT)
  {
    const struct oak_registered_trait_t* tr =
        oak_trait_find_by_id(&c->traits, recv_ty.id);
    if (tr)
    {
      const int slot = oak_trait_method_slot(tr, mn);
      if (slot >= 0)
      {
        const struct oak_trait_method_t* tm = &tr->methods[slot];
        if (tm->sig_decl)
        {
          const struct oak_ast_node_t* retn =
              oak_fn_return_type_node(tm->sig_decl);
          if (retn)
            oak_lower_type_node(c, retn, out);
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
    m = oak_find_array_method(c, mn);
  else if (recv_ty.kind == OAK_TYPE_KIND_MAP)
    m = oak_find_map_method(c, mn);
  if (m)
    out->id = m->return_type_id;
}

static void infer_generic_call_type(struct oak_compiler_t* c,
                                    const struct oak_registered_fn_t* fe,
                                    const struct oak_ast_node_t* call,
                                    struct oak_type_t* out)
{
  const struct oak_generic_def_t* def =
      &c->generics.defs[fe->generic_def_index];

  /* Activate the callee's generic params on the compiler so that type-node
   * lowering below resolves param IDs to OAK_TYPE_PARAM_BASE+i.  These two
   * fields MUST be restored before returning from this function — every
   * early-return path between here and the restore at the end must preserve
   * the pairing or it will leak the wrong context to subsequent calls. */
  struct oak_generic_param_t* saved_gp = c->generic_params;
  int saved_gpc = c->generic_param_count;
  c->generic_params = def->params;
  c->generic_param_count = def->param_count;

  struct oak_type_t bindings[OAK_MAX_GENERIC_PARAMS];
  for (int i = 0; i < def->param_count; ++i)
    oak_type_clear(&bindings[i]);

  const struct oak_list_entry_t* first = call->children.next;
  struct oak_list_entry_t* pos = first->next;
  int arg_idx = 0;
  for (; pos != &call->children; pos = pos->next, ++arg_idx)
  {
    const struct oak_ast_node_t* aw =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* ae = aw;
    if (aw->kind == OAK_NODE_FN_CALL_ARG)
      ae = aw->child;

    const struct oak_ast_node_t* param = oak_fn_param_at(fe->decl, arg_idx);
    if (!param)
      continue;
    const struct oak_ast_node_t* tn = oak_fn_param_type_node(param);
    if (!tn)
      continue;

    struct oak_type_t want;
    oak_lower_type_node(c, tn, &want);

    int pi = -1;
    if (want.kind == OAK_TYPE_KIND_PARAM)
      pi = (int)(want.id - OAK_TYPE_PARAM_BASE);
    else if (want.id >= OAK_TYPE_PARAM_BASE)
      pi = (int)(want.id - OAK_TYPE_PARAM_BASE);

    if (pi < 0 || pi >= def->param_count || oak_type_is_known(&bindings[pi]))
      continue;

    struct oak_type_t got;
    oak_infer_type(c, ae, &got);
    if (!oak_type_is_known(&got))
      continue;

    if (want.kind == OAK_TYPE_KIND_PARAM)
    {
      bindings[pi] = got;
    }
    else if (want.kind == OAK_TYPE_KIND_ARRAY &&
             got.kind == OAK_TYPE_KIND_ARRAY)
    {
      struct oak_type_t elem;
      oak_type_clear(&elem);
      elem.id = got.id;
      bindings[pi] = elem;
    }
  }

  const struct oak_ast_node_t* retn = oak_fn_return_type_node(fe->decl);
  if (retn)
  {
    oak_lower_type_node(c, retn, out);
    if (out->kind == OAK_TYPE_KIND_PARAM)
    {
      oak_assert(out->id >= OAK_TYPE_PARAM_BASE &&
                 out->id < OAK_TYPE_PARAM_BASE + OAK_MAX_GENERIC_PARAMS);
      int pi = (int)(out->id - OAK_TYPE_PARAM_BASE);
      if (pi >= 0 && pi < def->param_count && oak_type_is_known(&bindings[pi]))
        *out = bindings[pi];
    }
    else if (out->id >= OAK_TYPE_PARAM_BASE)
    {
      int pi = (int)(out->id - OAK_TYPE_PARAM_BASE);
      if (pi >= 0 && pi < def->param_count && oak_type_is_known(&bindings[pi]))
        out->id = bindings[pi].id;
    }
    if (out->kind == OAK_TYPE_KIND_MAP && out->key_id >= OAK_TYPE_PARAM_BASE)
    {
      int pi = (int)(out->key_id - OAK_TYPE_PARAM_BASE);
      if (pi >= 0 && pi < def->param_count && oak_type_is_known(&bindings[pi]))
        out->key_id = bindings[pi].id;
    }
  }
  else
    out->id = OAK_TYPE_VOID;

  /* Paired with the save above.  param_count must not have drifted: any code
   * path that mutates these fields without restoring them is a bug. */
  oak_assert(c->generic_param_count == def->param_count);
  c->generic_params = saved_gp;
  c->generic_param_count = saved_gpc;
}

/* Infer the return type of an OAK_NODE_FN_CALL expression. */
void oak_infer_fn_call_type(struct oak_compiler_t* c,
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
  const int clen = oak_token_size(callee->token);
  const struct oak_registered_fn_t* fe = oak_find_fn(c, cn, clen);
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
  if (fe->generic_param_count > 0 && fe->generic_def_index >= 0)
  {
    infer_generic_call_type(c, fe, expr, out);
    return;
  }
  const struct oak_ast_node_t* retn = oak_fn_return_type_node(fe->decl);
  if (retn)
    oak_lower_type_node(c, retn, out);
  else
    out->id = OAK_TYPE_VOID;
}
