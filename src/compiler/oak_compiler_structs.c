#include "oak_compiler_internal.h"

/* ---------- Native type registration ---------- */

void oak_compiler_register_native_types(
    struct oak_compiler_t* c, const struct oak_compile_options_t* opts)
{
  if (!opts || opts->native_type_count == 0)
    return;

  for (int i = 0; i < opts->native_type_count; ++i)
  {
    const struct oak_native_type_t* nt = opts->native_types[i];
    if (!nt)
      continue;

    if (oak_compiler_find_struct_by_name(c, nt->name, nt->name_len))
    {
      oak_compiler_error_at(
          c,
          null,
          "native type '%s' conflicts with an already-registered type",
          nt->name);
      return;
    }

    /* Register the pre-assigned stable id into the compiler's type registry.
     * This ensures that references to this name in Oak source resolve to the
     * same id that the embedding code holds in nt->type_id. */
    const oak_type_id_t tid = oak_type_registry_intern_with_id(
        &c->types, nt->name, nt->name_len, nt->type_id);
    if (tid < 0)
    {
      oak_compiler_error_at(c,
                            null,
                            "failed to register native type '%s' (type "
                            "registry full or id conflict)",
                            nt->name);
      return;
    }

    struct oak_registered_struct_t proto = { 0 };
    proto.name = nt->name;
    proto.name_len = nt->name_len;
    proto.type_id = tid;
    proto.field_count = nt->field_count;
    proto.method_count = 0;
    proto.method_capacity = 0;
    proto.methods = null;

    if (nt->field_count > OAK_MAX_STRUCT_FIELDS)
    {
      oak_compiler_error_at(c,
                            null,
                            "native type '%s' has too many fields (max %d)",
                            nt->name,
                            OAK_MAX_STRUCT_FIELDS);
      return;
    }

    for (int fi = 0; fi < nt->field_count; ++fi)
    {
      const struct oak_native_field_t* nf = &nt->fields[fi];
      struct oak_struct_field_t* sf = &proto.fields[fi];
      sf->name = nf->name;
      sf->name_len = nf->name_len;
      oak_type_clear(&sf->type);
      sf->type.id = nf->field_type_id;
    }

    oak_struct_registry_insert(&c->structs, &proto);
    if (c->has_error)
      return;
  }
}

static int register_struct_field_decls(struct oak_compiler_t* c,
                                       struct oak_registered_struct_t* slot,
                                       const struct oak_ast_node_t* fields_wrap,
                                       const char* struct_name,
                                       const struct oak_token_t* err_ctx_token);

/* ---------- oak_struct_registry_t lifecycle ---------- */

void oak_struct_registry_init(struct oak_struct_registry_t* r)
{
  oak_hash_table_init(&r->by_name);
  r->entries = null;
  r->count = 0;
  r->capacity = 0;
}

void oak_struct_registry_free(struct oak_struct_registry_t* r)
{
  oak_hash_table_free(&r->by_name);
  if (r->entries)
  {
    for (int i = 0; i < r->count; ++i)
    {
      if (r->entries[i].methods)
        oak_free(r->entries[i].methods, OAK_SRC_LOC);
      if (r->entries[i].static_methods)
        oak_free(r->entries[i].static_methods, OAK_SRC_LOC);
    }
    oak_free(r->entries, OAK_SRC_LOC);
  }
  r->entries = null;
  r->count = 0;
  r->capacity = 0;
}

struct oak_registered_struct_t*
oak_struct_registry_insert(struct oak_struct_registry_t* r,
                           const struct oak_registered_struct_t* s)
{
  if (r->count >= r->capacity)
  {
    const int new_cap = r->capacity < 8 ? 8 : r->capacity * 2;
    r->entries = oak_realloc(
        r->entries, (usize)new_cap * sizeof *r->entries, OAK_SRC_LOC);
    r->capacity = new_cap;
  }
  const int idx = r->count;
  r->entries[idx] = *s;
  r->count++;
  oak_hash_table_insert(
      &r->by_name, r->entries[idx].name, r->entries[idx].name_len, idx);
  return &r->entries[idx];
}

const struct oak_registered_struct_t* oak_struct_registry_find_by_name(
    const struct oak_struct_registry_t* r, const char* name, usize len)
{
  const int idx = oak_hash_table_get(&r->by_name, name, len);
  if (idx < 0)
    return null;
  return &r->entries[idx];
}

