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
        oak_free(r->allocator, methods[mi].param_types, OAK_HERE);
    }
    oak_destroy(interfaces[i].methods);
  }
  oak_destroy(r->interfaces);

  oak_interface_impl_t* impls =
      OAK_DATA(oak_interface_impl_t, r->impls);
  for (usize i = 0; i < oak_size(r->impls); ++i)
  {
    if (impls[i].vtable)
      oak_free(r->allocator, impls[i].vtable, OAK_HERE);
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
                          "type '%s' does not implement interface '%s'; "
                          "add 'implements %s'",
                          sd->name,
                          tr->name,
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

      /* An interface value is a record instance behind a vtable, so there is
         nothing for a receiverless member to dispatch on. Refusing it here is
         also what keeps oak_interface_method_t from needing an is_static:
         every member has a receiver. */
      const oak_ast_node_t* mode = oak_fn_receiver_mode(mdecl);
      if (mode && mode->kind == OAK_NODE_STATIC_KEYWORD)
      {
        oak_compiler_error_at(c,
                              mode->token,
                              "interface member '%s' cannot be 'static': an "
                              "interface dispatches on an instance",
                              mname);
        return;
      }

      const int explicit_arity = oak_count_fn_params(mdecl);
      /* An interface member always dispatches on an instance, so its arity
         always counts a receiver. `static` is refused below. */
      const int total_arity = explicit_arity + 1;

      /* If the body is a real BLOCK (not just ';'), record the decl for
       * later compilation as a default implementation. */
      const oak_ast_node_t* body = oak_fn_block(mdecl);
      oak_interface_method_t tm = {
        .name = mname,
        .arity = total_arity,
        .sig_decl = mdecl,
        .decl = (body && body->kind == OAK_NODE_BLOCK) ? mdecl : null,
        .self_is_mut = oak_fn_self_is_mut(mdecl),
        .param_types = null,
        .return_type = { 0 },
      };
      oak_assert(oak_push_back(tr->methods, &tm));
    }
  }
}

/* Why a record falls short of an interface, phrased to follow
 * "record 'X' does not implement interface 'Y': ". Written into the caller's
 * buffer so the message survives oak_type_full_name's rotating buffers. */
#define OAK_INTERFACE_WHY_MAX 192

#define MISMATCH(...)                                                          \
  do                                                                           \
  {                                                                            \
    if (why)                                                                   \
      snprintf(why, OAK_INTERFACE_WHY_MAX, __VA_ARGS__);                       \
    return 0;                                                                  \
  } while (0)

/* Returns 1 if `sd` has every method `tr` requires with a compatible
 * signature. `why` is optional: the coercion path passes null and only wants
 * the answer, the declaration check passes a buffer and reports what it
 * says. */
static int record_methods_match_interface(oak_compiler_t* c,
                                          const oak_registered_record_t* sd,
                                          const oak_registered_interface_t* tr,
                                          char* why)
{
  const oak_interface_method_t* methods =
      OAK_CDATA(oak_interface_method_t, tr->methods);
  for (usize i = 0; i < oak_size(tr->methods); ++i)
  {
    const oak_interface_method_t* tm = &methods[i];
    const oak_registered_fn_t* sm =
        oak_find_record_method(sd, tm->name, 0);
    if (!sm)
      MISMATCH("no method '%s'", tm->name);
    /* A record method carries its mut-ness on the AST when it was declared in
       source, and in param_mut_flags[0] when it came from a binding or an
       import. */
    const int sm_self_is_mut =
        sm->decl ? oak_fn_self_is_mut(sm->decl)
                 : sm->param_mut_flags && sm->param_mut_flags[0];
    if (sm_self_is_mut != tm->self_is_mut)
      MISMATCH("method '%s' is declared '%s', interface declares '%s'",
               tm->name,
               sm_self_is_mut ? "fn mut" : "fn",
               tm->self_is_mut ? "fn mut" : "fn");
    /* Arities count the receiver, the message counts what the caller writes. */
    const int sm_params = sm->arity - 1;
    if (sm->arity != tm->arity)
      MISMATCH("method '%s' takes %d parameter%s, interface declares %d",
               tm->name,
               sm_params,
               sm_params == 1 ? "" : "s",
               tm->arity - 1);

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
        MISMATCH("method '%s' returns '%s', interface declares '%s'",
                 tm->name,
                 oak_type_full_name(c, sm_ret),
                 oak_type_full_name(c, tr_ret));

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
          MISMATCH("method '%s' takes '%s' for parameter %d, interface "
                   "declares '%s'",
                   tm->name,
                   oak_type_full_name(c, got_p),
                   j + 1,
                   oak_type_full_name(c, want_p));
      }
    }
  }
  return 1;
}

#undef MISMATCH

static int record_declares_interface(const oak_registered_record_t* sd,
                                     const oak_registered_interface_t* tr)
{
  const oak_type_t* interfaces = OAK_CDATA(oak_type_t, sd->interfaces);
  for (usize i = 0; i < oak_size(sd->interfaces); ++i)
    if (interfaces[i].kind == OAK_TYPE_KIND_INTERFACE &&
        interfaces[i].id == tr->interface_id)
      return 1;
  return 0;
}

int oak_record_satisfies_interface(oak_compiler_t* c,
                                   const oak_registered_record_t* sd,
                                   const oak_registered_interface_t* tr)
{
  return record_declares_interface(sd, tr) &&
         record_methods_match_interface(c, sd, tr, null);
}

void oak_validate_record_interfaces(oak_compiler_t* c)
{
  oak_registered_record_t* records =
      OAK_DATA(oak_registered_record_t, c->records.entries);
  for (usize ri = 0; ri < oak_size(c->records.entries); ++ri)
  {
    oak_registered_record_t* record = &records[ri];
    const char* const* names =
        (const char* const*)oak_cdata(record->interface_names);
    for (usize ii = 0; ii < oak_size(record->interface_names); ++ii)
    {
      const oak_registered_interface_t* tr =
          oak_interface_find(&c->interfaces, names[ii]);
      if (!tr)
      {
        /* A clause in this program's source is a claim that can be checked
           here and now. A native binding's is not: the same
           oak_compile_options_t compiles many programs, and only some of them
           declare the interface. Where it is absent the record simply does
           not implement it, and any coercion says so at its own site. */
        if (!record->decl_token)
          continue;
        oak_compiler_error_at(c, record->decl_token,
                              "record '%s' declares unknown interface '%s'",
                              record->name, names[ii]);
        return;
      }
      if (!record_declares_interface(record, tr))
      {
        const oak_type_t interface_type = {
          .id = tr->interface_id,
          .kind = OAK_TYPE_KIND_INTERFACE,
        };
        oak_assert(oak_push_back(record->interfaces, &interface_type));
      }
      char why[OAK_INTERFACE_WHY_MAX] = { 0 };
      if (!record_methods_match_interface(c, record, tr, why))
      {
        oak_compiler_error_at(
            c, record->decl_token,
            "record '%s' does not implement interface '%s': %s",
            record->name, tr->name, why);
        return;
      }
    }
  }
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
      .vtable = oak_alloc(c->allocator, method_count * sizeof(u16), OAK_HERE),
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
