#include "internal/oak_compiler.h"

#include <string.h>

/* Shared attribute storage for all native items — borrowed by every
 * oak_registered_*_t that is registered from the C binding API. */

/* Find a compiled module by its dotted name (linear scan). */
static const struct oak_module_t*
find_module_by_dotted(const struct oak_module_registry_t* reg,
                      const char* dotted)
{
  if (!reg || !dotted)
    return null;
  for (int i = 0; i < oak_dynarr_count(reg->modules); ++i)
  {
    const struct oak_module_t* m = reg->modules[i];
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
  for (int i = 0; i < oak_dynarr_count(rec->methods); ++i)
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
  out->id = r->type ? r->type->resolved_type_id : r->id;
  if (r->kind == OAK_TYPE_KIND_MAP)
    out->key_id = r->key_type ? r->key_type->resolved_type_id : r->key_id;
}

/* ---------- Native type registration ---------- */

void oak_register_native_types(
    struct oak_compiler_t* c, const struct oak_compile_options_t* opts)
{
  if (!opts || oak_dynarr_count(opts->native_types) == 0)
    return;

  /* Assign every descriptor first so fields/signatures may reference a type
   * registered later in the binding list. */
  for (int i = c->native_types_cursor; i < oak_dynarr_count(opts->native_types);
       ++i)
  {
    struct oak_bind_type_t* nt = opts->native_types[i];
    if (!nt)
      continue;
    nt->resolved_type_id = oak_type_registry_intern(&c->types, nt->name);
  }

  /* Resume from the cursor: entries before it were registered by an earlier
   * pass (see oak_compiler_t.native_types_cursor). */
  for (int i = c->native_types_cursor; i < oak_dynarr_count(opts->native_types); ++i)
  {
    struct oak_bind_type_t* nt = opts->native_types[i];
    if (!nt)
      continue;

    if (oak_records_find(&c->records, nt->name))
    {
      oak_compiler_error_at(
          c,
          null,
          "native type '%s' conflicts with an already-registered type",
          nt->name);
      return;
    }

    const oak_type_id_t tid = nt->resolved_type_id;
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
    proto.type_id = tid;
    proto.source_module_id = OAK_MODULE_ID_NONE;
    oak_assert(oak_dynarr_init(c->allocator, &proto.fields, sizeof *proto.fields));
    oak_assert(oak_dynarr_init(c->allocator, &proto.methods, sizeof *proto.methods));
    proto.attrs = null;
    proto.attr_count = 0;
    proto.is_value = (nt->kind == OAK_BIND_TYPE_VALUE);

    for (int fi = 0; fi < oak_dynarr_count(nt->fields); ++fi)
    {
      const struct oak_bind_field_t* nf = &nt->fields[fi];
      struct oak_record_field_t sf = {
        .name = nf->name,
      };
      lower_bind_ref(&nf->type, &sf.type);
      oak_assert(oak_dynarr_push(&proto.fields, &sf));
    }

    if (!oak_compiler_declare_symbol(
            c, null, proto.name, OAK_SYMBOL_RECORD,
            oak_dynarr_count(c->records.entries), OAK_MODULE_ID_NONE, 0))
      return;
    oak_record_registry_insert(&c->records, &proto);
    if (c->has_error)
      return;
  }

  c->native_types_cursor = oak_dynarr_count(opts->native_types);
}

/* ---------- Native function registration ---------- */

static void record_append_method(struct oak_registered_record_t* sd,
                                 const struct oak_registered_fn_t* m)
{
  oak_assert(oak_dynarr_push(&sd->methods, m));
}

void oak_register_native_fns(struct oak_compiler_t* c,
                                      const struct oak_compile_options_t* opts)
{
  if (!opts)
    return;

  /* Both loops resume from their cursors so a second registration pass only
   * sees bindings added since the first (see native_*_cursor). */
  for (int i = c->native_global_fns_cursor; i < oak_dynarr_count(opts->native_global_fns); ++i)
  {
    const struct oak_bind_global_fn_t* b = &opts->native_global_fns[i];
    if (!b->name || !b->impl || b->module_name)
      continue;

    struct oak_obj_native_fn_t* native = oak_native_fn_new(
        c->allocator, b->impl, b->arity, b->name, b->user_data);
    const u16 idx =
        oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&native->obj));

    struct oak_registered_fn_t entry = { 0 };
    entry.name = b->name;
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

    if (oak_fn_registry_find(&c->fns, b->name))
    {
      oak_compiler_error_at(c, null, "duplicate native function '%s'", b->name);
      return;
    }
    if (!oak_compiler_declare_symbol(
            c, null, entry.name, OAK_SYMBOL_FUNCTION,
            oak_dynarr_count(c->fns.entries), OAK_MODULE_ID_NONE, 0))
      return;
    oak_fn_registry_insert(&c->fns, &entry);
    if (c->has_error)
      return;
  }

  c->native_global_fns_cursor = oak_dynarr_count(opts->native_global_fns);

  for (int i = c->native_fns_cursor; i < oak_dynarr_count(opts->native_fns); ++i)
  {
    const struct oak_bind_fn_t* b = &opts->native_fns[i];
    if (!b->name || !b->impl)
      continue;

    const int vm_arity = (b->kind == OAK_BIND_FN_INSTANCE_METHOD)
                             ? b->arity + 1
                             : b->arity;
    struct oak_obj_native_fn_t* native = oak_native_fn_new(
        c->allocator, b->impl, vm_arity, b->name, b->user_data);

    /* Apply runtime attribute hooks from the module stub, if the receiver type
     * belongs to a module that has a compiled stub with attributed methods. */
    if (c->module_registry && c->opts)
    {
      const struct oak_bind_type_t* bind_type = b->receiver_type;
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
    entry.const_idx = idx;
    entry.receiver_type_id = b->receiver_type->resolved_type_id;
    lower_bind_ref(&b->return_type, &entry.return_type);
    entry.decl = null;
    entry.attrs = null;
    entry.attr_count = 0;
    entry.source_module_id = OAK_MODULE_ID_NONE;

    struct oak_registered_record_t* sd =
        (struct oak_registered_record_t*)oak_records_find_by_id(
            &c->records, entry.receiver_type_id);
    if (!sd)
    {
      oak_compiler_error_at(c,
                            null,
                            "native method '%s': no record registered for "
                            "receiver type id %d",
                            b->name,
                            entry.receiver_type_id);
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
        entry.param_types[slot].id = entry.receiver_type_id;
        ++slot;
      }
      for (int pi = 0; pi < b->arity; ++pi, ++slot)
        lower_bind_ref(&b->param_types[pi], &entry.param_types[slot]);
    }
    for (int j = 0; j < oak_dynarr_count(sd->methods); ++j)
    {
      if (strcmp(sd->methods[j].name, b->name) == 0)
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
    if (c->has_error)
      return;
  }

  c->native_fns_cursor = oak_dynarr_count(opts->native_fns);
}
