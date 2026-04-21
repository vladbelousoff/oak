#include "oak_compiler_internal.h"

const struct oak_registered_struct_t*
oak_compiler_find_struct_by_name(const struct oak_compiler_t* c,
                                 const char* name,
                                 const usize len)
{
  for (int i = 0; i < c->struct_count; ++i)
  {
    const struct oak_registered_struct_t* s = &c->structs[i];
    if (s->name_len == len && memcmp(s->name, name, len) == 0)
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
    if (f->name_len == len && memcmp(f->name, name, len) == 0)
      return i;
  }
  return -1;
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
    const int name_len = oak_token_length(name_ident->token);

    if (oak_compiler_find_struct_by_name(c, name, (usize)name_len))
    {
      oak_compiler_error_at(
          c, name_ident->token, "duplicate struct '%.*s'", name_len, name);
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
    slot->name_len = (usize)name_len;
    slot->type_id =
        oak_type_registry_intern(&c->type_registry, name, (usize)name_len);
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

    /* Collect field declarations in source order. Each entry is a binary node
     * STRUCT_FIELD_DECL(IDENT, IDENT) where lhs is the field name and rhs
     * names the field's type. */
    for (struct oak_list_entry_t* fpos = fields_wrap->children.next;
         fpos != &fields_wrap->children;
         fpos = fpos->next)
    {
      const struct oak_ast_node_t* fdecl =
          oak_container_of(fpos, struct oak_ast_node_t, link);
      if (fdecl->kind != OAK_NODE_STRUCT_FIELD_DECL || !fdecl->lhs
          || !fdecl->rhs)
      {
        oak_compiler_error_at(c, item->token, "malformed struct field");
        return;
      }
      if (slot->field_count >= OAK_MAX_STRUCT_FIELDS)
      {
        oak_compiler_error_at(c,
                              fdecl->lhs->token,
                              "too many fields in struct '%.*s' (max %d)",
                              name_len,
                              name,
                              OAK_MAX_STRUCT_FIELDS);
        return;
      }

      const struct oak_ast_node_t* fname = fdecl->lhs;
      const struct oak_ast_node_t* ftype = fdecl->rhs;
      if (fname->kind != OAK_NODE_IDENT || ftype->kind != OAK_NODE_IDENT)
      {
        oak_compiler_error_at(
            c, fdecl->lhs->token, "struct field must be 'name : type'");
        return;
      }

      const char* fn_name = oak_token_text(fname->token);
      const usize fn_len = (usize)oak_token_length(fname->token);
      for (int i = 0; i < slot->field_count; ++i)
      {
        if (slot->fields[i].name_len == fn_len
            && memcmp(slot->fields[i].name, fn_name, fn_len) == 0)
        {
          oak_compiler_error_at(c,
                                fname->token,
                                "duplicate field '%.*s' in struct '%.*s'",
                                (int)fn_len,
                                fn_name,
                                name_len,
                                name);
          return;
        }
      }

      struct oak_struct_field_t* f = &slot->fields[slot->field_count++];
      f->name = fn_name;
      f->name_len = fn_len;
      oak_type_clear(&f->type);
      f->type.id = oak_compiler_intern_type_token(c, ftype->token);
    }
  }
}
