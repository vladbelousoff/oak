#include "internal/oak_compiler.h"

static int register_record_field_decls(oak_compiler_t* c,
                                       oak_registered_record_t* slot,
                                       const oak_ast_node_t* fields_wrap,
                                       const char* record_name,
                                       const oak_token_t* err_ctx_token);

/* Walk all top-level record declarations and register each in the compiler's
 * record registry. The record's type id is interned into the type registry so
 * later passes (function param types, record literals) can resolve them. */
/* Resolve the TYPE_NAME child to an IDENT for a record declaration. */
static const oak_ast_node_t* record_decl_name_ident(
    oak_compiler_t* c,
    const oak_ast_node_t* item,
    const oak_ast_node_t* type_name_node)
{
  const oak_ast_node_t* name_ident = type_name_node;
  if (name_ident->kind == OAK_NODE_RECORD_DECL_HEADER_IMPL)
    name_ident = name_ident->lhs;
  if (name_ident->kind == OAK_NODE_TYPE_NAME)
  {
    const oak_list_entry_t* tn_first = name_ident->children.next;
    if (tn_first == &name_ident->children)
    {
      oak_compiler_error_at(
          c, item->token, "record type name must be an identifier");
      return null;
    }
    name_ident = oak_container_of(tn_first, oak_ast_node_t, link);
  }
  if (name_ident->kind != OAK_NODE_IDENT)
  {
    oak_compiler_error_at(
        c, item->token, "record type name must be an identifier");
    return null;
  }
  return name_ident;
}

static const oak_ast_node_t* record_decl_implementations(
    const oak_ast_node_t* type_name_node)
{
  return type_name_node &&
                 type_name_node->kind == OAK_NODE_RECORD_DECL_HEADER_IMPL
             ? type_name_node->rhs
             : null;
}

/* Record the names from an `implements I, J` clause on `slot`. */
static int collect_declared_interfaces(oak_compiler_t* c,
                                       oak_registered_record_t* slot,
                                       const oak_ast_node_t* type_name_node,
                                       const char* record_name)
{
  const oak_ast_node_t* implementations =
      record_decl_implementations(type_name_node);
  if (!implementations)
    return 1;

  oak_list_entry_t* ipos;
  oak_list_for_each(ipos, &implementations->children)
  {
    const oak_ast_node_t* impl_name =
        oak_container_of(ipos, oak_ast_node_t, link);
    if (!impl_name || impl_name->kind != OAK_NODE_IDENT)
      continue;
    const char* interface_name = oak_token_text(impl_name->token);

    const char* const* names =
        (const char* const*)oak_cdata(slot->interface_names);
    for (usize i = 0; i < oak_size(slot->interface_names); ++i)
      if (strcmp(names[i], interface_name) == 0)
      {
        oak_compiler_error_at(c, impl_name->token,
                              "duplicate interface '%s' in record '%s'",
                              interface_name, record_name);
        return 0;
      }
    oak_assert(oak_push_back(slot->interface_names, &interface_name));
  }
  return 1;
}

static const oak_bind_type_t* native_record_binding(
    const oak_compiler_t* c,
    const char* name)
{
  if (!c->opts)
    return null;
  oak_bind_type_t** native_types =
      OAK_DATA(oak_bind_type_t*, c->opts->native_types);
  for (usize i = 0; i < oak_size(c->opts->native_types); ++i)
  {
    const oak_bind_type_t* native = native_types[i];
    if (!native || native->kind != OAK_BIND_TYPE_RECORD || !native->name)
      continue;
    if (strcmp(native->name, name) == 0)
      return native;
  }
  return null;
}

static oak_type_t native_field_type(const oak_bind_field_t* field)
{
  oak_type_t type;
  oak_lower_bind_ref(&field->type, &type);
  return type;
}

static const oak_bind_field_t* native_record_field(
    const oak_bind_type_t* native,
    const char* name)
{
  const oak_bind_field_t* fields =
      OAK_CDATA(oak_bind_field_t, native->fields);
  for (usize i = 0; i < oak_size(native->fields); ++i)
  {
    const oak_bind_field_t* field = &fields[i];
    if (strcmp(field->name, name) == 0)
      return field;
  }
  return null;
}

static int decl_implements(const oak_ast_node_t* implementations,
                           const char* name)
{
  if (!implementations)
    return 0;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &implementations->children)
  {
    const oak_ast_node_t* ident = oak_container_of(pos, oak_ast_node_t, link);
    if (ident && ident->kind == OAK_NODE_IDENT &&
        strcmp(oak_token_text(ident->token), name) == 0)
      return 1;
  }
  return 0;
}

