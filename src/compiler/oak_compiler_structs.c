#include "oak_compiler_internal.h"

static int register_struct_field_decls(
    struct oak_compiler_t* c, struct oak_registered_struct_t* slot,
    const struct oak_ast_node_t* fields_wrap, const char* struct_name,
    usize struct_name_len, const struct oak_token_t* err_ctx_token);

const struct oak_registered_struct_t*
oak_compiler_find_struct_by_name(const struct oak_compiler_t* c,
                                 const char* name,
                                 const usize len)
{
  for (int i = 0; i < c->struct_count; ++i)
  {
    const struct oak_registered_struct_t* s = &c->structs[i];
    if (oak_name_eq(s->name, s->name_len, name, len))
      return s;
  }
  return null;
}

const struct oak_registered_struct_t*
oak_compiler_find_struct_by_type_id(const struct oak_compiler_t* c,
                                    const oak_type_id_t type_id)
{
  if (type_id == OAK_TYPE_UNKNOWN)
    return null;
  for (int i = 0; i < c->struct_count; ++i)
  {
    if (c->structs[i].type_id == type_id)
      return &c->structs[i];
  }
  return null;
}

int oak_compiler_find_struct_field(const struct oak_registered_struct_t* s,
                                   const char* name,
                                   const usize len)
{
  for (int i = 0; i < s->field_count; ++i)
  {
    const struct oak_struct_field_t* f = &s->fields[i];
    if (oak_name_eq(f->name, f->name_len, name, len))
      return i;
  }
  return -1;
}

int oak_compiler_struct_field_index(
    const struct oak_compiler_t* c,
    struct oak_type_t recv_ty,
    const char* field_name,
    const usize field_len,
    const struct oak_registered_struct_t** out_sd)
{
  if (!oak_type_is_known(&recv_ty))
    return -1;
  const struct oak_registered_struct_t* sd =
      oak_compiler_find_struct_by_type_id(c, recv_ty.id);
  if (!sd)
    return -1;
  if (out_sd)
    *out_sd = sd;
  return oak_compiler_find_struct_field(sd, field_name, field_len);
}

int oak_compiler_require_struct_field(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* recv,
    const struct oak_ast_node_t* fname,
    const int is_assignment,
    const struct oak_registered_struct_t** out_sd)
{
  struct oak_type_t recv_ty;
  oak_compiler_infer_expr_static_type(c, recv, &recv_ty);
  const char* ftext = oak_token_text(fname->token);
  const usize flen = oak_token_length(fname->token);
  const struct oak_registered_struct_t* sd = null;
  const int idx = oak_compiler_struct_field_index(c, recv_ty, ftext, flen, &sd);
  if (!oak_type_is_known(&recv_ty) || !sd)
  {
    oak_compiler_error_at(
        c,
        fname->token,
        is_assignment
            ?         "field assignment '.%s ='" " requires a struct receiver"
            : "field access '.%s' requires a struct receiver",
        ftext);
    return -1;
  }
  if (idx < 0)
  {
    oak_compiler_error_at(
        c,
        fname->token,
        "no such field '%s' on struct '%s'", ftext, sd->name);
    return -1;
  }
  if (out_sd)
    *out_sd = sd;
  return idx;
}

/* Walk all top-level struct declarations and register each in the compiler's
 * struct registry. The struct's type id is interned into the type registry so
 * later passes (function param types, struct literals) can resolve them. */
