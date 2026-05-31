#include "internal/oak_compiler.h"

#include <string.h>

/* Shared attribute storage for all native items — borrowed by every
 * oak_registered_*_t that is registered from the C binding API. */

/* Find the oak_bind_type_t whose type_id matches, or null. */
static const struct oak_bind_type_t*
find_bind_type_by_id(const struct oak_compile_options_t* opts,
                     oak_type_id_t type_id)
{
  for (int i = 0; i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* t = opts->native_types.items[i];
    if (t && t->type_id == type_id)
      return t;
  }
  return null;
}

/* Find a compiled module by its dotted name (linear scan). */
static const struct oak_module_t*
find_module_by_dotted(const struct oak_module_registry_t* reg,
                      const char* dotted)
{
  if (!reg || !dotted)
    return null;
  for (int i = 0; i < reg->modules.count; ++i)
  {
    const struct oak_module_t* m = reg->modules.items[i];
    if (m && m->dotted_name && strcmp(m->dotted_name, dotted) == 0)
      return m;
  }
  return null;
}

/* Find a method export entry by name in a record export. */
static const struct oak_module_export_record_method_t*
find_method_export(const struct oak_module_export_record_t* rec,
                   const char* name)
{
  for (int i = 0; i < rec->method_count; ++i)
    if (strcmp(rec->methods[i].name, name) == 0)
      return &rec->methods[i];
  return null;
}

/* Lower a public oak_bind_type_ref_t into an internal oak_type_t. */
static void lower_bind_ref(const struct oak_bind_type_ref_t* r,
                           struct oak_type_t* out)
{
  oak_type_clear(out);
  out->kind = r->kind;
  out->id = r->id;
  if (r->kind == OAK_TYPE_KIND_MAP)
    out->key_id = r->key_id;
}

/* ---------- Native type registration ---------- */

void oak_register_native_types(
    struct oak_compiler_t* c, const struct oak_compile_options_t* opts)
{
  if (!opts || opts->native_types.count == 0)
    return;

  /* Resume from the cursor: entries before it were registered by an earlier
   * pass (see oak_compiler_t.native_types_cursor). */
  for (int i = c->native_types_cursor; i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* nt = opts->native_types.items[i];
    if (!nt)
      continue;
    const int nt_name_len = (int)strlen(nt->name);

    if (oak_records_find(&c->records, nt->name, (usize)nt_name_len))
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
        &c->types, nt->name, nt_name_len, nt->type_id);
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
    proto.name_len = nt_name_len;
    proto.type_id = tid;
    proto.fields = null;
    proto.field_count = 0;
    proto.field_capacity = 0;
    proto.attrs = null;
    proto.attr_count = 0;
    proto.is_value = (nt->kind == OAK_BIND_TYPE_VALUE);

    for (int fi = 0; fi < nt->field_count; ++fi)
    {
      const struct oak_bind_field_t* nf = &nt->fields[fi];
      struct oak_record_field_t sf = {
        .name = nf->name,
        .name_len = (int)strlen(nf->name),
      };
      lower_bind_ref(&nf->type, &sf.type);
      oak_dynarr_push(c->allocator, &proto.fields,
                      &proto.field_count,
                      &proto.field_capacity,
                      &sf,
                      sizeof(sf));
    }

    oak_record_registry_insert(&c->records, &proto);
    if (c->has_error)
      return;
  }

  c->native_types_cursor = opts->native_types.count;
}

/* ---------- Native function registration ---------- */

static void record_append_method(struct oak_allocator_t* allocator,
                                 struct oak_registered_record_t* sd,
                                 const struct oak_registered_fn_t* m)
{
  oak_dynarr_push(allocator, &sd->methods.items,
                  &sd->methods.count,
                  &sd->methods.capacity,
                  m,
                  sizeof(*m));
}

