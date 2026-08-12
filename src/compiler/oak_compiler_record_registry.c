#include "internal/oak_compiler.h"


void oak_record_registry_init(oak_record_registry_t* r,
                              oak_allocator_t* allocator)
{
  r->allocator = allocator;
  r->by_name = oak_hash_map_new(allocator, sizeof(usize));
  r->entries =
      oak_vector_new(allocator, sizeof(oak_registered_record_t));
  oak_assert(r->by_name && r->entries);
}

void oak_record_registry_free(oak_record_registry_t* r)
{
  oak_destroy(r->by_name);
  oak_registered_record_t* entries =
      OAK_DATA(oak_registered_record_t, r->entries);
  for (usize i = 0; i < oak_size(r->entries); ++i)
  {
    oak_registered_record_t* e = &entries[i];
    if (e->attrs)
      OAK_FREE(r->allocator, e->attrs);
    oak_registered_fn_t* methods =
        OAK_DATA(oak_registered_fn_t, e->methods);
    for (usize j = 0; j < oak_size(e->methods); ++j)
    {
      if (methods[j].attrs)
        OAK_FREE(r->allocator, methods[j].attrs);
      if (methods[j].param_types)
        OAK_FREE(r->allocator, methods[j].param_types);
      if (methods[j].param_mut_flags)
        OAK_FREE(r->allocator, methods[j].param_mut_flags);
    }
    oak_destroy(e->fields);
    oak_destroy(e->methods);
  }
  oak_destroy(r->entries);
}

oak_registered_record_t*
oak_record_registry_insert(oak_record_registry_t* r,
                           const oak_registered_record_t* s)
{
  oak_assert(oak_push_back(r->entries, s));
  const usize idx = oak_size(r->entries) - 1;
  oak_registered_record_t* entry = oak_get(r->entries, idx);
  oak_assert(oak_put_str(r->by_name, entry->name, &idx));
  return entry;
}

const oak_registered_record_t* oak_records_find(
    const oak_record_registry_t* r, const char* name)
{
  const usize* idx = oak_cfind_str(r->by_name, name);
  return idx ? oak_cget(r->entries, *idx) : null;
}

const oak_registered_record_t*
oak_records_find_by_id(const oak_record_registry_t* r,
                                    oak_type_id_t type_id)
{
  if (type_id == OAK_TYPE_VOID)
    return null;
  const oak_registered_record_t* entries =
      OAK_CDATA(oak_registered_record_t, r->entries);
  for (usize i = 0; i < oak_size(r->entries); ++i)
  {
    if (entries[i].type_id == type_id)
      return &entries[i];
  }
  return null;
}


int oak_record_field(const oak_registered_record_t* s,
                                   const char* name)
{
  const oak_record_field_t* fields =
      OAK_CDATA(oak_record_field_t, s->fields);
  for (usize i = 0; i < oak_size(s->fields); ++i)
  {
    if (strcmp(fields[i].name, name) == 0)
      return (int)i;
  }
  return -1;
}

const oak_registered_fn_t*
oak_find_record_method(const oak_registered_record_t* sd,
                                const char* name,
                                const int static_only)
{
  if (!sd)
    return null;
  const oak_registered_fn_t* methods =
      OAK_CDATA(oak_registered_fn_t, sd->methods);
  for (usize i = 0; i < oak_size(sd->methods); ++i)
  {
    const oak_registered_fn_t* m = &methods[i];
    if ((!!m->is_static) != (!!static_only))
      continue;
    if (strcmp(m->name, name) == 0)
      return m;
  }
  return null;
}

int oak_record_field_index(
    const oak_compiler_t* c,
    oak_type_t recv_ty,
    const char* field_name,
    const oak_registered_record_t** out_sd)
{
  if (!oak_type_is_known(&recv_ty))
    return -1;
  const oak_registered_record_t* sd =
      oak_records_find_by_id(&c->records, recv_ty.id);
  if (!sd)
    return -1;
  if (out_sd)
    *out_sd = sd;
  return oak_record_field(sd, field_name);
}

int oak_require_record_field(
    oak_compiler_t* c,
    const oak_ast_node_t* recv,
    const oak_ast_node_t* fname,
    const int is_assignment,
    const oak_registered_record_t** out_sd)
{
  oak_type_t recv_ty;
  oak_infer_type(c, recv, &recv_ty);
  const char* ftext = oak_token_text(fname->token);
  const oak_registered_record_t* sd = null;
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
