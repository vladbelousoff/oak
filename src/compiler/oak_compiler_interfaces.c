#include "internal/oak_compiler.h"

#include <string.h>

void oak_interface_registry_init(oak_interface_registry_t* r,
                                 oak_allocator_t* allocator)
{
  r->allocator = allocator;
  r->interfaces =
      oak_vector_new(allocator, sizeof(oak_registered_interface_t));
  r->impls = oak_vector_new(allocator, sizeof(oak_interface_impl_t));
  oak_assert(r->interfaces && r->impls);
}

void oak_interface_registry_free(oak_interface_registry_t* r)
{
  oak_registered_interface_t* interfaces =
      OAK_DATA(oak_registered_interface_t, r->interfaces);
  for (usize i = 0; i < oak_size(r->interfaces); ++i)
  {
    oak_interface_method_t* methods =
        OAK_DATA(oak_interface_method_t, interfaces[i].methods);
    for (usize mi = 0; mi < oak_size(interfaces[i].methods); ++mi)
    {
      if (methods[mi].param_types)
        OAK_FREE(r->allocator, methods[mi].param_types);
    }
    oak_destroy(interfaces[i].methods);
  }
  oak_destroy(r->interfaces);

  oak_interface_impl_t* impls =
      OAK_DATA(oak_interface_impl_t, r->impls);
  for (usize i = 0; i < oak_size(r->impls); ++i)
  {
    if (impls[i].vtable)
      OAK_FREE(r->allocator, impls[i].vtable);
    impls[i].vtable = null;
    impls[i].vtable_count = 0;
  }
  oak_destroy(r->impls);
}

void oak_emit_interface_coerce(oak_compiler_t* c,
                               const oak_ast_node_t* arg_expr,
                               oak_type_t want,
                               oak_code_loc_t loc)
{
  if (want.kind != OAK_TYPE_KIND_INTERFACE || want.is_weak)
    return;

  const oak_registered_interface_t* tr =
      oak_interface_find_by_id(&c->interfaces, want.id);
  if (!tr)
    return;

  oak_type_t got;
  oak_infer_type(c, arg_expr, &got);
  if (!oak_type_is_known(&got))
    return;

  /* Interface-to-same-interface: already an interface object; no coercion
   * needed. */
  if (oak_type_equal(&want, &got))
    return;

  const oak_registered_record_t* sd = null;
  if (got.kind == OAK_TYPE_KIND_SCALAR)
    sd = oak_records_find_by_id(&c->records, got.id);

  if (!sd)
  {
    oak_compiler_error_at(
        c,
        arg_expr ? arg_expr->token : null,
        "cannot coerce type '%s' to interface '%s': not a record type",
        oak_type_full_name(c, got),
        tr->name);
    return;
  }

  if (!oak_record_satisfies_interface(c, sd, tr))
  {
    oak_compiler_error_at(c,
                          arg_expr ? arg_expr->token : null,
                          "type '%s' does not implement interface '%s'",
                          sd->name,
                          tr->name);
    return;
  }

  const u16 vtable_idx = oak_get_or_build_vtable(c, sd, tr);
  if (c->has_error)
    return;
  oak_compiler_emit_op(
      c, OAK_OP_MAKE_INTERFACE_OBJECT, loc, OAK_ARG_U16(vtable_idx));
}

void oak_emit_weak_coerce(oak_compiler_t* c,
                          const oak_ast_node_t* arg_expr,
                          oak_type_t want,
                          oak_code_loc_t loc)
{
  if (!want.is_weak)
    return;

  oak_type_t got;
  oak_infer_type(c, arg_expr, &got);
  if (!oak_type_is_known(&got) || got.is_weak)
    return;

  if (!oak_type_equal_base(&want, &got))
    return;

  if (!oak_expr_is_reference_place(c, arg_expr))
  {
    oak_compiler_error_at(
        c,
        arg_expr ? arg_expr->token : null,
        "cannot create weak reference from a temporary value");
    return;
  }

  oak_compiler_emit_op(c, OAK_OP_WEAKEN, loc);
}

