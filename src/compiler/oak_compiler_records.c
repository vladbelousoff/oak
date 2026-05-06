#include "oak_compiler_internal.h"

/* ---------- Native type registration ---------- */

void oak_compiler_register_native_types(
    struct oak_compiler_t* c, const struct oak_compile_options_t* opts)
{
  if (!opts || opts->native_types.count == 0)
    return;

  for (int i = 0; i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* nt = opts->native_types.items[i];
    if (!nt)
      continue;

    if (oak_record_registry_find_by_name(&c->records, nt->name, nt->name_len))
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

    struct oak_registered_record_t proto = { 0 };
    proto.name = nt->name;
    proto.name_len = nt->name_len;
    proto.type_id = tid;
    proto.field_count = nt->field_count;

    if (nt->field_count > OAK_MAX_RECORD_FIELDS)
    {
      oak_compiler_error_at(c,
                            null,
                            "native type '%s' has too many fields (max %d)",
                            nt->name,
                            OAK_MAX_RECORD_FIELDS);
      return;
    }

    for (int fi = 0; fi < nt->field_count; ++fi)
    {
      const struct oak_bind_field_t* nf = &nt->fields[fi];
      struct oak_record_field_t* sf = &proto.fields[fi];
      sf->name = nf->name;
      sf->name_len = nf->name_len;
      oak_type_clear(&sf->type);
      if (nf->shape == OAK_BIND_SHAPE_ARRAY)
      {
        sf->type.kind = OAK_TYPE_KIND_ARRAY;
        sf->type.id = nf->field_type_id;
      }
      else
        sf->type.id = nf->field_type_id;
    }

    oak_record_registry_insert(&c->records, &proto);
    if (c->has_error)
      return;
  }
}

static int register_record_field_decls(struct oak_compiler_t* c,
                                       struct oak_registered_record_t* slot,
                                       const struct oak_ast_node_t* fields_wrap,
                                       const char* record_name,
                                       const struct oak_token_t* err_ctx_token);

/* ---------- oak_record_registry_t lifecycle ---------- */

void oak_record_registry_init(struct oak_record_registry_t* r)
{
  oak_hash_table_init(&r->by_name);
  oak_dynarr_init(&r->entries.items, &r->entries.count, &r->entries.capacity);
}

void oak_record_registry_free(struct oak_record_registry_t* r)
{
  oak_hash_table_free(&r->by_name);
  for (int i = 0; i < r->entries.count; ++i)
  {
    oak_dynarr_free(&r->entries.items[i].methods.items,
                    &r->entries.items[i].methods.count,
                    &r->entries.items[i].methods.capacity);
  }
  oak_dynarr_free(&r->entries.items, &r->entries.count, &r->entries.capacity);
}

struct oak_registered_record_t*
oak_record_registry_insert(struct oak_record_registry_t* r,
                           const struct oak_registered_record_t* s)
{
  oak_dynarr_push(&r->entries.items, &r->entries.count, &r->entries.capacity, s, sizeof(*s));
  const int idx = r->entries.count - 1;
  oak_hash_table_insert(
      &r->by_name, r->entries.items[idx].name, r->entries.items[idx].name_len, idx);
  return &r->entries.items[idx];
}

const struct oak_registered_record_t* oak_record_registry_find_by_name(
    const struct oak_record_registry_t* r, const char* name, usize len)
{
  const int idx = oak_hash_table_get(&r->by_name, name, len);
  if (idx < 0)
    return null;
  return &r->entries.items[idx];
}

const struct oak_registered_record_t*
oak_record_registry_find_by_type_id(const struct oak_record_registry_t* r,
                                    oak_type_id_t type_id)
{
  if (type_id == OAK_TYPE_VOID)
    return null;
  for (int i = 0; i < r->entries.count; ++i)
  {
    if (r->entries.items[i].type_id == type_id)
      return &r->entries.items[i];
  }
  return null;
}



int oak_compiler_find_record_field(const struct oak_registered_record_t* s,
                                   const char* name,
                                   const usize len)
{
  for (int i = 0; i < s->field_count; ++i)
  {
    const struct oak_record_field_t* f = &s->fields[i];
    if (oak_name_eq(f->name, f->name_len, name, len))
      return i;
  }
  return -1;
}