const struct oak_registered_struct_t*
oak_struct_registry_find_by_type_id(const struct oak_struct_registry_t* r,
                                    oak_type_id_t type_id)
{
  if (type_id == OAK_TYPE_VOID)
    return null;
  for (int i = 0; i < r->count; ++i)
  {
    if (r->entries[i].type_id == type_id)
      return &r->entries[i];
  }
  return null;
}



/* ---------- Compiler-level lookup wrappers ---------- */

const struct oak_registered_struct_t* oak_compiler_find_struct_by_name(
    const struct oak_compiler_t* c, const char* name, const usize len)
{
  return oak_struct_registry_find_by_name(&c->structs, name, len);
}

const struct oak_registered_struct_t*
oak_compiler_find_struct_by_type_id(const struct oak_compiler_t* c,
                                    const oak_type_id_t type_id)
{
  return oak_struct_registry_find_by_type_id(&c->structs, type_id);
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

const struct oak_registered_fn_t* oak_compiler_find_struct_static_method(
    const struct oak_registered_struct_t* sd,
    const char* name,
    const usize len)
{
  if (!sd || !sd->static_methods)
    return null;
  for (int i = 0; i < sd->static_method_count; ++i)
  {
    const struct oak_registered_fn_t* m = &sd->static_methods[i];
    if (oak_name_eq(m->name, m->name_len, name, len))
      return m;
  }
  return null;
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
    oak_compiler_error_at(c,
                          fname->token,
                          is_assignment
                              ? "field assignment '.%s ='"
                                " requires a struct receiver"
                              : "field access '.%s' requires a struct receiver",
                          ftext);
    return -1;
  }
  if (idx < 0)
  {
    oak_compiler_error_at(
        c, fname->token, "no such field '%s' on struct '%s'", ftext, sd->name);
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

    /* lhs = TYPE_NAME; for a plain user struct it nests an IDENT child. */
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

    struct oak_registered_struct_t proto = { 0 };
    proto.name = name;
    proto.name_len = name_len;
    proto.type_id = oak_type_registry_intern(&c->types, name, name_len);
    proto.field_count = 0;
    proto.method_count = 0;

    if (proto.type_id < 0)
    {
      oak_compiler_error_at(
          c, name_ident->token, "type registry full while declaring struct");
      return;
    }

    /* Insert into registry first so the pointer is stable. */
    struct oak_registered_struct_t* slot =
        oak_struct_registry_insert(&c->structs, &proto);

    const struct oak_ast_node_t* fields_wrap = item->rhs;
    if (fields_wrap->kind != OAK_NODE_STRUCT_FIELDS)
    {
      oak_compiler_error_at(c, item->token, "malformed struct declaration");
      return;
    }
    if (!register_struct_field_decls(c, slot, fields_wrap, name, item->token) ||
        c->has_error)
      return;
  }
}

