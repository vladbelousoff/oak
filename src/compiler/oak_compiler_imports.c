#include "internal/oak_compiler.h"
#include "oak_memory.h"

#define OC_FOR_EACH_DEP(c, dep)                                                \
  if ((c)->module_registry && (c)->current_module)                             \
    for (int _di = 0; _di < (c)->current_module->import_modules.count; ++_di) \
      if (((dep) = oak_module_registry_get(                                    \
               (c)->module_registry,                                           \
               (c)->current_module->import_modules.items[_di])) != null)

/* Pre-register enum variants from all imported modules so that cross-module
 * `alias.EnumName.Variant` expressions can be resolved.  Variants are stored
 * as small integers; we intern OAK_VALUE_I32(value) as constants in the
 * current chunk and register them in c->enums with the local const_idx.
 * Must run BEFORE oakc_register_program_enums. */
void register_imported_enums(struct oak_compiler_t* c)
{
  const struct oak_module_t* dep;
  OC_FOR_EACH_DEP(c, dep)
    for (int ei = 0; ei < dep->exports_enum.count; ++ei)
    {
      const struct oak_module_export_enum_t* exp = &dep->exports_enum.items[ei];
      /* Skip if this enum name is already registered (diamond imports). */
      if (oakc_is_enum_name(&c->enums, exp->name, exp->name_len))
        continue;
      const oak_type_id_t enum_type_id =
          oak_type_registry_intern(&c->types, exp->name, exp->name_len);
      if (enum_type_id < 0)
      {
        oak_compiler_error_at(
            c, null, "failed to register imported enum as a type");
        return;
      }
      for (int vi = 0; vi < exp->variant_count; ++vi)
      {
        const struct oak_module_export_enum_variant_t* v = &exp->variants[vi];
        /* Skip if the unqualified variant name already exists. */
        if (oak_enum_registry_find(&c->enums, v->name, v->name_len))
          continue;
        const u16 local_idx =
            oak_compiler_intern_constant(c, OAK_VALUE_I32(v->value));
        if (c->has_error)
          return;
        struct oak_enum_variant_t ev = {
          .name = v->name,
          .name_len = v->name_len,
          .enum_name = exp->name,
          .enum_name_len = exp->name_len,
          .const_idx = local_idx,
          .value = v->value,
          .type_id = enum_type_id,
        };
        oak_enum_registry_insert(&c->enums, &ev);
      }
    }
}

/* For each imported module in the current module's dependency list,
 * pre-register its exported record types into the current compiler's type and
 * record registries.  This must run BEFORE
 * oakc_register_program_records so that user-defined type IDs are
 * assigned in a consistent topological order across all modules. */
void register_imported_records(struct oak_compiler_t* c)
{
  const struct oak_module_t* dep;
  OC_FOR_EACH_DEP(c, dep)
    for (int ri = 0; ri < dep->exports_record.count; ++ri)
    {
      const struct oak_module_export_record_t* exp =
          &dep->exports_record.items[ri];
      /* Skip if already registered (diamond imports). */
      if (oakc_records_find(
              &c->records, exp->name, exp->name_len))
        continue;
      const oak_type_id_t tid =
          oak_type_registry_intern(&c->types, exp->name, exp->name_len);
      struct oak_registered_record_t proto = { 0 };
      proto.name = exp->name;
      proto.name_len = exp->name_len;
      proto.type_id = tid;
      oak_dynarr_init(&proto.fields, &proto.field_count, &proto.field_capacity);
      oak_dynarr_init(
          &proto.methods.items, &proto.methods.count, &proto.methods.capacity);
      for (int fi = 0; fi < exp->field_count; ++fi)
      {
        struct oak_record_field_t field = {
          .name = exp->fields[fi].name,
          .name_len = exp->fields[fi].name_len,
        };
        oak_type_clear(&field.type);
        field.type.id =
            oak_type_registry_intern(&c->types,
                                     exp->fields[fi].type_name,
                                     exp->fields[fi].type_name_len);
        field.type.is_weak = exp->fields[fi].is_weak;
        oak_dynarr_push(c->allocator, &proto.fields,
                        &proto.field_count,
                        &proto.field_capacity,
                        &field,
                        sizeof(field));
      }
      oak_record_registry_insert(&c->records, &proto);
    }
}

/* Populate the current module's export tables from the now-fully-populated
 * compiler registries.  All exports use the module's lexer-arena strings, so
 * they remain valid as long as the module is alive. */