const struct oak_registered_fn_t* oak_compiler_find_record_method(
    const struct oak_registered_record_t* sd,
    const char* name,
    const usize len,
    const int static_only)
{
  if (!sd)
    return null;
  for (int i = 0; i < sd->methods.count; ++i)
  {
    const struct oak_registered_fn_t* m = &sd->methods.items[i];
    if ((!!m->is_static) != (!!static_only))
      continue;
    if (oak_name_eq(m->name, m->name_len, name, len))
      return m;
  }
  return null;
}

int oak_compiler_record_field_index(
    const struct oak_compiler_t* c,
    struct oak_type_t recv_ty,
    const char* field_name,
    const usize field_len,
    const struct oak_registered_record_t** out_sd)
{
  if (!oak_type_is_known(&recv_ty))
    return -1;
  const struct oak_registered_record_t* sd =
      oak_record_registry_find_by_type_id(&c->records, recv_ty.id);
  if (!sd)
    return -1;
  if (out_sd)
    *out_sd = sd;
  return oak_compiler_find_record_field(sd, field_name, field_len);
}

int oak_compiler_require_record_field(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* recv,
    const struct oak_ast_node_t* fname,
    const int is_assignment,
    const struct oak_registered_record_t** out_sd)
{
  struct oak_type_t recv_ty;
  oak_compiler_infer_expr_static_type(c, recv, &recv_ty);
  const char* ftext = oak_token_text(fname->token);
  const usize flen = oak_token_length(fname->token);
  const struct oak_registered_record_t* sd = null;
  const int idx = oak_compiler_record_field_index(c, recv_ty, ftext, flen, &sd);
  if (!oak_type_is_known(&recv_ty) || !sd)
  {
    oak_compiler_error_at(c,
                          fname->token,
                          is_assignment
                              ? "field assignment '.%s ='"
                                " requires a record receiver"
                              : "field access '.%s' requires a record receiver",
                          ftext);
    return -1;
  }
  if (idx < 0)
  {
    oak_compiler_error_at(
        c, fname->token, "no such field '%s' on record '%s'", ftext, sd->name);
    return -1;
  }
  if (out_sd)
    *out_sd = sd;
  return idx;
}

/* Walk all top-level record declarations and register each in the compiler's
 * record registry. The record's type id is interned into the type registry so
 * later passes (function param types, record literals) can resolve them. */
void oak_compiler_register_program_records(struct oak_compiler_t* c,
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

    if (oak_record_registry_find_by_name(&c->records, name, name_len))
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
      if (oak_name_eq(
              slot->fields[i].name, slot->fields[i].name_len, fn_name, fn_len))
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
    f->type.id = oak_compiler_intern_type_token(c, ftype->token);
  }
  return 1;
}

/* Append a method entry (instance or static) to a record's methods array. */
static void record_append_method(struct oak_registered_record_t* sd,
                                 const struct oak_registered_fn_t* m)
{
  oak_dynarr_push(&sd->methods.items, &sd->methods.count, &sd->methods.capacity, m, sizeof(*m));
}

/* ---------- Native function registration ---------- */

void oak_compiler_register_native_fns(
    struct oak_compiler_t* c, const struct oak_compile_options_t* opts)
{
  if (!opts || opts->native_fns.count == 0)
    return;

  for (int i = 0; i < opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* b = &opts->native_fns.items[i];
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
    entry.return_kind = (b->return_shape == OAK_BIND_SHAPE_ARRAY)
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
      struct oak_registered_record_t* sd =
          (struct oak_registered_record_t*)oak_record_registry_find_by_type_id(
              &c->records, b->receiver_type_id);
      if (!sd)
      {
        oak_compiler_error_at(c,
                              null,
                              "native method '%s': no record registered for "
                              "receiver type id %d",
                              b->name,
                              b->receiver_type_id);
        return;
      }
      const int is_static = (b->kind == OAK_BIND_FN_STATIC_METHOD);
      entry.arity = vm_arity;
      entry.is_static = is_static;
      for (int j = 0; j < sd->methods.count; ++j)
      {
        if (oak_name_eq(sd->methods.items[j].name,
                        sd->methods.items[j].name_len,
                        b->name,
                        name_len))
        {
          oak_compiler_error_at(c,
                                null,
                                is_static
                                    ? "duplicate native static method '%s' on "
                                      "record '%s'"
                                    : "duplicate native method '%s' on record "
                                      "'%s'",
                                b->name,
                                sd->name);
          return;
        }
      }
      record_append_method(sd, &entry);
    }
    if (c->has_error)
      return;
  }
}