/* A native record names its interfaces twice: through
 * oak_bind_type_implements on the binding, and in the `implements` clause of
 * the Oak declaration that mirrors it. Neither side is subordinate to the
 * other, so they have to agree -- the same rule the field list already
 * follows. */
static int native_record_interfaces_match(oak_compiler_t* c,
                                          const oak_bind_type_t* native,
                                          const oak_ast_node_t* type_name_node,
                                          const oak_ast_node_t* name_ident)
{
  const oak_ast_node_t* implementations =
      record_decl_implementations(type_name_node);

  char* const* bound = OAK_DATA(char*, native->interface_names);
  for (usize i = 0; i < oak_size(native->interface_names); ++i)
  {
    if (decl_implements(implementations, bound[i]))
      continue;
    oak_compiler_error_at(c,
                          name_ident->token,
                          "native record '%s' is bound as implementing '%s', "
                          "but its declaration does not say 'implements %s'",
                          native->name,
                          bound[i],
                          bound[i]);
    return 0;
  }

  if (!implementations)
    return 1;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &implementations->children)
  {
    const oak_ast_node_t* ident = oak_container_of(pos, oak_ast_node_t, link);
    if (!ident || ident->kind != OAK_NODE_IDENT)
      continue;
    const char* name = oak_token_text(ident->token);
    int found = 0;
    for (usize i = 0; i < oak_size(native->interface_names) && !found; ++i)
      found = strcmp(bound[i], name) == 0;
    if (!found)
    {
      oak_compiler_error_at(c,
                            ident->token,
                            "native record '%s' declares 'implements %s', but "
                            "its binding does not",
                            native->name,
                            name);
      return 0;
    }
  }
  return 1;
}

static int native_record_decl_matches(oak_compiler_t* c,
                                      const oak_bind_type_t* native,
                                      const oak_ast_node_t* item,
                                      const oak_ast_node_t* type_name_node,
                                      const oak_ast_node_t* name_ident)
{
  if (!native_record_interfaces_match(c, native, type_name_node, name_ident))
    return 0;

  if (item->kind == OAK_NODE_RECORD_DECL_EMPTY)
    return 1;

  const oak_ast_node_t* fields_wrap = item->rhs;
  if (!fields_wrap || fields_wrap->kind != OAK_NODE_RECORD_MEMBERS)
  {
    oak_compiler_error_at(c, item->token, "malformed record declaration");
    return 0;
  }

  /* Counts fields only — a native record declares its methods in the same
     member list, and those are matched against the binding separately. */
  int field_count = 0;
  for (oak_list_entry_t* fpos = fields_wrap->children.next;
       fpos != &fields_wrap->children;
       fpos = fpos->next)
  {
    const oak_ast_node_t* member =
        oak_container_of(fpos, oak_ast_node_t, link);
    if (member->kind == OAK_NODE_RECORD_FIELD_DECL)
      ++field_count;
  }

  if ((usize)field_count != oak_size(native->fields))
  {
    oak_compiler_error_at(c,
                          name_ident->token,
                          "native record '%s' declaration has %d fields, "
                          "but binding has %d",
                          native->name,
                          field_count,
                          (int)oak_size(native->fields));
    return 0;
  }

  for (oak_list_entry_t* fpos = fields_wrap->children.next;
       fpos != &fields_wrap->children;
       fpos = fpos->next)
  {
    const oak_ast_node_t* fdecl =
        oak_container_of(fpos, oak_ast_node_t, link);
    /* Methods share the member list; oak_register_program_methods handles
       them, and rejects any member that is neither a field nor a fn. */
    if (fdecl->kind != OAK_NODE_RECORD_FIELD_DECL)
      continue;
    if (!fdecl->lhs || !fdecl->rhs)
    {
      oak_compiler_error_at(c, item->token, "malformed record field");
      return 0;
    }
    if (fdecl->lhs->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, fdecl->lhs->token, "record field must be 'name : type'");
      return 0;
    }

    const char* field_name = oak_token_text(fdecl->lhs->token);
    const oak_bind_field_t* native_field =
        native_record_field(native, field_name);
    if (!native_field)
    {
      oak_compiler_error_at(c,
                            fdecl->lhs->token,
                            "field '%s' is not bound on native record '%s'",
                            field_name,
                            native->name);
      return 0;
    }

    oak_type_t declared_type;
    oak_type_clear(&declared_type);
    oak_lower_type_node(c, fdecl->rhs, &declared_type);
    if (c->has_error)
      return 0;
    const oak_type_t bound_type = native_field_type(native_field);
    if (!oak_type_equal(&declared_type, &bound_type))
    {
      oak_compiler_error_at(c,
                            fdecl->rhs->token ? fdecl->rhs->token
                                               : fdecl->lhs->token,
                            "field '%s' type does not match native record "
                            "'%s' binding",
                            field_name,
                            native->name);
      return 0;
    }
  }

  return 1;
}

