#include "internal/oak_compiler.h"

static int register_record_field_decls(struct oak_compiler_t* c,
                                       struct oak_registered_record_t* slot,
                                       const struct oak_ast_node_t* fields_wrap,
                                       const char* record_name,
                                       const struct oak_token_t* err_ctx_token);

/* Walk all top-level record declarations and register each in the compiler's
 * record registry. The record's type id is interned into the type registry so
 * later passes (function param types, record literals) can resolve them. */
void oc_register_program_records(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind != OAK_NODE_RECORD_DECL)
      continue;

    if (!item->lhs || !item->rhs)
    {
      oak_compiler_error_at(c, item->token, "malformed record declaration");
      return;
    }

    /* lhs = TYPE_NAME; for a plain user record it nests an IDENT child. */
    const struct oak_ast_node_t* name_ident = item->lhs;
    if (name_ident->kind == OAK_NODE_TYPE_NAME)
    {
      const struct oak_list_entry_t* tn_first = name_ident->children.next;
      if (tn_first == &name_ident->children)
      {
        oak_compiler_error_at(
            c, item->token, "record type name must be an identifier");
        return;
      }
      name_ident = oak_container_of(tn_first, struct oak_ast_node_t, link);
    }
    if (name_ident->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, item->token, "record type name must be an identifier");
      return;
    }

    const char* name = oak_token_text(name_ident->token);
    const usize name_len = oak_token_length(name_ident->token);

    if (oc_records_find(&c->records, name, name_len))
    {
      oak_compiler_error_at(
          c, name_ident->token, "duplicate record '%s'", name);
      return;
    }

    struct oak_registered_record_t proto = { 0 };
    proto.name = name;
    proto.name_len = name_len;
    proto.type_id = oak_type_registry_intern(&c->types, name, name_len);
    proto.field_count = 0;

    if (proto.type_id < 0)
    {
      oak_compiler_error_at(
          c, name_ident->token, "type registry full while declaring record");
      return;
    }

    /* Insert into registry first so the pointer is stable. */
    struct oak_registered_record_t* slot =
        oak_record_registry_insert(&c->records, &proto);

    const struct oak_ast_node_t* fields_wrap = item->rhs;
    if (fields_wrap->kind != OAK_NODE_RECORD_FIELDS)
    {
      oak_compiler_error_at(c, item->token, "malformed record declaration");
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
    if (fdecl->kind == OAK_NODE_FN_DECL)
      continue;
    if (fdecl->kind != OAK_NODE_RECORD_FIELD_DECL || !fdecl->lhs || !fdecl->rhs)
    {
      oak_compiler_error_at(c, err_ctx_token, "malformed record field");
      return 0;
    }
    if (slot->field_count >= OAK_MAX_RECORD_FIELDS)
    {
      oak_compiler_error_at(c,
                            fdecl->lhs->token,
                            "too many fields in record '%s' (max %d)",
                            record_name,
                            OAK_MAX_RECORD_FIELDS);
      return 0;
    }

    const struct oak_ast_node_t* fname = fdecl->lhs;
    const struct oak_ast_node_t* ftype = fdecl->rhs;
    if (fname->kind != OAK_NODE_IDENT || ftype->kind != OAK_NODE_IDENT)
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

    struct oak_record_field_t* f = &slot->fields[slot->field_count++];
    f->name = fn_name;
    f->name_len = fn_len;
    oak_type_clear(&f->type);
    f->type.id = oc_intern_type_tok(c, ftype->token);
  }
  return 1;
}