void populate_module_exports(struct oak_compiler_t* c)
{
  if (!c->current_module)
    return;
  struct oak_module_t* mod = c->current_module;
  for (int i = 0; i < c->fns.entries.count; ++i)
  {
    const struct oak_registered_fn_t* e = &c->fns.entries.items[i];
    /* Only export fns that come from the user's source (decl != null) AND are
     * global (no receiver).  Native fns and methods are not exposed cross-
     * module in v1. */
    if (!e->decl || e->receiver_type_id != OAK_TYPE_VOID)
      continue;
    struct oak_module_export_fn_t exp = {
      .name = e->name,
      .name_len = e->name_len,
      .const_idx = e->const_idx,
      .arity = e->arity,
      .return_type_node = oakc_fn_return_type_node(e->decl),
      .return_type_id = OAK_TYPE_VOID,
      .return_kind = OAK_TYPE_KIND_SCALAR,
      .stub_attrs = oakc_alloc_attrs(c->allocator, e->attrs, e->attr_count),
      .stub_attr_count = e->attr_count,
    };
    const int idx = mod->exports_fn.count;
    oak_dynarr_push(c->allocator, &mod->exports_fn.items,
                    &mod->exports_fn.count,
                    &mod->exports_fn.capacity,
                    &exp,
                    sizeof(exp));
    oak_htable_insert(&mod->exports_fn.by_name, e->name, e->name_len, idx);
  }
  /* Export user-defined records (those registered after native + imported ones,
   * i.e. with index >= c->user_record_start). */
  for (int i = c->user_record_start; i < c->records.entries.count; ++i)
  {
    const struct oak_registered_record_t* r = &c->records.entries.items[i];
    struct oak_module_export_record_t exp = { 0 };
    exp.name = r->name;
    exp.name_len = r->name_len;
    oak_dynarr_init(&exp.fields, &exp.field_count, &exp.field_capacity);
    for (int fi = 0; fi < r->field_count; ++fi)
    {
      struct oak_module_export_record_field_t field = {
        .name = r->fields[fi].name,
        .name_len = r->fields[fi].name_len,
        .is_weak = r->fields[fi].type.is_weak,
      };
      /* Resolve type_id back to a name via the type registry so the importing
       * module can re-intern it using its own registry. */
      if (r->fields[fi].type.id >= 0 && r->fields[fi].type.id < c->types.count)
      {
        field.type_name = c->types.entries[r->fields[fi].type.id].name;
        field.type_name_len =
            c->types.entries[r->fields[fi].type.id].len;
      }
      oak_dynarr_push(c->allocator, &exp.fields,
                      &exp.field_count,
                      &exp.field_capacity,
                      &field,
                      sizeof(field));
    }
    /* Export per-method stub attrs for bodyless native stub methods so that
     * calling modules can wire runtime hooks onto the native fn objects they
     * create via oakc_register_native_fns. */
    oak_dynarr_init(&exp.methods, &exp.method_count, &exp.method_capacity);
    for (int mi = 0; mi < r->methods.count; ++mi)
    {
      const struct oak_registered_fn_t* m = &r->methods.items[mi];
      if (m->attr_count == 0)
        continue;
      struct oak_module_export_record_method_t mexp = {
        .name = m->name,
        .name_len = m->name_len,
        .stub_attrs = oakc_alloc_attrs(c->allocator, m->attrs, m->attr_count),
        .stub_attr_count = m->attr_count,
      };
      oak_dynarr_push(c->allocator, &exp.methods,
                      &exp.method_count,
                      &exp.method_capacity,
                      &mexp,
                      sizeof(mexp));
    }
    exp.layout_id = 0; /* populated on first cross-module new when needed */
    const int idx = mod->exports_record.count;
    oak_dynarr_push(c->allocator, &mod->exports_record.items,
                    &mod->exports_record.count,
                    &mod->exports_record.capacity,
                    &exp,
                    sizeof(exp));
    oak_htable_insert(
        &mod->exports_record.by_name, exp.name, exp.name_len, idx);
  }
  /* Export user-defined enums (those registered after native + imported ones).
   * We group variants by enum_name to produce one export per enum type. */
  if (c->user_enum_start >= 0)
  {
    for (int i = c->user_enum_start; i < c->enums.variants.count; ++i)
    {
      const struct oak_enum_variant_t* v = &c->enums.variants.items[i];
      /* Find or create the export entry for this enum type. */
      int eidx = oak_htable_get(
          &mod->exports_enum.by_name, v->enum_name, v->enum_name_len);
      if (eidx < 0)
      {
        struct oak_module_export_enum_t ee = { 0 };
        ee.name = v->enum_name;
        ee.name_len = v->enum_name_len;
        eidx = mod->exports_enum.count;
        oak_dynarr_push(c->allocator, &mod->exports_enum.items,
                        &mod->exports_enum.count,
                        &mod->exports_enum.capacity,
                        &ee,
                        sizeof(ee));
        oak_htable_insert(
            &mod->exports_enum.by_name, ee.name, ee.name_len, eidx);
      }
      struct oak_module_export_enum_t* ee = &mod->exports_enum.items[eidx];
      struct oak_module_export_enum_variant_t variant = {
        .name = v->name,
        .name_len = v->name_len,
        .value = v->value,
      };
      oak_dynarr_push(c->allocator, &ee->variants,
                      &ee->variant_count,
                      &ee->variant_capacity,
                      &variant,
                      sizeof(variant));
    }
  }
}
