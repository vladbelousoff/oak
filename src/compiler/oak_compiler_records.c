#include "internal/oak_compiler.h"
#include "oak_memory.h"

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

void oakc_register_program_records(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* raw_item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* item = oakc_unwrap_decl(raw_item);

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
    const usize name_len = oak_token_length(name_ident->token);

    if (oakc_records_find(&c->records, name, name_len))
    {
      oak_compiler_error_at(
          c, name_ident->token, "duplicate record '%s'", name);
      return;
    }

    struct oak_registered_record_t proto = { 0 };
    proto.name = name;
    proto.name_len = name_len;
    proto.type_id = oak_type_registry_intern(&c->types, name, name_len);
    proto.fields = null;
    proto.field_count = 0;
    proto.field_capacity = 0;
    proto.attrs = oakc_extract_attrs(c->allocator, raw_item, &proto.attr_count);
    oakc_dispatch_compile_attr_cbs(
        c, proto.attrs, proto.attr_count, name, OAK_ATTR_TARGET_RECORD);

    if (proto.type_id < 0)
    {
      oak_compiler_error_at(
          c, name_ident->token, "type registry full while declaring record");
      return;
    }

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

    if (!register_record_field_decls(c, slot, fields_wrap, name, item->token) ||
        c->has_error)
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
    const usize fn_len = oak_token_length(fname->token);
    for (int i = 0; i < slot->field_count; ++i)
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
    oakc_lower_type_node(c, ftype, &f.type);
    if (c->has_error)
      return 0;
    if (!oak_type_is_known(&f.type))
    {
      oak_compiler_error_at(
          c, ftype->token ? ftype->token : fname->token,
          "record field must be 'name : type'");
      return 0;
    }
    oak_dynarr_push(c->allocator, &slot->fields,
                    &slot->field_count,
                    &slot->field_capacity,
                    &f,
                    sizeof(f));
  }
  return 1;
}