void oak_register_program_interfaces(oak_compiler_t* c,
                                     const oak_ast_node_t* program)
{
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const oak_ast_node_t* raw_item =
        oak_container_of(pos, oak_ast_node_t, link);
    const oak_ast_node_t* item = oak_unwrap_decl(raw_item);
    if (!item || item->kind != OAK_NODE_INTERFACE_DECL)
      continue;

    if (!item->lhs || item->lhs->kind != OAK_NODE_IDENT || !item->rhs)
    {
      oak_compiler_error_at(c, item->token, "malformed interface declaration");
      return;
    }

    const char* tname = oak_token_text(item->lhs->token);
    if (!tname || tname[0] != 'I')
    {
      oak_compiler_error_at(
          c, item->lhs->token, "interface names must start with 'I'");
      return;
    }

    if (oak_interface_find(&c->interfaces, tname))
    {
      oak_compiler_error_at(
          c, item->lhs->token, "duplicate interface '%s'", tname);
      return;
    }

    oak_registered_interface_t proto = {
      .name = tname,
      .interface_id = oak_type_registry_intern(&c->types, tname),
      .methods = null,
    };

    proto.methods =
        oak_vector_new(c->allocator, sizeof(oak_interface_method_t));
    oak_assert(proto.methods);

    if (proto.interface_id < 0)
    {
      oak_compiler_error_at(c, item->lhs->token, "type registry full");
      return;
    }

    const u16 owner_module_id =
        c->current_module ? c->current_module->module_id : OAK_MODULE_ID_NONE;
    if (!oak_compiler_declare_symbol(c,
                                     item->lhs->token,
                                     tname,
                                     OAK_SYMBOL_INTERFACE,
                                     (int)oak_size(c->interfaces.interfaces),
                                     owner_module_id,
                                     0))
      return;
    if (oak_decl_is_exported(raw_item))
      oak_compiler_mark_symbol_exported(c, tname);
    oak_assert(oak_push_back(c->interfaces.interfaces, &proto));
    oak_registered_interface_t* tr =
        oak_get(c->interfaces.interfaces,
                oak_size(c->interfaces.interfaces) - 1);

    /* Walk interface members — each must be a FN_DECL. */
    const oak_ast_node_t* members = item->rhs;
    for (oak_list_entry_t* mp = members->children.next;
         mp != &members->children;
         mp = mp->next)
    {
      const oak_ast_node_t* mdecl =
          oak_container_of(mp, oak_ast_node_t, link);
      if (mdecl->kind != OAK_NODE_FN_DECL)
        continue;

      const oak_ast_node_t* name_node = oak_fn_name_node(mdecl);
      if (!name_node)
      {
        oak_compiler_error_at(c, mdecl->token, "malformed interface method");
        return;
      }

      const char* mname = oak_token_text(name_node->token);
      const int explicit_arity = oak_count_fn_params(mdecl);
      const oak_ast_node_t* self_p = oak_fn_self_param(mdecl);
      const int total_arity = self_p ? explicit_arity + 1 : explicit_arity;

      /* If the body is a real BLOCK (not just ';'), record the decl for
       * later compilation as a default implementation. */
      const oak_ast_node_t* body = oak_fn_block(mdecl);
      oak_interface_method_t tm = {
        .name = mname,
        .arity = total_arity,
        .sig_decl = mdecl,
        .decl = (body && body->kind == OAK_NODE_BLOCK) ? mdecl : null,
        .self_is_mut = (self_p && oak_self_is_mut(self_p)) ? 1 : 0,
        .param_types = null,
        .return_type = { 0 },
      };
      oak_assert(oak_push_back(tr->methods, &tm));
    }
  }
}

const oak_ast_node_t*
oak_method_decl_type_ident(const oak_ast_node_t* decl)
{
  if (!decl->lhs || !decl->lhs->lhs)
    return null;
  return decl->lhs->lhs->lhs; /* METHOD_PROTO -> METHOD_HEAD -> type IDENT */
}