void oak_register_native_fns(struct oak_compiler_t* c,
                                      const struct oak_compile_options_t* opts)
{
  if (!opts)
    return;

  /* Both loops resume from their cursors so a second registration pass only
   * sees bindings added since the first (see native_*_cursor). */
  for (int i = c->native_global_fns_cursor; i < opts->native_global_fns.count; ++i)
  {
    const struct oak_bind_global_fn_t* b = &opts->native_global_fns.items[i];
    if (!b->name || !b->impl || b->module_name)
      continue;

    const int name_len = (int)strlen(b->name);
    struct oak_obj_native_fn_t* native =
        oak_native_fn_new(c->allocator, b->impl, b->arity, b->name);
    const u16 idx =
        oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&native->obj));

    struct oak_registered_fn_t entry = { 0 };
    entry.name = b->name;
    entry.name_len = name_len;
    entry.const_idx = idx;
    entry.arity = b->arity;
    lower_bind_ref(&b->return_type, &entry.return_type);
    entry.decl = null;
    entry.attrs = null;
    entry.attr_count = 0;
    entry.source_module_id = OAK_MODULE_ID_NONE;
    if (b->param_types && b->arity > 0)
    {
      entry.param_types = OAK_ALLOC(
          c->allocator, (usize)b->arity * sizeof(struct oak_type_t));
      for (int pi = 0; pi < b->arity; ++pi)
        lower_bind_ref(&b->param_types[pi], &entry.param_types[pi]);
    }

    if (oak_fn_registry_find(&c->fns, b->name, name_len))
    {
      oak_compiler_error_at(c, null, "duplicate native function '%s'", b->name);
      return;
    }
    oak_fn_registry_insert(&c->fns, &entry);
    if (c->has_error)
      return;
  }

  c->native_global_fns_cursor = opts->native_global_fns.count;

  for (int i = c->native_fns_cursor; i < opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* b = &opts->native_fns.items[i];
    if (!b->name || !b->impl)
      continue;

    const int name_len = (int)strlen(b->name);
    const int vm_arity = (b->kind == OAK_BIND_FN_INSTANCE_METHOD)
                             ? b->arity + 1
                             : b->arity;
    struct oak_obj_native_fn_t* native =
        oak_native_fn_new(c->allocator, b->impl, vm_arity, b->name);

    /* Apply runtime attribute hooks from the module stub, if the receiver type
     * belongs to a module that has a compiled stub with attributed methods. */
    if (c->module_registry && c->opts)
    {
      const struct oak_bind_type_t* bind_type =
          find_bind_type_by_id(opts, b->receiver_type_id);
      if (bind_type && bind_type->module_name)
      {
        const struct oak_module_t* stub_mod =
            find_module_by_dotted(c->module_registry, bind_type->module_name);
        if (stub_mod)
        {
          const struct oak_module_export_record_t* rec_exp =
              oak_module_find_export_record(
                  stub_mod, bind_type->name);
          if (rec_exp)
          {
            const struct oak_module_export_record_method_t* mexp =
                find_method_export(rec_exp, b->name);
            if (mexp && mexp->stub_attr_count > 0)
              oak_apply_attr_hooks(c->opts,
                                   null,
                                   native,
                                   mexp->stub_attrs,
                                   mexp->stub_attr_count);
          }
        }
      }
    }

    const u16 idx =
        oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&native->obj));

    struct oak_registered_fn_t entry = { 0 };
    entry.name = b->name;
    entry.name_len = name_len;
    entry.const_idx = idx;
    entry.receiver_type_id = b->receiver_type_id;
    lower_bind_ref(&b->return_type, &entry.return_type);
    entry.decl = null;
    entry.attrs = null;
    entry.attr_count = 0;
    entry.source_module_id = OAK_MODULE_ID_NONE;

    struct oak_registered_record_t* sd =
        (struct oak_registered_record_t*)oak_records_find_by_id(
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
    /* Per-parameter types: for instance methods slot 0 is the implicit self
     * (the receiver type), with the user-facing params following. */
    if (b->param_types && vm_arity > 0)
    {
      entry.param_types =
          OAK_ALLOC(c->allocator, (usize)vm_arity * sizeof(struct oak_type_t));
      int slot = 0;
      if (!is_static)
      {
        oak_type_clear(&entry.param_types[slot]);
        entry.param_types[slot].id = b->receiver_type_id;
        ++slot;
      }
      for (int pi = 0; pi < b->arity; ++pi, ++slot)
        lower_bind_ref(&b->param_types[pi], &entry.param_types[slot]);
    }
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
    record_append_method(c->allocator, sd, &entry);
    if (c->has_error)
      return;
  }

  c->native_fns_cursor = opts->native_fns.count;
}