void oak_compiler_register_program_structs(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind != OAK_NODE_STRUCT_DECL)
      continue;

    if (!item->lhs || !item->rhs)
    {
      oak_compiler_error_at(c, item->token, "malformed struct declaration");
      return;
    }

    /* lhs = TYPE_NAME; for a plain user struct it nests an IDENT child. We only
     * support simple ident names for struct types. */
    const struct oak_ast_node_t* name_ident = item->lhs;
    if (name_ident->kind == OAK_NODE_TYPE_NAME)
    {
      const struct oak_list_entry_t* tn_first = name_ident->children.next;
      if (tn_first == &name_ident->children)
      {
        oak_compiler_error_at(
            c, item->token, "struct type name must be an identifier");
        return;
      }
      name_ident = oak_container_of(tn_first, struct oak_ast_node_t, link);
    }
    if (name_ident->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, item->token, "struct type name must be an identifier");
      return;
    }

    const char* name = oak_token_text(name_ident->token);
    const usize name_len = oak_token_length(name_ident->token);

    if (oak_compiler_find_struct_by_name(c, name, name_len))
    {
      oak_compiler_error_at(
          c, name_ident->token, "duplicate struct '%s'", name);
      return;
    }

    if (c->struct_count >= OAK_MAX_STRUCTS)
    {
      oak_compiler_error_at(
          c, item->token, "too many structs (max %d)", OAK_MAX_STRUCTS);
      return;
    }

    struct oak_registered_struct_t* slot = &c->structs[c->struct_count++];
    slot->name = name;
    slot->name_len = name_len;
    slot->type_id =
        oak_type_registry_intern(&c->type_registry, name, name_len);
    if (slot->type_id == OAK_TYPE_UNKNOWN)
    {
      oak_compiler_error_at(
          c, name_ident->token, "type registry full while declaring struct");
      return;
    }
    slot->field_count = 0;

    const struct oak_ast_node_t* fields_wrap = item->rhs;
    if (fields_wrap->kind != OAK_NODE_STRUCT_FIELDS)
    {
      oak_compiler_error_at(c, item->token, "malformed struct declaration");
      return;
    }
    if (!register_struct_field_decls(c, slot, fields_wrap, name, name_len,
                                    item->token) ||
        c->has_error)
      return;
  }
}

/* Collect field declarations in source order. Each entry is a binary node
 * STRUCT_FIELD_DECL(IDENT, IDENT) where lhs is the field name and rhs
 * names the field's type. */
static int register_struct_field_decls(
    struct oak_compiler_t* c, struct oak_registered_struct_t* slot,
    const struct oak_ast_node_t* fields_wrap, const char* struct_name,
    const usize struct_name_len, const struct oak_token_t* err_ctx_token)
{
  for (struct oak_list_entry_t* fpos = fields_wrap->children.next;
       fpos != &fields_wrap->children; fpos = fpos->next)
  {
    const struct oak_ast_node_t* fdecl =
        oak_container_of(fpos, struct oak_ast_node_t, link);
    if (fdecl->kind != OAK_NODE_STRUCT_FIELD_DECL || !fdecl->lhs
        || !fdecl->rhs)
    {
      oak_compiler_error_at(c, err_ctx_token, "malformed struct field");
      return 0;
    }
    if (slot->field_count >= OAK_MAX_STRUCT_FIELDS)
    {
        oak_compiler_error_at(
            c, fdecl->lhs->token, "too many fields in struct '%s' (max %d)",
            struct_name, OAK_MAX_STRUCT_FIELDS);
      return 0;
    }

    const struct oak_ast_node_t* fname = fdecl->lhs;
    const struct oak_ast_node_t* ftype = fdecl->rhs;
    if (fname->kind != OAK_NODE_IDENT || ftype->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, fdecl->lhs->token, "struct field must be 'name : type'");
      return 0;
    }

    const char* fn_name = oak_token_text(fname->token);
    const usize fn_len = oak_token_length(fname->token);
    for (int i = 0; i < slot->field_count; ++i)
    {
      if (oak_name_eq(slot->fields[i].name, slot->fields[i].name_len, fn_name,
                     fn_len))
      {
        oak_compiler_error_at(
            c, fname->token, "duplicate field '%s' in struct '%s'", fn_name,
            struct_name);
        return 0;
      }
    }

    struct oak_struct_field_t* f = &slot->fields[slot->field_count++];
    f->name = fn_name;
    f->name_len = fn_len;
    oak_type_clear(&f->type);
    f->type.id = oak_compiler_intern_type_token(c, ftype->token);
  }
  return 1;
}
