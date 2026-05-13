#include "internal/oak_compiler.h"

#include <string.h>

void oak_trait_registry_init(struct oak_trait_registry_t* r)
{
  oak_dynarr_init(&r->traits, &r->trait_count, &r->trait_capacity);
  oak_dynarr_init(&r->impls, &r->impl_count, &r->impl_capacity);
}

void oak_trait_registry_free(struct oak_trait_registry_t* r)
{
  for (int i = 0; i < r->trait_count; ++i)
    oak_dynarr_free(&r->traits[i].methods,
                    &r->traits[i].method_count,
                    &r->traits[i].method_capacity);
  oak_dynarr_free(&r->traits, &r->trait_count, &r->trait_capacity);

  for (int i = 0; i < r->impl_count; ++i)
  {
    if (r->impls[i].vtable)
      oak_free(r->impls[i].vtable, OAK_SRC_LOC);
    r->impls[i].vtable = null;
    r->impls[i].vtable_count = 0;
  }
  oak_dynarr_free(&r->impls, &r->impl_count, &r->impl_capacity);
}

/* ---------- Trait coercion emission ---------- */

void oakc_emit_trait_coerce(struct oak_compiler_t* c,
                            const struct oak_ast_node_t* arg_expr,
                            struct oak_type_t want,
                            struct oak_code_loc_t loc)
{
  if (want.kind != OAK_TYPE_KIND_TRAIT || want.is_weak)
    return;

  const struct oak_registered_trait_t* tr =
      oakc_trait_find_by_id(&c->traits, want.id);
  if (!tr)
    return;

  struct oak_type_t got;
  oakc_infer_type(c, arg_expr, &got);
  if (!oak_type_is_known(&got))
    return;

  /* Trait-to-same-trait: already a trait object; no coercion needed. */
  if (oak_type_equal(&want, &got))
    return;

  const struct oak_registered_record_t* sd = null;
  if (got.kind == OAK_TYPE_KIND_SCALAR)
    sd = oakc_records_find_by_id(&c->records, got.id);

  if (!sd)
  {
    oak_compiler_error_at(
        c,
        arg_expr ? arg_expr->token : null,
        "cannot coerce type '%s' to trait '%s': not a record type",
        oakc_type_full_name(c, got),
        tr->name);
    return;
  }

  if (!oakc_record_satisfies_trait(c, sd, tr))
  {
    oak_compiler_error_at(
        c,
        arg_expr ? arg_expr->token : null,
        "type '%s' does not implement trait '%s'",
        sd->name,
        tr->name);
    return;
  }

  const u16 vtable_idx = oakc_get_or_build_vtable(c, sd, tr);
  if (c->has_error)
    return;
  oak_compiler_emit_op(c, OAK_OP_MAKE_TRAIT_OBJECT, loc,
                       OAK_ARG_U16(vtable_idx));
}

void oakc_emit_weak_coerce(struct oak_compiler_t* c,
                           const struct oak_ast_node_t* arg_expr,
                           struct oak_type_t want,
                           struct oak_code_loc_t loc)
{
  if (!want.is_weak)
    return;

  struct oak_type_t got;
  oakc_infer_type(c, arg_expr, &got);
  if (!oak_type_is_known(&got) || got.is_weak)
    return;

  if (!oak_type_equal_base(&want, &got))
    return;

  if (!oakc_expr_is_reference_place(c, arg_expr))
  {
    oak_compiler_error_at(
        c,
        arg_expr ? arg_expr->token : null,
        "cannot create weak reference from a temporary value");
    return;
  }

  oak_compiler_emit_op(c, OAK_OP_WEAKEN, loc);
}

/* ---------- Trait registration ---------- */