void oak_register_method_decls(oak_compiler_t* c,
                               const oak_ast_node_t* program)
{
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const oak_ast_node_t* raw_item =
        oak_container_of(pos, oak_ast_node_t, link);
    const oak_ast_node_t* item = oak_unwrap_decl(raw_item);
    if (!item || item->kind != OAK_NODE_METHOD_DECL)
      continue;

    const oak_ast_node_t* type_ident = oak_method_decl_type_ident(item);
    if (!type_ident)
    {
      oak_compiler_error_at(c, null, "malformed method declaration");
      return;
    }

    const char* rname = oak_token_text(type_ident->token);

    oak_registered_record_t* sd = null;
    oak_registered_record_t* records =
        OAK_DATA(oak_registered_record_t, c->records.entries);
    for (usize i = 0; i < oak_size(c->records.entries); ++i)
    {
      if (oak_name_eq(records[i].name, rname))
      {
        sd = &records[i];
        break;
      }
    }
    if (!sd)
    {
      oak_compiler_error_at(c,
                            type_ident->token,
                            "method declaration for unknown type '%s'",
                            rname);
      return;
    }

    oak_register_method_on_record(c, raw_item, item, sd);
    if (c->has_error)
      return;
  }
}

void oak_compile_method_decl_bodies(oak_compiler_t* c,
                                    const oak_ast_node_t* program)
{
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const oak_ast_node_t* item =
        oak_unwrap_decl(oak_container_of(pos, oak_ast_node_t, link));
    if (!item || item->kind != OAK_NODE_METHOD_DECL)
      continue;

    const oak_ast_node_t* body = oak_fn_block(item);
    if (!body || body->kind != OAK_NODE_BLOCK)
    {
      if (!c->allow_bodyless_fns)
        oak_compiler_error_at(c, item->token, "method has no body");
      continue;
    }

    const oak_ast_node_t* type_ident = oak_method_decl_type_ident(item);
    if (!type_ident)
      continue;
    const char* rname = oak_token_text(type_ident->token);

    const oak_registered_record_t* sd =
        oak_records_find(&c->records, rname);
    if (!sd)
      continue;

    const oak_ast_node_t* name_node = oak_fn_name_node(item);
    if (!name_node)
      continue;
    const char* mname = oak_token_text(name_node->token);

    const oak_registered_fn_t* sm = oak_find_record_method(sd, mname, 0);
    if (!sm)
      sm = oak_find_record_method(sd, mname, 1);
    if (!sm)
      continue;

    const usize fn_offset = oak_chunk_size(c->chunk);
    oak_obj_fn_t* fn_obj =
        oak_as_fn(oak_chunk_constant(c->chunk, (usize)sm->const_idx));
    fn_obj->code_offset = fn_offset;

    const int is_static = oak_fn_self_param(item) == null;
    oak_compile_fn_body(c, item, is_static ? null : sd);
    if (c->has_error)
      return;
  }
}

/* Returns 1 if concrete record type `sd` structurally satisfies interface `tr`
 * (i.e. has all required methods with compatible arity). */
int oak_record_satisfies_interface(oak_compiler_t* c,
                                   const oak_registered_record_t* sd,
                                   const oak_registered_interface_t* tr)
{
  const oak_interface_method_t* methods =
      OAK_CDATA(oak_interface_method_t, tr->methods);
  for (usize i = 0; i < oak_size(tr->methods); ++i)
  {
    const oak_interface_method_t* tm = &methods[i];
    const oak_registered_fn_t* sm =
        oak_find_record_method(sd, tm->name, 0);
    if (!sm)
      return 0;
    if (sm->arity != tm->arity)
      return 0;

    /* Compare parameter and return types. When both sides have AST decls
     * (local interface), lower from the AST. When the interface is imported
     * (sig_decl == null), use the pre-lowered param_types/return_type. */
    {
      oak_type_t tr_ret;
      oak_type_clear(&tr_ret);
      if (tm->sig_decl)
      {
        const oak_ast_node_t* rn = oak_fn_return_type_node(tm->sig_decl);
        if (rn)
          oak_lower_type_node(c, rn, &tr_ret);
      }
      else
      {
        tr_ret = tm->return_type;
      }
      oak_type_t sm_ret;
      oak_type_clear(&sm_ret);
      if (sm->decl)
      {
        const oak_ast_node_t* rn = oak_fn_return_type_node(sm->decl);
        if (rn)
          oak_lower_type_node(c, rn, &sm_ret);
      }
      else
      {
        sm_ret = sm->return_type;
      }
      if (oak_type_is_known(&tr_ret) && oak_type_is_known(&sm_ret) &&
          !oak_type_equal(&tr_ret, &sm_ret))
        return 0;

      const int explicit_params = tm->arity - 1;
      for (int j = 0; j < explicit_params; ++j)
      {
        oak_type_t want_p;
        oak_type_clear(&want_p);
        if (tm->sig_decl)
        {
          const oak_ast_node_t* tp = oak_fn_param_at(tm->sig_decl, j);
          if (tp)
          {
            const oak_ast_node_t* tn = oak_fn_param_type_node(tp);
            if (tn)
              oak_lower_type_node(c, tn, &want_p);
          }
        }
        else if (tm->param_types)
        {
          want_p = tm->param_types[j + 1];
        }
        oak_type_t got_p;
        oak_type_clear(&got_p);
        if (sm->decl)
        {
          const oak_ast_node_t* sp = oak_fn_param_at(sm->decl, j);
          if (sp)
          {
            const oak_ast_node_t* sn = oak_fn_param_type_node(sp);
            if (sn)
              oak_lower_type_node(c, sn, &got_p);
          }
        }
        else if (sm->param_types)
        {
          got_p = sm->param_types[j + 1];
        }
        if (oak_type_is_known(&want_p) && oak_type_is_known(&got_p) &&
            !oak_type_equal(&want_p, &got_p))
          return 0;
      }
    }
  }
  return 1;
}

