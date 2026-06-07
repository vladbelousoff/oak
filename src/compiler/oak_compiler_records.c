#include "internal/oak_compiler.h"

static int register_record_field_decls(struct oak_compiler_t* c,
                                       struct oak_registered_record_t* slot,
                                       const struct oak_ast_node_t* fields_wrap,
                                       const char* record_name,
                                       const struct oak_token_t* err_ctx_token);

/* Walk all top-level record declarations and register each in the compiler's
 * record registry. The record's type id is interned into the type registry so
 * later passes (function param types, record literals) can resolve them. */
/* Resolve the TYPE_NAME child to an IDENT for a record declaration. */
static const struct oak_ast_node_t* record_decl_name_ident(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* item,
    const struct oak_ast_node_t* type_name_node)
{
  const struct oak_ast_node_t* name_ident = type_name_node;
  if (name_ident->kind == OAK_NODE_TYPE_NAME)
  {
    const struct oak_list_entry_t* tn_first = name_ident->children.next;
    if (tn_first == &name_ident->children)
    {
      oak_compiler_error_at(
          c, item->token, "record type name must be an identifier");
      return null;
    }
    name_ident = oak_container_of(tn_first, struct oak_ast_node_t, link);
  }
  if (name_ident->kind != OAK_NODE_IDENT)
  {
    oak_compiler_error_at(
        c, item->token, "record type name must be an identifier");
    return null;
  }
  return name_ident;
}

static const struct oak_bind_type_t* native_record_binding(
    const struct oak_compiler_t* c,
    const char* name,
    int name_len)
{
  if (!c->opts)
    return null;
  for (int i = 0; i < oak_dynarr_count(c->opts->native_types); ++i)
  {
    const struct oak_bind_type_t* native = c->opts->native_types[i];
    if (!native || native->kind != OAK_BIND_TYPE_RECORD || !native->name)
      continue;
    if ((int)strlen(native->name) == name_len &&
        strncmp(native->name, name, name_len) == 0)
      return native;
  }
  return null;
}

static struct oak_type_t native_field_type(const struct oak_bind_field_t* field)
{
  struct oak_type_t type;
  oak_type_clear(&type);
  type.kind = field->type.kind;
  type.id = field->type.id;
  if (field->type.kind == OAK_TYPE_KIND_MAP)
    type.key_id = field->type.key_id;
  return type;
}

static const struct oak_bind_field_t* native_record_field(
    const struct oak_bind_type_t* native,
    const char* name,
    int name_len)
{
  for (int i = 0; i < oak_dynarr_count(native->fields); ++i)
  {
    const struct oak_bind_field_t* field = &native->fields[i];
    if ((int)strlen(field->name) == name_len &&
        strncmp(field->name, name, name_len) == 0)
      return field;
  }
  return null;
}