void oakc_register_program_traits(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind != OAK_NODE_TRAIT_DECL)
      continue;

    if (!item->lhs || item->lhs->kind != OAK_NODE_IDENT || !item->rhs)
    {
      oak_compiler_error_at(c, item->token, "malformed trait declaration");
      return;
    }

    const char* tname = oak_token_text(item->lhs->token);
    const usize tname_len = oak_token_length(item->lhs->token);

    if (oakc_trait_find(&c->traits, tname, tname_len))
    {
      oak_compiler_error_at(
          c, item->lhs->token, "duplicate trait '%s'", tname);
      return;
    }

    struct oak_registered_trait_t proto = {
      .name = tname,
      .name_len = tname_len,
      .trait_id = oak_type_registry_intern(&c->types, tname, tname_len),
      .methods = null,
      .method_count = 0,
      .method_capacity = 0,
    };

    if (proto.trait_id < 0)
    {
      oak_compiler_error_at(c, item->lhs->token, "type registry full");
      return;
    }

    oak_dynarr_push(&c->traits.traits,
                    &c->traits.trait_count,
                    &c->traits.trait_capacity,
                    &proto,
                    sizeof(proto));
    struct oak_registered_trait_t* tr =
        &c->traits.traits[c->traits.trait_count - 1];

    /* Walk trait members — each must be a FN_DECL. */
    const struct oak_ast_node_t* members = item->rhs;
    for (struct oak_list_entry_t* mp = members->children.next;
         mp != &members->children;
         mp = mp->next)
    {
      const struct oak_ast_node_t* mdecl =
          oak_container_of(mp, struct oak_ast_node_t, link);
      if (mdecl->kind != OAK_NODE_FN_DECL)
        continue;

      const struct oak_ast_node_t* name_node = oakc_fn_name_node(mdecl);
      if (!name_node)
      {
        oak_compiler_error_at(c, mdecl->token, "malformed trait method");
        return;
      }

      const char* mname = oak_token_text(name_node->token);
      const usize mname_len = oak_token_length(name_node->token);
      const int explicit_arity = oakc_count_fn_params(mdecl);
      const struct oak_ast_node_t* self_p = oakc_fn_self_param(mdecl);
      const int total_arity = self_p ? explicit_arity + 1 : explicit_arity;

      /* If the body is a real BLOCK (not just ';'), record the decl for
       * later compilation as a default implementation. */
      const struct oak_ast_node_t* body = oakc_fn_block(mdecl);
      struct oak_trait_method_t tm = {
        .name = mname,
        .name_len = mname_len,
        .arity = total_arity,
        .sig_decl = mdecl,
        .decl = (body && body->kind == OAK_NODE_BLOCK) ? mdecl : null,
      };
      oak_dynarr_push(&tr->methods,
                      &tr->method_count,
                      &tr->method_capacity,
                      &tm,
                      sizeof(tm));
    }
  }
}

/* ---------- fn TypeName.method_name registration ---------- */

const struct oak_ast_node_t* oakc_method_decl_type_ident(
    const struct oak_ast_node_t* decl)
{
  if (!decl->lhs || !decl->lhs->lhs)
    return null;
  return decl->lhs->lhs->lhs; /* METHOD_PROTO -> METHOD_HEAD -> type IDENT */
}

void oakc_register_method_decls(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* raw_item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* item = oakc_unwrap_decl(raw_item);
    if (!item || item->kind != OAK_NODE_METHOD_DECL)
      continue;

    const struct oak_ast_node_t* type_ident = oakc_method_decl_type_ident(item);
    if (!type_ident)
    {
      oak_compiler_error_at(c, null, "malformed method declaration");
      return;
    }

    const char* rname = oak_token_text(type_ident->token);
    const usize rname_len = oak_token_length(type_ident->token);

    struct oak_registered_record_t* sd = null;
    for (int i = 0; i < c->records.entries.count; ++i)
    {
      if (oak_name_eq(c->records.entries.items[i].name,
                      c->records.entries.items[i].name_len,
                      rname, rname_len))
      {
        sd = &c->records.entries.items[i];
        break;
      }
    }
    if (!sd)
    {
      oak_compiler_error_at(
          c, type_ident->token, "method declaration for unknown type '%s'", rname);
      return;
    }

    oakc_register_method_on_record(c, raw_item, item, sd);
    if (c->has_error)
      return;
  }
}

/* ---------- fn TypeName.method_name body compilation ---------- */

void oakc_compile_method_decl_bodies(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oakc_unwrap_decl(oak_container_of(pos, struct oak_ast_node_t, link));
    if (!item || item->kind != OAK_NODE_METHOD_DECL)
      continue;

    const struct oak_ast_node_t* body = oakc_fn_block(item);
    if (!body || body->kind != OAK_NODE_BLOCK)
    {
      if (!c->allow_bodyless_fns)
        oak_compiler_error_at(c, item->token, "method has no body");
      continue;
    }

    const struct oak_ast_node_t* type_ident = oakc_method_decl_type_ident(item);
    if (!type_ident)
      continue;
    const char* rname = oak_token_text(type_ident->token);
    const usize rname_len = oak_token_length(type_ident->token);

    const struct oak_registered_record_t* sd =
        oakc_records_find(&c->records, rname, rname_len);
    if (!sd)
      continue;

    const struct oak_ast_node_t* name_node = oakc_fn_name_node(item);
    if (!name_node)
      continue;
    const char* mname = oak_token_text(name_node->token);
    const usize mname_len = oak_token_length(name_node->token);

    const struct oak_registered_fn_t* sm =
        oakc_find_record_method(sd, mname, mname_len, 0);
    if (!sm)
      sm = oakc_find_record_method(sd, mname, mname_len, 1);
    if (!sm)
      continue;

    const usize fn_offset = c->chunk->count;
    struct oak_obj_fn_t* fn_obj =
        oak_as_fn(c->chunk->constants[sm->const_idx]);
    fn_obj->code_offset = fn_offset;

    const int is_static = oakc_fn_self_param(item) == null;
    oakc_compile_fn_body(c, item, is_static ? null : sd);
    if (c->has_error)
      return;
  }
}

/* ---------- Structural conformance check ---------- */

/* Returns 1 if concrete record type `sd` structurally satisfies trait `tr`
 * (i.e. has all required methods with compatible arity). */