/* Build (or return cached) vtable array const_idx for (sd, tr).
 * The vtable is an OAK_OBJ_ARRAY of function values, one per interface method.
 * Must be called after all fn bodies have been compiled (const_idx stable). */
u16 oak_get_or_build_vtable(oak_compiler_t* c,
                            const oak_registered_record_t* sd,
                            const oak_registered_interface_t* tr)
{
  oak_interface_impl_t* impl =
      oak_interface_impl_find(&c->interfaces, sd->type_id, tr->interface_id);

  const usize method_count = oak_size(tr->methods);
  const oak_interface_method_t* methods =
      OAK_CDATA(oak_interface_method_t, tr->methods);

  if (!impl)
  {
    oak_interface_impl_t proto = {
      .interface_id = tr->interface_id,
      .record_type_id = sd->type_id,
      .vtable = OAK_ALLOC(c->allocator, method_count * sizeof(u16)),
      .vtable_count = (int)method_count,
      .vtable_array_const_idx = 0,
      .vtable_built = 0,
    };

    for (usize i = 0; i < method_count; ++i)
    {
      const oak_registered_fn_t* sm =
          oak_find_record_method(sd, methods[i].name, 0);
      proto.vtable[i] = sm ? sm->const_idx : 0;
    }
    oak_assert(oak_push_back(c->interfaces.impls, &proto));
    impl = oak_get(c->interfaces.impls, oak_size(c->interfaces.impls) - 1);
  }

  if (impl->vtable_built)
    return impl->vtable_array_const_idx;

  /* Build vtable as an OAK_OBJ_ARRAY of function values.
     For imported methods, resolve the function from the source module. */
  oak_obj_array_t* arr = oak_array_new(c->allocator);
  for (usize i = 0; i < method_count; ++i)
  {
    const oak_interface_method_t* tm = &methods[i];
    const oak_registered_fn_t* sm =
        oak_find_record_method(sd, tm->name, 0);
    oak_value_t fn_val;
    if (sm && sm->source_module_id != OAK_MODULE_ID_NONE && c->module_registry)
    {
      const oak_module_t* src_mod =
          oak_module_registry_get(c->module_registry, sm->source_module_id);
      if (src_mod && src_mod->chunk &&
          (usize)sm->source_const_idx < oak_size(src_mod->chunk->constants))
        fn_val = oak_chunk_constant(src_mod->chunk, (usize)sm->source_const_idx);
      else
        fn_val = oak_chunk_constant(c->chunk, (usize)impl->vtable[i]);
    }
    else
    {
      fn_val = oak_chunk_constant(c->chunk, (usize)impl->vtable[i]);
    }
    /* Compiler-created vtables and functions both live in shared table 0. */
    oak_assert(oak_array_push(arr, fn_val));
  }

  const u16 arr_idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&arr->obj));
  /* The constant table stores a raw value (no incref), so the fn_obj-style
   * ownership is: creation sets refcount=1, table slot owns that ref.
   * Do NOT decref here — the table now owns the only reference. */

  impl->vtable_array_const_idx = arr_idx;
  impl->vtable_built = 1;
  return arr_idx;
}
