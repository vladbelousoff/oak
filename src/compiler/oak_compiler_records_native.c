#include "internal/oak_compiler.h"

#include <string.h>

/* Shared attribute storage for all native items — borrowed by every
 * oak_registered_*_t that is registered from the C binding API. */

/* Find a compiled module by its dotted name (linear scan). */
static const oak_module_t*
find_module_by_dotted(const oak_module_registry_t* reg,
                      const char* dotted)
{
  if (!reg || !dotted)
    return null;
  oak_module_t* const* modules =
      OAK_DATA(oak_module_t*, reg->modules);
  for (usize i = 0; i < oak_size(reg->modules); ++i)
  {
    const oak_module_t* m = modules[i];
    if (m && m->dotted_name && strcmp(m->dotted_name, dotted) == 0)
      return m;
  }
  return null;
}

/* Whether a module-scoped binding belongs in the namespace being compiled.
 *
 * A descriptor with no module_name is global and always belongs. One that
 * names a module belongs only while that module itself is being compiled --
 * everywhere else it is reached through `import`, and the module loader
 * installs it on the module's exports.
 *
 * Without this check oak_bind_type_in_module(opts, "io", ..., "File") also
 * registered a bare global `File`, with all its methods, in every compilation
 * unit. The enum and global-function passes already filtered on module_name;
 * the type and method passes did not. */
static int binding_is_in_scope(const oak_compiler_t* c,
                               const char* module_name)
{
  if (!module_name)
    return 1;
  return c->current_module && c->current_module->dotted_name &&
         strcmp(c->current_module->dotted_name, module_name) == 0;
}

/* Find a method export entry by name in a record export. */
static const oak_module_export_record_method_t*
find_method_export(const oak_module_export_record_t* rec,
                   const char* name)
{
  const oak_module_export_record_method_t* methods =
      OAK_CDATA(oak_module_export_record_method_t, rec->methods);
  for (usize i = 0; i < oak_size(rec->methods); ++i)
    if (strcmp(methods[i].name, name) == 0)
      return &methods[i];
  return null;
}


void oak_register_native_types(
    oak_compiler_t* c, const oak_compile_options_t* opts)
{
  if (!opts || oak_size(opts->native_types) == 0)
    return;

  oak_bind_type_t** native_types =
      OAK_DATA(oak_bind_type_t*, opts->native_types);

  /* Assign the ids first so fields and signatures may reference a type
   * registered later in the binding list.  Out-of-scope module types are
   * skipped in both passes: interning the name is what makes it resolvable in
   * a type position, so doing it here would put a bare `File` in scope no
   * matter what the second pass declares.  Their ids are assigned by the
   * module loader when their own module is compiled. */
  for (usize i = (usize)c->native_types_cursor;
       i < oak_size(opts->native_types);
       ++i)
  {
    oak_bind_type_t* nt = native_types[i];
    if (!nt || !binding_is_in_scope(c, nt->module_name))
      continue;
    nt->resolved_type_id = oak_type_registry_intern(&c->types, nt->name);
  }

  /* Resume from the cursor: entries before it were registered by an earlier
   * pass (see oak_compiler_t.native_types_cursor). */
  for (usize i = (usize)c->native_types_cursor;
       i < oak_size(opts->native_types);
       ++i)
  {
    oak_bind_type_t* nt = native_types[i];
    if (!nt || !binding_is_in_scope(c, nt->module_name))
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
    oak_registered_record_t proto = { 0 };
    proto.name = nt->name;
    proto.type_id = tid;
    proto.source_module_id = OAK_MODULE_ID_NONE;
    proto.fields =
        oak_vector_new(c->allocator, sizeof(oak_record_field_t));
    proto.methods =
        oak_vector_new(c->allocator, sizeof(oak_registered_fn_t));
    proto.interface_names =
        oak_vector_new(c->allocator, sizeof(const char*));
    proto.interfaces = oak_vector_new(c->allocator, sizeof(oak_type_t));
    oak_assert(proto.fields && proto.methods && proto.interface_names &&
               proto.interfaces);
    proto.attrs = null;
    proto.attr_count = 0;
    proto.is_value = (nt->kind == OAK_BIND_TYPE_VALUE);

    char* const* interface_names = OAK_DATA(char*, nt->interface_names);
    for (usize ii = 0; ii < oak_size(nt->interface_names); ++ii)
    {
      const char* interface_name = interface_names[ii];
      oak_assert(oak_push_back(proto.interface_names, &interface_name));
    }

    const oak_bind_field_t* nt_fields =
        OAK_CDATA(oak_bind_field_t, nt->fields);
    for (usize fi = 0; fi < oak_size(nt->fields); ++fi)
    {
      const oak_bind_field_t* nf = &nt_fields[fi];
      oak_record_field_t sf = {
        .name = nf->name,
      };
      oak_lower_bind_ref(&nf->type, &sf.type);
      oak_assert(oak_push_back(proto.fields, &sf));
    }

    if (!oak_compiler_declare_symbol(
            c, null, proto.name, OAK_SYMBOL_RECORD,
            (int)oak_size(c->records.entries), OAK_MODULE_ID_NONE, 0))
      return;
    oak_record_registry_insert(&c->records, &proto);
    if (c->has_error)
      return;
  }

  c->native_types_cursor = (int)oak_size(opts->native_types);
}


static void record_append_method(oak_registered_record_t* sd,
                                 const oak_registered_fn_t* m)
{
  oak_assert(oak_push_back(sd->methods, m));
}