int oakc_record_satisfies_trait(struct oak_compiler_t* c,
                                const struct oak_registered_record_t* sd,
                                const struct oak_registered_trait_t* tr)
{
  for (int i = 0; i < tr->method_count; ++i)
  {
    const struct oak_trait_method_t* tm = &tr->methods[i];
    const struct oak_registered_fn_t* sm =
        oakc_find_record_method(sd, tm->name, tm->name_len, 0);
    if (!sm)
      return 0;
    if (sm->arity != tm->arity)
      return 0;

    /* Also compare parameter types and return type when both sides have AST
     * decls; this catches cases like a record implementing area()->string
     * when the trait declares area()->number. */
    if (tm->sig_decl && sm->decl)
    {
      /* Return type. */
      struct oak_type_t tr_ret, sm_ret;
      oak_type_clear(&tr_ret);
      oak_type_clear(&sm_ret);
      const struct oak_ast_node_t* tr_ret_node =
          oakc_fn_return_type_node(tm->sig_decl);
      const struct oak_ast_node_t* sm_ret_node =
          oakc_fn_return_type_node(sm->decl);
      if (tr_ret_node)
        oakc_lower_type_node(c, tr_ret_node, &tr_ret);
      if (sm_ret_node)
        oakc_lower_type_node(c, sm_ret_node, &sm_ret);
      if (oak_type_is_known(&tr_ret) && oak_type_is_known(&sm_ret) &&
          !oak_type_equal(&tr_ret, &sm_ret))
        return 0;

      /* Parameter types (excluding self). */
      const int explicit_params = tm->arity - 1;
      for (int j = 0; j < explicit_params; ++j)
      {
        const struct oak_ast_node_t* tp = oakc_fn_param_at(tm->sig_decl, j);
        const struct oak_ast_node_t* sp = oakc_fn_param_at(sm->decl, j);
        if (!tp || !sp)
          continue;
        const struct oak_ast_node_t* tp_type = oakc_fn_param_type_node(tp);
        const struct oak_ast_node_t* sp_type = oakc_fn_param_type_node(sp);
        if (!tp_type || !sp_type)
          continue;
        struct oak_type_t want_p, got_p;
        oak_type_clear(&want_p);
        oak_type_clear(&got_p);
        oakc_lower_type_node(c, tp_type, &want_p);
        oakc_lower_type_node(c, sp_type, &got_p);
        if (oak_type_is_known(&want_p) && oak_type_is_known(&got_p) &&
            !oak_type_equal(&want_p, &got_p))
          return 0;
      }
    }
  }
  return 1;
}

/* ---------- Vtable construction ---------- */

/* Build (or return cached) vtable array const_idx for (sd, tr).
 * The vtable is an OAK_OBJ_ARRAY of function values, one per trait method.
 * Must be called after all fn bodies have been compiled (const_idx stable). */
u16 oakc_get_or_build_vtable(struct oak_compiler_t* c,
                             const struct oak_registered_record_t* sd,
                             const struct oak_registered_trait_t* tr)
{
  struct oak_trait_impl_t* impl =
      oakc_trait_impl_find(&c->traits, sd->type_id, tr->trait_id);

  if (!impl)
  {
    struct oak_trait_impl_t proto = {
      .trait_id = tr->trait_id,
      .record_type_id = sd->type_id,
      .vtable = oak_alloc((usize)tr->method_count * sizeof(u16), OAK_SRC_LOC),
      .vtable_count = tr->method_count,
      .vtable_array_const_idx = 0,
      .vtable_built = 0,
    };

    for (int i = 0; i < tr->method_count; ++i)
    {
      const struct oak_trait_method_t* tm = &tr->methods[i];
      const struct oak_registered_fn_t* sm =
          oakc_find_record_method(sd, tm->name, tm->name_len, 0);
      proto.vtable[i] = sm ? sm->const_idx : 0;
    }
    oak_dynarr_push(&c->traits.impls,
                    &c->traits.impl_count,
                    &c->traits.impl_capacity,
                    &proto,
                    sizeof(proto));
    impl = &c->traits.impls[c->traits.impl_count - 1];
  }

  if (impl->vtable_built)
    return impl->vtable_array_const_idx;

  /* Build vtable as an OAK_OBJ_ARRAY of function values. */
  struct oak_obj_array_t* arr = oak_array_new();
  for (int i = 0; i < tr->method_count; ++i)
  {
    const struct oak_value_t fn_val = c->chunk->constants[impl->vtable[i]];
    oak_array_push(arr, fn_val);
  }

  const u16 arr_idx = oak_compiler_intern_constant(
      c, OAK_VALUE_OBJ(&arr->obj));
  /* The constant table stores a raw value (no incref), so the fn_obj-style
   * ownership is: creation sets refcount=1, table slot owns that ref.
   * Do NOT decref here — the table now owns the only reference. */

  impl->vtable_array_const_idx = arr_idx;
  impl->vtable_built = 1;
  return arr_idx;
}
