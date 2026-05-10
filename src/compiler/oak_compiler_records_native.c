#include "internal/oak_compiler.h"

/* ---------- Native type registration ---------- */

void oakc_register_native_types(
    struct oak_compiler_t* c, const struct oak_compile_options_t* opts)
{
  if (!opts || opts->native_types.count == 0)
    return;

  for (int i = 0; i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* nt = opts->native_types.items[i];
    if (!nt)
      continue;

    if (oakc_records_find(&c->records, nt->name, nt->name_len))
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

/* ---------- Native function registration ---------- */

static void record_append_method(struct oak_registered_record_t* sd,
                                 const struct oak_registered_fn_t* m)
{
  oak_dynarr_push(&sd->methods.items,
                  &sd->methods.count,
                  &sd->methods.capacity,
                  m,
                  sizeof(*m));
}

void oakc_register_native_fns(struct oak_compiler_t* c,
                                      const struct oak_compile_options_t* opts)
{
  if (!opts || opts->native_fns.count == 0)
    return;

  for (int i = 0; i < opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* b = &opts->native_fns.items[i];
    if (!b->name || !b->impl)
      continue;
    if (b->kind == OAK_BIND_FN_GLOBAL && b->module_name)
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
        oakc_intern_native_const(c, b->impl, vm_arity, b->name);

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
          (struct oak_registered_record_t*)oakc_records_find_by_id(
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
        if (strcmp(sd->methods.items[j].name, b->name) == 0)
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