/* Collect field declarations in source order. */
static int register_struct_field_decls(struct oak_compiler_t* c,
                                       struct oak_registered_struct_t* slot,
                                       const struct oak_ast_node_t* fields_wrap,
                                       const char* struct_name,
                                       const struct oak_token_t* err_ctx_token)
{
  for (struct oak_list_entry_t* fpos = fields_wrap->children.next;
       fpos != &fields_wrap->children;
       fpos = fpos->next)
  {
    const struct oak_ast_node_t* fdecl =
        oak_container_of(fpos, struct oak_ast_node_t, link);
    if (fdecl->kind != OAK_NODE_STRUCT_FIELD_DECL || !fdecl->lhs || !fdecl->rhs)
    {
      oak_compiler_error_at(c, err_ctx_token, "malformed struct field");
      return 0;
    }
    if (slot->field_count >= OAK_MAX_STRUCT_FIELDS)
    {
      oak_compiler_error_at(c,
                            fdecl->lhs->token,
                            "too many fields in struct '%s' (max %d)",
                            struct_name,
                            OAK_MAX_STRUCT_FIELDS);
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
      if (oak_name_eq(
              slot->fields[i].name, slot->fields[i].name_len, fn_name, fn_len))
      {
        oak_compiler_error_at(c,
                              fname->token,
                              "duplicate field '%s' in struct '%s'",
                              fn_name,
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

/* Append a method entry to a struct's growable methods array. */
static void struct_append_method(struct oak_registered_struct_t* sd,
                                 const struct oak_registered_fn_t* m)
{
  if (sd->method_count >= sd->method_capacity)
  {
    const int new_cap = sd->method_capacity < 4 ? 4 : sd->method_capacity * 2;
    sd->methods = oak_realloc(
        sd->methods,
        (usize)new_cap * sizeof(struct oak_registered_fn_t),
        OAK_SRC_LOC);
    sd->method_capacity = new_cap;
  }
  sd->methods[sd->method_count++] = *m;
}

/* Append a static method entry to a struct's growable static_methods array. */
static void struct_append_static_method(struct oak_registered_struct_t* sd,
                                       const struct oak_registered_fn_t* m)
{
  if (sd->static_method_count >= sd->static_method_capacity)
  {
    const int new_cap =
        sd->static_method_capacity < 4 ? 4 : sd->static_method_capacity * 2;
    sd->static_methods = oak_realloc(
        sd->static_methods,
        (usize)new_cap * sizeof(struct oak_registered_fn_t),
        OAK_SRC_LOC);
    sd->static_method_capacity = new_cap;
  }
  sd->static_methods[sd->static_method_count++] = *m;
}

/* ---------- Native function registration ---------- */

void oak_compiler_register_native_fns(
    struct oak_compiler_t* c, const struct oak_compile_options_t* opts)
{
  if (!opts || opts->native_fn_count == 0)
    return;

  for (int i = 0; i < opts->native_fn_count; ++i)
  {
    const struct oak_native_fn_binding_t* b = &opts->native_fns[i];
    if (!b->name || !b->impl)
      continue;

    const usize name_len = strlen(b->name);

    int vm_arity;
    switch (b->kind)
    {
    case OAK_BIND_FN_GLOBAL:
      vm_arity = b->arity;
      break;
    case OAK_BIND_FN_INSTANCE_METHOD:
      vm_arity = b->arity + 1;
      break;
    case OAK_BIND_FN_STATIC_METHOD:
      vm_arity = b->arity;
      break;
    default:
      continue;
    }

    const u16 idx =
        oak_compiler_intern_native_constant(c, b->impl, vm_arity, b->name);

    struct oak_registered_fn_t entry = { 0 };
    entry.name = b->name;
    entry.name_len = name_len;
    entry.const_idx = idx;
    entry.receiver_type_id = b->receiver_type_id;
    entry.return_type_id = b->return_type_id;
    entry.return_kind = (b->return_shape == OAK_BIND_RETURN_ARRAY)
                            ? OAK_TYPE_KIND_ARRAY
                            : OAK_TYPE_KIND_SCALAR;
    entry.decl = null;

    if (b->kind == OAK_BIND_FN_GLOBAL)
    {
      entry.arity = b->arity;
      if (oak_fn_registry_find(&c->fns, b->name, name_len))
      {
        oak_compiler_error_at(
            c, null, "duplicate native function '%s'", b->name);
        return;
      }
      oak_fn_registry_insert(&c->fns, &entry);
    }
    else
    {
      struct oak_registered_struct_t* sd =
          (struct oak_registered_struct_t*)oak_struct_registry_find_by_type_id(
              &c->structs, b->receiver_type_id);
      if (!sd)
      {
        oak_compiler_error_at(c,
                              null,
                              "native method '%s': no struct registered for "
                              "receiver type id %d",
                              b->name,
                              b->receiver_type_id);
        return;
      }
      if (b->kind == OAK_BIND_FN_STATIC_METHOD)
      {
        entry.arity = vm_arity;
        for (int j = 0; j < sd->static_method_count; ++j)
        {
          if (oak_name_eq(sd->static_methods[j].name,
                          sd->static_methods[j].name_len,
                          b->name,
                          name_len))
          {
            oak_compiler_error_at(
                c,
                null,
                "duplicate native static method '%s' on struct '%s'",
                b->name,
                sd->name);
            return;
          }
        }
        struct_append_static_method(sd, &entry);
      }
      else
      {
        /* OAK_BIND_FN_INSTANCE_METHOD — arity includes implicit self */
        entry.arity = vm_arity;
        for (int j = 0; j < sd->method_count; ++j)
        {
          if (oak_name_eq(
                  sd->methods[j].name, sd->methods[j].name_len, b->name, name_len))
          {
            oak_compiler_error_at(c,
                                  null,
                                  "duplicate native method '%s' on struct '%s'",
                                  b->name,
                                  sd->name);
            return;
          }
        }
        struct_append_method(sd, &entry);
      }
    }
    if (c->has_error)
      return;
  }
}