void oak_register_program_records(oak_compiler_t* c,
                                           const oak_ast_node_t* program)
{
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const oak_ast_node_t* raw_item =
        oak_container_of(pos, oak_ast_node_t, link);
    const oak_ast_node_t* item = oak_unwrap_decl(raw_item);

    const int is_empty = item && item->kind == OAK_NODE_RECORD_DECL_EMPTY;
    if (!item || (item->kind != OAK_NODE_RECORD_DECL && !is_empty))
      continue;

    /* RECORD_DECL_EMPTY: child = RECORD_DECL_HEADER
     * RECORD_DECL:       lhs   = RECORD_DECL_HEADER, rhs = RECORD_MEMBERS */
    const oak_ast_node_t* type_name_node =
        is_empty ? item->child : item->lhs;
    if (!type_name_node || (!is_empty && !item->rhs))
    {
      oak_compiler_error_at(c, item->token, "malformed record declaration");
      return;
    }

    const oak_ast_node_t* name_ident =
        record_decl_name_ident(c, item, type_name_node);
    if (!name_ident || c->has_error)
      return;

    const char* name = oak_token_text(name_ident->token);
    const oak_registered_record_t* existing =
        oak_records_find(&c->records, name);
    if (existing)
    {
      const oak_bind_type_t* native =
          native_record_binding(c, name);
      if (native && existing->type_id == native->resolved_type_id)
      {
        if (!native_record_decl_matches(
                c, native, item, type_name_node, name_ident))
          return;

        /* The interface list came from the binding when the type was
           registered, and native_record_decl_matches has just confirmed the
           declaration's clause says the same, so there is nothing to merge.
           Recording the declaration token is still worth it: it makes this a
           record declared in this source, which is what lets a bad interface
           name be reported here rather than left to a coercion site. */
        const usize* entry_idx =
            (const usize*)oak_cfind_str(c->records.by_name, name);
        if (entry_idx)
        {
          oak_registered_record_t* slot =
              oak_get(c->records.entries, *entry_idx);
          slot->decl_token = name_ident->token;
        }

        int attr_count = 0;
        const char** attrs = oak_extract_attrs(c->allocator, raw_item, &attr_count);
        if (attr_count > 0)
        {
          oak_compiler_dispatch_attr_cbs(
              c, attrs, attr_count, name, OAK_ATTR_TARGET_RECORD,
              null, 0, null, 0, -1);
        }
        continue;
      }
      oak_compiler_error_at(
          c, name_ident->token, "duplicate record '%s'", name);
      return;
    }

    oak_registered_record_t proto = { 0 };
    proto.name = name;
    proto.decl_token = name_ident->token;
    proto.type_id = oak_type_registry_intern(&c->types, name);
    proto.fields =
        oak_vector_new(c->allocator, sizeof(oak_record_field_t));
    proto.methods =
        oak_vector_new(c->allocator, sizeof(oak_registered_fn_t));
    proto.interface_names =
        oak_vector_new(c->allocator, sizeof(const char*));
    proto.interfaces = oak_vector_new(c->allocator, sizeof(oak_type_t));
    oak_assert(proto.fields && proto.methods && proto.interface_names &&
               proto.interfaces);
    proto.attrs = oak_extract_attrs(c->allocator, raw_item, &proto.attr_count);

    /* Pre-scan fields for attribute callbacks. */
    oak_attr_field_info_t* finfo = null;
    int finfo_count = 0;
    if (proto.attr_count > 0 && !is_empty && item->rhs &&
        item->rhs->kind == OAK_NODE_RECORD_MEMBERS)
    {
      const oak_ast_node_t* fw = item->rhs;
      oak_list_entry_t* fp;
      oak_list_for_each(fp, &fw->children) { ++finfo_count; }
      if (finfo_count > 0)
      {
        finfo = oak_alloc(c->allocator,
                          (usize)finfo_count * sizeof(oak_attr_field_info_t),
                          OAK_HERE);
        int fi = 0;
        oak_list_for_each(fp, &fw->children)
        {
          const oak_ast_node_t* fd =
              oak_container_of(fp, oak_ast_node_t, link);
          if (fd->kind == OAK_NODE_RECORD_FIELD_DECL && fd->lhs && fd->rhs)
          {
            finfo[fi].name = oak_token_text(fd->lhs->token);
            finfo[fi].type_id = -1;
            if (fd->rhs->kind == OAK_NODE_IDENT)
            {
              finfo[fi].type_name = oak_token_text(fd->rhs->token);
            }
            else
            {
              finfo[fi].type_name = "";
            }
            ++fi;
          }
        }
        finfo_count = fi;
      }
    }

    oak_compiler_dispatch_attr_cbs(
        c, proto.attrs, proto.attr_count, name, OAK_ATTR_TARGET_RECORD,
        null, 0, finfo, finfo_count, -1);
    if (finfo)
      oak_free(c->allocator, finfo, OAK_HERE);

    if (proto.type_id < 0)
    {
      oak_compiler_error_at(
          c, name_ident->token, "type registry full while declaring record");
      return;
    }

    const u16 owner_module_id =
        c->current_module ? c->current_module->module_id : OAK_MODULE_ID_NONE;
    if (!oak_compiler_declare_symbol(
            c, name_ident->token, name, OAK_SYMBOL_RECORD,
            (int)oak_size(c->records.entries), owner_module_id, 0))
      return;
    if (oak_decl_is_exported(raw_item))
      oak_compiler_mark_symbol_exported(c, name);
    oak_registered_record_t* slot =
        oak_record_registry_insert(&c->records, &proto);

    if (!collect_declared_interfaces(c, slot, type_name_node, name))
      return;

    if (is_empty)
      continue; /* no fields to register */

    const oak_ast_node_t* fields_wrap = item->rhs;
    if (fields_wrap->kind != OAK_NODE_RECORD_MEMBERS)
    {
      oak_compiler_error_at(c, item->token, "malformed record declaration");
      return;
    }

    /* Reject 'record Foo {}' — use 'record Foo;' for empty records. A body
       holding only methods is fine, which is what native records look like. */
    if (fields_wrap->children.next == &fields_wrap->children)
    {
      oak_compiler_error_at(
          c, name_ident->token,
          "record '%s' has no fields or methods; use 'record %s;' instead "
          "of '{}'",
          name, name);
      return;
    }

    const int field_ok =
        register_record_field_decls(c, slot, fields_wrap, name, item->token);
    if (!field_ok || c->has_error)
      return;
  }
}

