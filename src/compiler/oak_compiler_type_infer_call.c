#include "internal/oak_compiler.h"
#include "internal/oak_compiler_modules.h"

/* Infer the return type of a method call whose callee is a MEMBER_ACCESS node
 * (recv.method(...)).  Handles four cases in resolution order:
 *   1. mod.Type.method(...)  — cross-module static method
 *   2. alias.fn(...)         — cross-module free function
 *   3. Type.method(...)      — local static method (no local named Type)
 *   4. expr.method(...)      — instance method on record / builtin collection */

/* Resolve a native method's return type.  self_offset is unused, kept for
 * call-site symmetry. */
static void native_method_return_type(oak_compiler_t* c,
                                      const oak_registered_fn_t* sm,
                                      const oak_ast_node_t* call,
                                      int self_offset,
                                      oak_type_t* out)
{
  (void)c;
  (void)call;
  (void)self_offset;
  *out = sm->return_type;
  if (out->id == OAK_TYPE_VOID)
    out->kind = OAK_TYPE_KIND_SCALAR;
}

static void infer_method_call_type(oak_compiler_t* c,
                                   const oak_ast_node_t* callee,
                                   const oak_ast_node_t* call,
                                   oak_type_t* out)
{
  const oak_ast_node_t* recv = callee->lhs;
  const oak_ast_node_t* method = callee->rhs;
  const oak_ast_node_t* method_name = method;
  if (!recv || !method_name || method_name->kind != OAK_NODE_IDENT)
    return;
  const char* mn = oak_token_text(method_name->token);

  /* Case 1: mod.Type.method — cross-module static method. */
  if (recv->kind == OAK_NODE_MEMBER_ACCESS && recv->lhs && recv->rhs &&
      recv->lhs->kind == OAK_NODE_IDENT && recv->rhs->kind == OAK_NODE_IDENT)
  {
    const oak_module_t* dep = OAK_NULL;
    if (oak_compiler_module_export_record(c,
                                          oak_token_text(recv->lhs->token),
                                          oak_token_text(recv->rhs->token),
                                          &dep))
    {
      /* The record is exported, but an alias-only import left no local copy of
       * its layout, so the lookup below would find nothing. */
      oak_ensure_dep_named_type_imported(
          c, dep, oak_token_text(recv->rhs->token));
      const oak_registered_record_t* sd = oak_records_find_any(
          &c->records, oak_token_text(recv->rhs->token));
      const oak_registered_fn_t* sm =
          oak_find_record_method(sd, mn, 1);
      if (sm)
      {
        if (sm->decl)
        {
          const oak_ast_node_t* retn = oak_fn_return_type_node(sm->decl);
          if (retn)
            oak_lower_type_node(c, retn, out);
          else
            out->id = OAK_TYPE_VOID;
          if (out->id == OAK_TYPE_VOID)
            out->kind = OAK_TYPE_KIND_SCALAR;
        }
        else
          native_method_return_type(c, sm, call, 0, out);
        return;
      }
    }
  }

  if (recv->kind == OAK_NODE_IDENT)
  {
    const char* rname = oak_token_text(recv->token);

    /* Case 2: alias.fn — cross-module free function. */
    {
      const oak_module_t* dep = OAK_NULL;
      const oak_module_export_fn_t* fexp =
          oak_compiler_module_export_fn(c, rname, mn, &dep);
      if (fexp)
      {
        *out = fexp->return_type;
        /* The id names a type in the dependency's registry. Bring its layout
         * across, or `a.make_point().x` has a receiver the record registry has
         * never heard of. */
        oak_ensure_dep_type_imported(c, dep, out);
        if (out->id == OAK_TYPE_VOID)
          out->kind = OAK_TYPE_KIND_SCALAR;
        return;
      }
    }

    /* Case 3: Type.method — local static method (only if rname is not a local). */
    oak_type_t local_ty;
    oak_type_clear(&local_ty);
    if (!oak_local_type_get(c, rname, &local_ty))
    {
      const oak_registered_record_t* sd =
          oak_records_find(&c->records, rname);
      if (sd)
      {
        const oak_registered_fn_t* sm =
            oak_find_record_method(sd, mn, 1);
        if (sm)
        {
          if (sm->decl)
          {
            const oak_ast_node_t* retn = oak_fn_return_type_node(sm->decl);
            if (retn)
              oak_lower_type_node(c, retn, out);
            else
              out->id = OAK_TYPE_VOID;
            if (out->id == OAK_TYPE_VOID)
              out->kind = OAK_TYPE_KIND_SCALAR;
          }
          else
            native_method_return_type(c, sm, call, 0, out);
          return;
        }
      }
    }
  }

  /* Case 4: expr.method — infer receiver type, then look up instance method. */
  oak_type_t recv_ty;
  oak_infer_type(c, recv, &recv_ty);
  if (oak_type_is_known(&recv_ty) && recv_ty.kind == OAK_TYPE_KIND_SCALAR)
  {
    const oak_registered_record_t* sd =
        oak_records_find_by_id(&c->records, recv_ty.id);
    if (sd)
    {
      const oak_registered_fn_t* sm =
          oak_find_record_method(sd, mn, 0);
      if (sm)
      {
        if (sm->decl)
        {
          const oak_ast_node_t* retn = oak_fn_return_type_node(sm->decl);
          if (retn)
            oak_lower_type_node(c, retn, out);
          else
            out->id = OAK_TYPE_VOID;
          if (out->id == OAK_TYPE_VOID)
            out->kind = OAK_TYPE_KIND_SCALAR;
        }
        else
          native_method_return_type(c, sm, call, 1, out);
        return;
      }
      /* No declared method of that name, so it may be one of the builtins
       * every record carries. Dispatch already falls back this way; without
       * the same fallback here `p.to_string()` compiles and runs but infers
       * as void, which fails the moment the result is bound or concatenated. */
      const oak_method_binding_t* bm =
          oak_find_record_builtin_method(c, mn);
      if (bm)
      {
        out->id = bm->return_type_id;
        return;
      }
    }
    if (recv_ty.id == OAK_TYPE_STRING)
    {
      const oak_method_binding_t* sm =
          oak_find_string_method(c, mn);
      if (sm)
        out->id = sm->return_type_id;
    }
    return;
  }
  if (recv_ty.kind == OAK_TYPE_KIND_INTERFACE)
  {
    const oak_registered_interface_t* tr =
        oak_interface_find_by_id(&c->interfaces, recv_ty.id);
    if (tr)
    {
      const int slot = oak_interface_method_slot(tr, mn);
      if (slot >= 0)
      {
        const oak_interface_method_t* tm =
            oak_cget(tr->methods, (usize)slot);
        if (tm->sig_decl)
        {
          const oak_ast_node_t* retn =
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
  const oak_method_binding_t* m = OAK_NULL;
  if (recv_ty.kind == OAK_TYPE_KIND_ARRAY)
    m = oak_find_array_method(c, mn);
  else if (recv_ty.kind == OAK_TYPE_KIND_MAP)
    m = oak_find_map_method(c, mn);
  if (m)
    out->id = m->return_type_id;
}

/* Infer the return type of an OAK_NODE_FN_CALL expression. */
void oak_infer_fn_call_type(oak_compiler_t* c,
                            const oak_ast_node_t* expr,
                            oak_type_t* out)
{
  const oak_list_entry_t* first = expr->children.next;
  if (first == &expr->children)
    return;
  const oak_ast_node_t* callee =
      OAK_CONTAINER_OF(first, oak_ast_node_t, link);
  if (!callee)
    return;

  if (callee->kind == OAK_NODE_MEMBER_ACCESS)
  {
    infer_method_call_type(c, callee, expr, out);
    return;
  }

  if (callee->kind != OAK_NODE_IDENT)
    return;
  const char* cn = oak_token_text(callee->token);
  const oak_registered_fn_t* fe = oak_find_fn(c, cn);
  if (!fe)
    return;
  if (!fe->decl)
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
  const oak_ast_node_t* retn = oak_fn_return_type_node(fe->decl);
  if (retn)
    oak_lower_type_node(c, retn, out);
  else
    out->id = OAK_TYPE_VOID;
}