static int native_record_decl_matches(struct oak_compiler_t* c,
                                      const struct oak_bind_type_t* native,
                                      const struct oak_ast_node_t* item,
                                      const struct oak_ast_node_t* name_ident)
{
  if (item->kind == OAK_NODE_RECORD_DECL_EMPTY)
    return 1;

  const struct oak_ast_node_t* fields_wrap = item->rhs;
  if (!fields_wrap || fields_wrap->kind != OAK_NODE_RECORD_FIELDS)
  {
    oak_compiler_error_at(c, item->token, "malformed record declaration");
    return 0;
  }

  int field_count = 0;
  for (struct oak_list_entry_t* fpos = fields_wrap->children.next;
       fpos != &fields_wrap->children;
       fpos = fpos->next)
    ++field_count;

  if (field_count != oak_dynarr_count(native->fields))
  {
    oak_compiler_error_at(c,
                          name_ident->token,
                          "native record '%s' declaration has %d fields, "
                          "but binding has %d",
                          native->name,
                          field_count,
                          oak_dynarr_count(native->fields));
    return 0;
  }

  for (struct oak_list_entry_t* fpos = fields_wrap->children.next;
       fpos != &fields_wrap->children;
       fpos = fpos->next)
  {
    const struct oak_ast_node_t* fdecl =
        oak_container_of(fpos, struct oak_ast_node_t, link);
    if (fdecl->kind != OAK_NODE_RECORD_FIELD_DECL || !fdecl->lhs ||
        !fdecl->rhs)
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
    const int field_name_len = oak_token_size(fdecl->lhs->token);
    const struct oak_bind_field_t* native_field =
        native_record_field(native, field_name, field_name_len);
    if (!native_field)
    {
      oak_compiler_error_at(c,
                            fdecl->lhs->token,
                            "field '%s' is not bound on native record '%s'",
                            field_name,
                            native->name);
      return 0;
    }

    struct oak_type_t declared_type;
    oak_type_clear(&declared_type);
    oak_lower_type_node(c, fdecl->rhs, &declared_type);
    if (c->has_error)
      return 0;
    const struct oak_type_t bound_type = native_field_type(native_field);
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

void oak_register_program_records(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* raw_item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* item = oak_unwrap_decl(raw_item);

    const int is_empty = item && item->kind == OAK_NODE_RECORD_DECL_EMPTY;
    if (!item || (item->kind != OAK_NODE_RECORD_DECL && !is_empty))
      continue;

    /* RECORD_DECL_EMPTY: child = TYPE_NAME
     * RECORD_DECL:       lhs   = TYPE_NAME, rhs = RECORD_FIELDS */
    const struct oak_ast_node_t* type_name_node =
        is_empty ? item->child : item->lhs;
    if (!type_name_node || (!is_empty && !item->rhs))
    {
      oak_compiler_error_at(c, item->token, "malformed record declaration");
      return;
    }

    const struct oak_ast_node_t* name_ident =
        record_decl_name_ident(c, item, type_name_node);
    if (!name_ident || c->has_error)
      return;

    const char* name = oak_token_text(name_ident->token);
    const int name_len = oak_token_size(name_ident->token);

    const struct oak_registered_record_t* existing =
        oak_records_find(&c->records, name, name_len);
    if (existing)
    {
      const struct oak_bind_type_t* native =
          native_record_binding(c, name, name_len);
      if (native && existing->type_id == native->resolved_type_id)
      {
        if (!native_record_decl_matches(c, native, item, name_ident))
          return;

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

    struct oak_registered_record_t proto = { 0 };
    proto.name = name;
    proto.name_len = name_len;
    proto.type_id = oak_type_registry_intern(&c->types, name, name_len);
    oak_assert(oak_dynarr_init(c->allocator, &proto.fields, sizeof *proto.fields));
    oak_assert(oak_dynarr_init(c->allocator, &proto.methods, sizeof *proto.methods));
    proto.attrs = oak_extract_attrs(c->allocator, raw_item, &proto.attr_count);

    /* Pre-scan fields for attribute callbacks. */
    struct oak_attr_field_info_t* finfo = null;
    int finfo_count = 0;
    if (proto.attr_count > 0 && !is_empty && item->rhs &&
        item->rhs->kind == OAK_NODE_RECORD_FIELDS)
    {
      const struct oak_ast_node_t* fw = item->rhs;
      struct oak_list_entry_t* fp;
      oak_list_for_each(fp, &fw->children) { ++finfo_count; }
      if (finfo_count > 0)
      {
        finfo = OAK_ALLOC(c->allocator,
                          (usize)finfo_count * sizeof(struct oak_attr_field_info_t));
        int fi = 0;
        oak_list_for_each(fp, &fw->children)
        {
          const struct oak_ast_node_t* fd =
              oak_container_of(fp, struct oak_ast_node_t, link);
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
      OAK_FREE(c->allocator, finfo);

    if (proto.type_id < 0)
    {
      oak_compiler_error_at(
          c, name_ident->token, "type registry full while declaring record");
      return;
    }

    const u16 owner_module_id =
        c->current_module ? c->current_module->module_id : OAK_MODULE_ID_NONE;
    if (!oak_compiler_declare_symbol(
            c, name_ident->token, name, name_len, OAK_SYMBOL_RECORD,
            oak_dynarr_count(c->records.entries), owner_module_id, 0))
      return;
    struct oak_registered_record_t* slot =
        oak_record_registry_insert(&c->records, &proto);

    if (is_empty)
      continue; /* no fields to register */

    const struct oak_ast_node_t* fields_wrap = item->rhs;
    if (fields_wrap->kind != OAK_NODE_RECORD_FIELDS)
    {
      oak_compiler_error_at(c, item->token, "malformed record declaration");
      return;
    }

    /* Reject 'record Foo {}' — use 'record Foo;' for empty records. */
    if (fields_wrap->children.next == &fields_wrap->children)
    {
      oak_compiler_error_at(
          c, name_ident->token,
          "record '%s' has no fields; use 'record %s;' instead of '{}'",
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
static int register_record_field_decls(struct oak_compiler_t* c,
                                       struct oak_registered_record_t* slot,
                                       const struct oak_ast_node_t* fields_wrap,
                                       const char* record_name,
                                       const struct oak_token_t* err_ctx_token)
{
  for (struct oak_list_entry_t* fpos = fields_wrap->children.next;
       fpos != &fields_wrap->children;
       fpos = fpos->next)
  {
    const struct oak_ast_node_t* fdecl =
        oak_container_of(fpos, struct oak_ast_node_t, link);
    if (fdecl->kind != OAK_NODE_RECORD_FIELD_DECL || !fdecl->lhs || !fdecl->rhs)
    {
      oak_compiler_error_at(c, err_ctx_token, "malformed record field");
      return 0;
    }
    const struct oak_ast_node_t* fname = fdecl->lhs;
    const struct oak_ast_node_t* ftype = fdecl->rhs;
    if (fname->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, fdecl->lhs->token, "record field must be 'name : type'");
      return 0;
    }

    const char* fn_name = oak_token_text(fname->token);
    const int fn_len = oak_token_size(fname->token);
    for (int i = 0; i < oak_dynarr_count(slot->fields); ++i)
    {
      if (strcmp(slot->fields[i].name, fn_name) == 0)
      {
        oak_compiler_error_at(c,
                              fname->token,
                              "duplicate field '%s' in record '%s'",
                              fn_name,
                              record_name);
        return 0;
      }
    }

    struct oak_record_field_t f = {
      .name = fn_name,
      .name_len = fn_len,
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
    oak_assert(oak_dynarr_push(&slot->fields, &f));
  }
  return 1;
}
