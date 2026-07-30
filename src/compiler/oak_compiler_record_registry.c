#include "internal/oak_compiler.h"


void oak_record_registry_init(struct oak_record_registry_t* r,
                              struct oak_allocator_t* allocator)
{
  r->allocator = allocator;
  oak_htable_init(&r->by_name, allocator);
  oak_assert(oak_dynarr_init(r->allocator, &r->entries, sizeof *r->entries));
}

void oak_record_registry_free(struct oak_record_registry_t* r)
{
  oak_htable_free(&r->by_name);
  for (int i = 0; i < oak_dynarr_count(r->entries); ++i)
  {
    struct oak_registered_record_t* e = &r->entries[i];
    if (e->attrs)
      OAK_FREE(r->allocator, e->attrs);
    for (int j = 0; j < oak_dynarr_count(e->methods); ++j)
    {
      if (e->methods[j].attrs)
        OAK_FREE(r->allocator, e->methods[j].attrs);
      if (e->methods[j].param_types)
        OAK_FREE(r->allocator, e->methods[j].param_types);
      if (e->methods[j].param_mut_flags)
        OAK_FREE(r->allocator, e->methods[j].param_mut_flags);
    }
    oak_dynarr_free(&e->fields);
    oak_dynarr_free(&e->methods);
  }
  oak_dynarr_free(&r->entries);
}

struct oak_registered_record_t*
oak_record_registry_insert(struct oak_record_registry_t* r,
                           const struct oak_registered_record_t* s)
{
  oak_assert(oak_dynarr_push(&r->entries, s));
  const int idx = oak_dynarr_count(r->entries) - 1;
  oak_htable_insert(&r->by_name,
                    r->entries[idx].name,
                    strlen(r->entries[idx].name),
                    idx);
  return &r->entries[idx];
}

const struct oak_registered_record_t* oak_records_find(
    const struct oak_record_registry_t* r, const char* name)
{
  const int idx = oak_htable_get(&r->by_name, name, strlen(name));
  if (idx < 0)
    return null;
  return &r->entries[idx];
}

const struct oak_registered_record_t*
oak_records_find_by_id(const struct oak_record_registry_t* r,
                                    oak_type_id_t type_id)
{
  if (type_id == OAK_TYPE_VOID)
    return null;
  for (int i = 0; i < oak_dynarr_count(r->entries); ++i)
  {
    if (r->entries[i].type_id == type_id)
      return &r->entries[i];
  }
  return null;
}


int oak_record_field(const struct oak_registered_record_t* s,
                                   const char* name)
{
  for (int i = 0; i < oak_dynarr_count(s->fields); ++i)
  {
    const struct oak_record_field_t* f = &s->fields[i];
    if (strcmp(f->name, name) == 0)
      return i;
  }
  return -1;
}

const struct oak_registered_fn_t*
oak_find_record_method(const struct oak_registered_record_t* sd,
                                const char* name,
                                const int static_only)
{
  if (!sd)
    return null;
  for (int i = 0; i < oak_dynarr_count(sd->methods); ++i)
  {
    const struct oak_registered_fn_t* m = &sd->methods[i];
    if ((!!m->is_static) != (!!static_only))
      continue;
    if (strcmp(m->name, name) == 0)
      return m;
  }
  return null;
}

int oak_record_field_index(
    const struct oak_compiler_t* c,
    struct oak_type_t recv_ty,
    const char* field_name,
    const struct oak_registered_record_t** out_sd)
{
  if (!oak_type_is_known(&recv_ty))
    return -1;
  const struct oak_registered_record_t* sd =
      oak_records_find_by_id(&c->records, recv_ty.id);
  if (!sd)
    return -1;
  if (out_sd)
    *out_sd = sd;
  return oak_record_field(sd, field_name);
}

int oak_require_record_field(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* recv,
    const struct oak_ast_node_t* fname,
    const int is_assignment,
    const struct oak_registered_record_t** out_sd)
{
  struct oak_type_t recv_ty;
  oak_infer_type(c, recv, &recv_ty);
  const char* ftext = oak_token_text(fname->token);
  const struct oak_registered_record_t* sd = null;
  const int idx = oak_record_field_index(c, recv_ty, ftext, &sd);
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