void oak_register_native_fns(oak_compiler_t* c,
                                      const oak_compile_options_t* opts)
{
  if (!opts)
    return;

  /* Both loops resume from their cursors so a second registration pass only
   * sees bindings added since the first (see native_*_cursor). */
  const oak_bind_global_fn_t* global_fns =
      OAK_CDATA(oak_bind_global_fn_t, opts->native_global_fns);
  for (usize i = (usize)c->native_global_fns_cursor;
       i < oak_size(opts->native_global_fns);
       ++i)
  {
    const oak_bind_global_fn_t* b = &global_fns[i];
    if (!b->name || !b->impl || b->module_name)
      continue;

    oak_obj_native_fn_t* native = oak_native_fn_new(
        c->allocator, b->impl, b->param_count, b->name, b->user_data);
    const u16 idx =
        oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&native->obj));

    oak_registered_fn_t entry = { 0 };
    entry.name = b->name;
    entry.const_idx = idx;
    entry.arity = (int)b->param_count;
    oak_lower_bind_ref(&b->return_type, &entry.return_type);
    entry.decl = null;
    entry.attrs = null;
    entry.attr_count = 0;
    entry.source_module_id = OAK_MODULE_ID_NONE;
    if (b->param_types && b->param_count > 0)
    {
      entry.param_types = oak_alloc(
          c->allocator, (usize)b->param_count * sizeof(oak_type_t), OAK_HERE);
      for (usize pi = 0; pi < b->param_count; ++pi)
        oak_lower_bind_ref(&b->param_types[pi], &entry.param_types[pi]);
    }

    if (oak_fn_registry_find(&c->fns, b->name))
    {
      oak_compiler_error_at(c, null, "duplicate native function '%s'", b->name);
      return;
    }
    if (!oak_compiler_declare_symbol(
            c, null, entry.name, OAK_SYMBOL_FUNCTION,
            (int)oak_size(c->fns.entries), OAK_MODULE_ID_NONE, 0))
      return;
    oak_fn_registry_insert(&c->fns, &entry);
    if (c->has_error)
      return;
  }

  c->native_global_fns_cursor = (int)oak_size(opts->native_global_fns);

  const oak_bind_fn_t* native_fns =
      OAK_CDATA(oak_bind_fn_t, opts->native_fns);
  for (usize i = (usize)c->native_fns_cursor;
       i < oak_size(opts->native_fns);
       ++i)
  {
    const oak_bind_fn_t* b = &native_fns[i];
    if (!b->name || !b->impl)
      continue;
    if (b->receiver_type &&
        !binding_is_in_scope(c, b->receiver_type->module_name))
      continue;

    /* The compiler's registries index and offset with signed arithmetic (the
     * implicit self is a -1/+1 adjustment throughout), so the descriptor's
     * count converts here, once, at the boundary. oak_bind_fn has already
     * capped it at OAK_MAX_ARITY. */
    const int vm_arity = (b->kind == OAK_BIND_FN_INSTANCE_METHOD)
                             ? (int)b->param_count + 1
                             : (int)b->param_count;
    oak_obj_native_fn_t* native = oak_native_fn_new(
        c->allocator, b->impl, vm_arity, b->name, b->user_data);
    /* Reaches the callback as oak_native_call_t::self_type, so a method can
     * unwrap its receiver and construct new instances of its own type without
     * the binding passing the descriptor through user_data. */
    native->self_type = b->receiver_type;

    /* Apply runtime attribute hooks from the module stub, if the receiver type
     * belongs to a module that has a compiled stub with attributed methods. */
    if (c->module_registry && c->opts)
    {
      const oak_bind_type_t* bind_type = b->receiver_type;
      if (bind_type && bind_type->module_name)
      {
        const oak_module_t* stub_mod =
            find_module_by_dotted(c->module_registry, bind_type->module_name);
        if (stub_mod)
        {
          const oak_module_export_record_t* rec_exp =
              oak_module_find_export_record(
                  stub_mod, bind_type->name);
          if (rec_exp)
          {
            const oak_module_export_record_method_t* mexp =
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

    oak_registered_fn_t entry = { 0 };
    entry.name = b->name;
    entry.const_idx = idx;
    entry.receiver_type_id = b->receiver_type->resolved_type_id;
    oak_lower_bind_ref(&b->return_type, &entry.return_type);
    entry.decl = null;
    entry.attrs = null;
    entry.attr_count = 0;
    entry.source_module_id = OAK_MODULE_ID_NONE;

    oak_registered_record_t* sd =
        (oak_registered_record_t*)oak_records_find_by_id(
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
      entry.param_types = oak_alloc(
          c->allocator, (usize)vm_arity * sizeof(oak_type_t), OAK_HERE);
      int slot = 0;
      if (!is_static)
      {
        oak_type_clear(&entry.param_types[slot]);
        entry.param_types[slot].id = entry.receiver_type_id;
        ++slot;
      }
      for (usize pi = 0; pi < b->param_count; ++pi, ++slot)
        oak_lower_bind_ref(&b->param_types[pi], &entry.param_types[slot]);
    }
    const oak_registered_fn_t* methods =
        OAK_CDATA(oak_registered_fn_t, sd->methods);
    for (usize j = 0; j < oak_size(sd->methods); ++j)
    {
      if (strcmp(methods[j].name, b->name) == 0)
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

  c->native_fns_cursor = (int)oak_size(opts->native_fns);
}