/* Collect field declarations in source order. */
static int register_record_field_decls(oak_compiler_t* c,
                                       oak_registered_record_t* slot,
                                       const oak_ast_node_t* fields_wrap,
                                       const char* record_name,
                                       const oak_token_t* err_ctx_token)
{
  for (oak_list_entry_t* fpos = fields_wrap->children.next;
       fpos != &fields_wrap->children;
       fpos = fpos->next)
  {
    const oak_ast_node_t* fdecl =
        oak_container_of(fpos, oak_ast_node_t, link);
    if (fdecl->kind != OAK_NODE_RECORD_FIELD_DECL)
      continue; /* a method; see oak_register_program_methods */
    if (!fdecl->lhs || !fdecl->rhs)
    {
      oak_compiler_error_at(c, err_ctx_token, "malformed record field");
      return 0;
    }
    const oak_ast_node_t* fname = fdecl->lhs;
    const oak_ast_node_t* ftype = fdecl->rhs;
    if (fname->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, fdecl->lhs->token, "record field must be 'name : type'");
      return 0;
    }

    const char* fn_name = oak_token_text(fname->token);
    const oak_record_field_t* existing =
        OAK_CDATA(oak_record_field_t, slot->fields);
    for (usize i = 0; i < oak_size(slot->fields); ++i)
    {
      if (strcmp(existing[i].name, fn_name) == 0)
      {
        oak_compiler_error_at(c,
                              fname->token,
                              "duplicate field '%s' in record '%s'",
                              fn_name,
                              record_name);
        return 0;
      }
    }

    oak_record_field_t f = {
      .name = fn_name,
    };
    oak_type_clear(&f.type);
    oak_lower_type_node(c, ftype, &f.type);
    if (c->has_error)
      return 0;
    if (!oak_type_is_known(&f.type))
    {
      oak_compiler_error_at(
          c, ftype->token ? ftype->token : fname->token,
          "record field must be 'name : type'");
      return 0;
    }
    oak_assert(oak_push_back(slot->fields, &f));
  }
  return 1;
}
