#include "internal/oak_compiler.h"
#include "internal/oak_trait_registry.h"

#include <string.h>

static void ensure_dep_type_imported(struct oak_compiler_t* c,
                                     const struct oak_module_t* dep,
                                     oak_type_id_t src_id);
static int ensure_full_type_imported(struct oak_compiler_t* c,
                                     const struct oak_module_t* dep,
                                     const struct oak_type_t* type);
static int ensure_sig_types_imported(struct oak_compiler_t* c,
                                     const struct oak_module_t* dep,
                                     const struct oak_type_t* return_type,
                                     const struct oak_type_t* param_types,
                                     int arity);

/* Translate a type from a dependency module's type registry into the local
 * compiler's registry.  Builtin IDs (number, string, bool, void, none) are
 * stable across registries and need no translation. */
static struct oak_type_t translate_type(struct oak_compiler_t* c,
                                        const struct oak_module_t* dep,
                                        struct oak_type_t src)
{
  struct oak_type_t dst = src;
  if (src.id >= OAK_TYPE_FIRST_USER)
  {
    const char* name = oak_type_registry_name(&dep->types, src.id);
    dst.id = oak_type_registry_intern(&c->types, name, (int)strlen(name));
  }
  if (src.key_id >= OAK_TYPE_FIRST_USER)
  {
    const char* name = oak_type_registry_name(&dep->types, src.key_id);
    dst.key_id = oak_type_registry_intern(&c->types, name, (int)strlen(name));
  }
  return dst;
}

/* Allocate and translate a param_types array from a dependency module. */
static struct oak_type_t* translate_param_types(struct oak_compiler_t* c,
                                                const struct oak_module_t* dep,
                                                const struct oak_type_t* src,
                                                int count)
{
  if (!src || count <= 0)
    return null;
  struct oak_type_t* dst =
      OAK_ALLOC(c->allocator, sizeof(struct oak_type_t) * (usize)count);
  for (int i = 0; i < count; ++i)
    dst[i] = translate_type(c, dep, src[i]);
  return dst;
}

static void import_trait_from_dep(struct oak_compiler_t* c,
                                  const struct oak_module_t* dep,
                                  const struct oak_module_export_trait_t* exp)
{
  const struct oak_registered_trait_t* existing =
      oak_trait_find(&c->traits, exp->name);
  if (existing)
  {
    int idx = (int)(existing - c->traits.traits);
    if (idx >= c->pre_import_trait_count &&
        existing->source_module_id != dep->module_id)
      oak_compiler_error_at(c, null,
                            "import collision: '%s' is already defined",
                            exp->name);
    return;
  }
  const int exp_name_len = (int)strlen(exp->name);
  const oak_type_id_t tid =
      oak_type_registry_intern(&c->types, exp->name, exp_name_len);

  /* Insert a provisional (empty) entry so self-referential or mutually
     recursive traits short-circuit through the oak_trait_find check above
     instead of recursing infinitely via ensure_sig_types_imported.  We store
     the index (not a pointer) because ensure_sig_types_imported may trigger
     further imports that reallocate the traits array. */
  {
    struct oak_registered_trait_t provisional = { 0 };
    oak_assert(oak_dynarr_init(c->allocator, &provisional.methods, sizeof *provisional.methods));
    provisional.name = exp->name;
    provisional.name_len = exp_name_len;
    provisional.trait_id = tid;
    provisional.source_module_id = dep->module_id;
    oak_assert(oak_dynarr_push(&c->traits.traits, &provisional));
  }
  const int trait_idx = oak_dynarr_count(c->traits.traits) - 1;

  for (int mi = 0; mi < oak_dynarr_count(exp->methods); ++mi)
  {
    const struct oak_module_export_trait_method_t* src = &exp->methods[mi];
    if (ensure_sig_types_imported(c, dep, &src->return_type,
                                  src->param_types, src->arity) < 0)
      return;
  }

  for (int mi = 0; mi < oak_dynarr_count(exp->methods); ++mi)
  {
    const struct oak_module_export_trait_method_t* src = &exp->methods[mi];
    struct oak_trait_method_t tm = {
      .name = src->name,
      .name_len = (int)strlen(src->name),
      .arity = src->arity,
      .sig_decl = null,
      .decl = null,
      .self_is_mut = src->self_is_mut,
      .param_types = translate_param_types(c, dep, src->param_types, src->arity),
      .return_type = translate_type(c, dep, src->return_type),
    };
    struct oak_registered_trait_t* entry = &c->traits.traits[trait_idx];
    oak_assert(oak_dynarr_push(&entry->methods, &tm));
  }
}

static void import_enum_from_dep(struct oak_compiler_t* c,
                                 const struct oak_module_t* dep,
                                 const struct oak_module_export_enum_t* exp)
{
  const int exp_name_len = (int)strlen(exp->name);
  if (oak_is_enum_name(&c->enums, exp->name, exp_name_len))
  {
    const struct oak_registered_enum_t* re =
        oak_enum_find(&c->enums, exp->name);
    int idx = re ? (int)(re - c->enums.enums) : -1;
    if (idx >= c->pre_import_enum_count &&
        re->source_module_id != dep->module_id)
      oak_compiler_error_at(c, null,
                            "import collision: '%s' is already defined",
                            exp->name);
    return;
  }
  const oak_type_id_t enum_type_id =
      oak_type_registry_intern(&c->types, exp->name, exp_name_len);
  if (enum_type_id < 0)
  {
    oak_compiler_error_at(c, null, "failed to register imported enum '%s'",
                          exp->name);
    return;
  }
  {
    struct oak_registered_enum_t re = {
      .name = exp->name,
      .name_len = exp_name_len,
      .type_id = enum_type_id,
      .source_module_id = dep->module_id,
      .attrs = null,
      .attr_count = 0,
    };
    oak_assert(oak_dynarr_push(&c->enums.enums, &re));
  }
  for (int vi = 0; vi < oak_dynarr_count(exp->variants); ++vi)
  {
    const struct oak_module_export_enum_variant_t* v = &exp->variants[vi];
    const u16 local_idx =
        oak_compiler_intern_constant(c, OAK_VALUE_I32(v->value));
    if (c->has_error)
      return;
    struct oak_enum_variant_t ev = {
      .name = v->name,
      .name_len = (int)strlen(v->name),
      .enum_name = exp->name,
      .enum_name_len = exp_name_len,
      .const_idx = local_idx,
      .value = v->value,
      .type_id = enum_type_id,
    };
    oak_enum_registry_insert(&c->enums, &ev);
  }
}

static void import_record_from_dep(struct oak_compiler_t* c,
                                   const struct oak_module_t* dep,
                                   const struct oak_module_export_record_t* exp)
{
  const int exp_name_len = (int)strlen(exp->name);
  if (oak_records_find(&c->records, exp->name, exp_name_len))
  {
    int idx = oak_htable_get(&c->records.by_name, exp->name, exp_name_len);
    if (idx >= c->pre_import_record_count &&
        c->records.entries[idx].source_module_id != dep->module_id)
      oak_compiler_error_at(c, null,
                            "import collision: '%s' is already defined",
                            exp->name);
    return;
  }
  const oak_type_id_t tid =
      oak_type_registry_intern(&c->types, exp->name, exp_name_len);
  struct oak_registered_record_t proto = { 0 };
  proto.name = exp->name;
  proto.source_module_id = dep->module_id;
  proto.name_len = exp_name_len;
  proto.type_id = tid;
  proto.is_value = exp->is_value;
  oak_assert(oak_dynarr_init(c->allocator, &proto.fields, sizeof *proto.fields));
  oak_assert(oak_dynarr_init(c->allocator, &proto.methods, sizeof *proto.methods));

  /* Insert a provisional (empty) entry so self-referential and mutually
     recursive records short-circuit through the oak_records_find check
     above instead of recursing infinitely via ensure_dep_type_imported.
     We store the index (not a pointer) because ensure_dep_type_imported
     may trigger further imports that reallocate the entries array. */
  oak_record_registry_insert(&c->records, &proto);
  const int entry_idx =
      oak_htable_get(&c->records.by_name, exp->name, exp_name_len);

#define REC_ENTRY() (&c->records.entries[entry_idx])

  for (int fi = 0; fi < oak_dynarr_count(exp->fields); ++fi)
  {
    ensure_dep_type_imported(c, dep, exp->fields[fi].type.id);
    if (c->has_error)
      return;
    if (exp->fields[fi].type.key_id >= OAK_TYPE_FIRST_USER)
    {
      ensure_dep_type_imported(c, dep, exp->fields[fi].type.key_id);
      if (c->has_error)
        return;
    }
    struct oak_record_field_t field = {
      .name = exp->fields[fi].name,
      .name_len = (int)strlen(exp->fields[fi].name),
      .type = translate_type(c, dep, exp->fields[fi].type),
    };
    struct oak_registered_record_t* e = REC_ENTRY();
    oak_assert(oak_dynarr_push(&e->fields, &field));
  }
  for (int mi = 0; mi < oak_dynarr_count(exp->methods); ++mi)
  {
    const struct oak_module_export_record_method_t* me = &exp->methods[mi];
    if (ensure_sig_types_imported(c, dep, &me->return_type,
                                  me->param_types, me->arity) < 0)
      return;
    struct oak_registered_fn_t mfn = { 0 };
    mfn.name = me->name;
    mfn.name_len = (int)strlen(me->name);
    mfn.const_idx = 0;
    mfn.arity = me->arity;
    mfn.receiver_type_id = tid;
    mfn.return_type = translate_type(c, dep, me->return_type);
    mfn.is_static = me->is_static;
    mfn.decl = null;
    mfn.param_types = translate_param_types(c, dep, me->param_types, me->arity);
    mfn.param_mut_flags = me->param_mut_flags;
    mfn.source_module_id = dep->module_id;
    mfn.source_const_idx = me->const_idx;
    mfn.attrs = null;
    mfn.attr_count = 0;
    if (me->stub_attr_count > 0)
    {
      mfn.attrs = oak_alloc_attrs(c->allocator, me->stub_attrs,
                                   me->stub_attr_count);
      mfn.attr_count = me->stub_attr_count;
    }
    struct oak_registered_record_t* e = REC_ENTRY();
    oak_assert(oak_dynarr_push(&e->methods, &mfn));
  }
#undef REC_ENTRY
}

static void ensure_dep_type_imported(struct oak_compiler_t* c,
                                     const struct oak_module_t* dep,
                                     oak_type_id_t src_id)
{
  if (src_id < OAK_TYPE_FIRST_USER)
    return;
  const char* name = oak_type_registry_name(&dep->types, src_id);
  if (!name || name[0] == '<')
    return;
  const struct oak_module_export_record_t* rec =
      oak_module_find_export_record(dep, name);
  if (rec)
  {
    import_record_from_dep(c, dep, rec);
    return;
  }
  const struct oak_module_export_enum_t* enm =
      oak_module_find_export_enum(dep, name);
  if (enm)
  {
    import_enum_from_dep(c, dep, enm);
    return;
  }
  const struct oak_module_export_trait_t* trt =
      oak_module_find_export_trait(dep, name);
  if (trt)
  {
    import_trait_from_dep(c, dep, trt);
    return;
  }

  /* The type is not exported by the immediate dependency — it may have been
     imported transitively. Search the dep's own dependencies for the type
     so we import the correct record/enum layout rather than silently
     interning a bare name that could collide with an unrelated local type. */
  if (!c->module_registry)
    return;
  for (int di = 0; di < oak_dynarr_count(dep->import_modules); ++di)
  {
    const struct oak_module_t* transitive =
        oak_module_registry_get(c->module_registry,
                                dep->import_modules[di]);
    if (!transitive)
      continue;
    rec = oak_module_find_export_record(transitive, name);
    if (rec)
    {
      import_record_from_dep(c, transitive, rec);
      return;
    }
    enm = oak_module_find_export_enum(transitive, name);
    if (enm)
    {
      import_enum_from_dep(c, transitive, enm);
      return;
    }
    trt = oak_module_find_export_trait(transitive, name);
    if (trt)
    {
      import_trait_from_dep(c, transitive, trt);
      return;
    }
  }
}

/* Import both the .id and .key_id of a type (handles map key types). */
static int ensure_full_type_imported(struct oak_compiler_t* c,
                                     const struct oak_module_t* dep,
                                     const struct oak_type_t* type)
{
  ensure_dep_type_imported(c, dep, type->id);
  if (c->has_error)
    return -1;
  if (type->key_id >= OAK_TYPE_FIRST_USER)
  {
    ensure_dep_type_imported(c, dep, type->key_id);
    if (c->has_error)
      return -1;
  }
  return 0;
}

/* Import all types referenced by a function/method signature. */
static int ensure_sig_types_imported(struct oak_compiler_t* c,
                                     const struct oak_module_t* dep,
                                     const struct oak_type_t* return_type,
                                     const struct oak_type_t* param_types,
                                     int arity)
{
  if (return_type && ensure_full_type_imported(c, dep, return_type) < 0)
    return -1;
  for (int i = 0; i < arity && param_types; ++i)
    if (ensure_full_type_imported(c, dep, &param_types[i]) < 0)
      return -1;
  return 0;
}

static void import_fn_from_dep(struct oak_compiler_t* c,
                               const struct oak_module_t* dep,
                               const struct oak_module_export_fn_t* exp,
                               const char* local_name,
                               int local_name_len)
{
  if (oak_fn_registry_find(&c->fns, local_name, local_name_len))
  {
    oak_compiler_error_at(c, null,
                          "import collision: '%.*s' is already defined",
                          (int)local_name_len, local_name);
    return;
  }
  if (ensure_sig_types_imported(c, dep, &exp->return_type,
                                exp->param_types, exp->arity) < 0)
    return;
  struct oak_registered_fn_t entry = {
    .name = local_name,
    .name_len = local_name_len,
    .const_idx = 0,
    .arity = exp->arity,
    .receiver_type_id = OAK_TYPE_VOID,
    .return_type = translate_type(c, dep, exp->return_type),
    .decl = null,
    .param_types = translate_param_types(c, dep, exp->param_types, exp->arity),
    .param_mut_flags = exp->param_mut_flags,
    .source_module_id = dep->module_id,
    .source_const_idx = exp->const_idx,
  };
  oak_fn_registry_insert(&c->fns, &entry);
}

/* Resolve the dependency module for the Nth import AST node by indexing into
 * the pre-populated import_modules array. */
static const struct oak_module_t*
resolve_dep_for_import(struct oak_compiler_t* c, int import_idx)
{
  if (!c->module_registry || !c->current_module)
    return null;
  if (import_idx < 0 ||
      import_idx >= oak_dynarr_count(c->current_module->import_modules))
    return null;
  return oak_module_registry_get(
      c->module_registry,
      c->current_module->import_modules[import_idx]);
}

static void import_all_from_dep(struct oak_compiler_t* c,
                                const struct oak_module_t* dep)
{
  for (int i = 0; i < oak_dynarr_count(dep->exports_record.items); ++i)
  {
    import_record_from_dep(c, dep, &dep->exports_record.items[i]);
    if (c->has_error)
      return;
  }
  for (int i = 0; i < oak_dynarr_count(dep->exports_enum.items); ++i)
  {
    import_enum_from_dep(c, dep, &dep->exports_enum.items[i]);
    if (c->has_error)
      return;
  }
  for (int i = 0; i < oak_dynarr_count(dep->exports_trait.items); ++i)
  {
    import_trait_from_dep(c, dep, &dep->exports_trait.items[i]);
    if (c->has_error)
      return;
  }
  for (int i = 0; i < oak_dynarr_count(dep->exports_fn.items); ++i)
  {
    const struct oak_module_export_fn_t* exp = &dep->exports_fn.items[i];
    import_fn_from_dep(c, dep, exp, exp->name, (int)strlen(exp->name));
    if (c->has_error)
      return;
  }
}

static void import_selective_from_dep(struct oak_compiler_t* c,
                                      const struct oak_module_t* dep,
                                      const struct oak_ast_node_t* names_node)
{
  if (!names_node)
    return;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &names_node->children)
  {
    const struct oak_ast_node_t* name_node =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (name_node->kind != OAK_NODE_IMPORT_NAME)
      continue;

    const struct oak_ast_node_t* orig = name_node->lhs;
    const struct oak_ast_node_t* alias_node = name_node->rhs;
    const struct oak_ast_node_t* alias =
        alias_node ? alias_node->child : null;
    if (!orig)
      continue;
    const char* orig_name = oak_token_text(orig->token);
    const int orig_len = oak_token_size(orig->token);
    const char* local_name = alias ? oak_token_text(alias->token) : orig_name;
    const int local_len = alias ? oak_token_size(alias->token) : orig_len;

    int found = 0;
    const struct oak_module_export_fn_t* fn_exp =
        oak_module_find_export_fn(dep, orig_name);
    if (fn_exp)
    {
      import_fn_from_dep(c, dep, fn_exp, local_name, local_len);
      found = 1;
    }
    if (!found)
    {
      const struct oak_module_export_record_t* rec_exp =
          oak_module_find_export_record(dep, orig_name);
      if (rec_exp)
      {
        if (alias)
        {
          oak_compiler_error_at(c, alias->token,
                                "'as' aliases are not supported for record imports");
          return;
        }
        import_record_from_dep(c, dep, rec_exp);
        found = 1;
      }
    }
    if (!found)
    {
      const struct oak_module_export_enum_t* enum_exp =
          oak_module_find_export_enum(dep, orig_name);
      if (enum_exp)
      {
        if (alias)
        {
          oak_compiler_error_at(c, alias->token,
                                "'as' aliases are not supported for enum imports");
          return;
        }
        import_enum_from_dep(c, dep, enum_exp);
        found = 1;
      }
    }
    if (!found)
    {
      const struct oak_module_export_trait_t* trait_exp =
          oak_module_find_export_trait(dep, orig_name);
      if (trait_exp)
      {
        if (alias)
        {
          oak_compiler_error_at(c, alias->token,
                                "'as' aliases are not supported for trait imports");
          return;
        }
        import_trait_from_dep(c, dep, trait_exp);
        found = 1;
      }
    }
    if (!found)
    {
      oak_compiler_error_at(c, orig->token,
                            "module '%s' does not export '%.*s'",
                            dep->dotted_name,
                            (int)orig_len, orig_name);
      return;
    }
    if (c->has_error)
      return;
  }
}

/* Walk the program's import AST nodes and register imported symbols.
 * Must run BEFORE oak_register_program_enums/records/fns. */
void oak_resolve_new_style_imports(struct oak_compiler_t* c,
                               const struct oak_ast_node_t* program)
{
  if (!c->current_module || !c->module_registry || !program)
    return;

  c->pre_import_record_count = oak_dynarr_count(c->records.entries);
  c->pre_import_enum_count = oak_dynarr_count(c->enums.enums);
  c->pre_import_trait_count = oak_dynarr_count(c->traits.traits);

  int import_idx = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind != OAK_NODE_IMPORT_SELECTIVE &&
        item->kind != OAK_NODE_IMPORT_WILDCARD)
      continue;

    const struct oak_module_t* dep = resolve_dep_for_import(c, import_idx);
    import_idx++;
    if (!dep)
      continue;

    if (item->kind == OAK_NODE_IMPORT_WILDCARD)
      import_all_from_dep(c, dep);
    else
      import_selective_from_dep(c, dep, item->lhs);
    if (c->has_error)
      return;
  }
}

/* Lower a parameter list from an AST declaration into param_types/param_mut_flags
 * arrays.  Used by oak_populate_module_exports for both fns and methods. */
static void lower_params_from_decl(struct oak_compiler_t* c,
                                   const struct oak_ast_node_t* decl,
                                   int arity,
                                   int is_static,
                                   struct oak_type_t** out_types,
                                   u8** out_muts)
{
  if (arity <= 0)
  {
    *out_types = null;
    *out_muts = null;
    return;
  }
  const int has_self = !is_static;
  struct oak_type_t* ptypes =
      OAK_ALLOC(c->allocator, sizeof(struct oak_type_t) * (usize)arity);
  u8* pmuts = OAK_ALLOC(c->allocator, sizeof(u8) * (usize)arity);
  for (int i = 0; i < arity; ++i)
  {
    oak_type_clear(&ptypes[i]);
    pmuts[i] = 0;
  }
  if (has_self)
  {
    const struct oak_ast_node_t* self_p = oak_fn_self_param(decl);
    if (self_p && oak_self_is_mut(self_p))
      pmuts[0] = 1;
  }
  for (int pi = 0; pi < arity - has_self; ++pi)
  {
    const int slot = pi + has_self;
    const struct oak_ast_node_t* param = oak_fn_param_at(decl, pi);
    if (!param)
      continue;
    pmuts[slot] = oak_param_is_mut(param) ? 1 : 0;
    const struct oak_ast_node_t* tn = oak_fn_param_type_node(param);
    if (tn)
      oak_lower_type_node(c, tn, &ptypes[slot]);
  }
  *out_types = ptypes;
  *out_muts = pmuts;
}

/* Lower a return-type AST node into an oak_type_t. */
static struct oak_type_t lower_return_type(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* decl)
{
  struct oak_type_t rt;
  oak_type_clear(&rt);
  if (!decl)
    return rt;
  const struct oak_ast_node_t* rtn = oak_fn_return_type_node(decl);
  if (rtn)
    oak_lower_type_node(c, rtn, &rt);
  return rt;
}

/* Export a single free function (no receiver) into the module's exports_fn. */
static void export_free_fn(struct oak_compiler_t* c,
                           struct oak_module_t* mod,
                           const struct oak_registered_fn_t* e)
{
  struct oak_type_t* ptypes = null;
  u8* pmuts = null;
  lower_params_from_decl(c, e->decl, e->arity, 1, &ptypes, &pmuts);
  struct oak_module_export_fn_t exp = {
    .name = e->name,
    .const_idx = e->const_idx,
    .arity = e->arity,
    .param_types = ptypes,
    .param_mut_flags = pmuts,
    .return_type = lower_return_type(c, e->decl),
    .stub_attrs = oak_alloc_attrs(c->allocator, e->attrs, e->attr_count),
    .stub_attr_count = e->attr_count,
  };
  const int idx = oak_dynarr_count(mod->exports_fn.items);
  oak_assert(oak_dynarr_push(&mod->exports_fn.items, &exp));
  oak_htable_insert(&mod->exports_fn.by_name, e->name, (int)strlen(e->name), idx);
}

static void export_user_fns(struct oak_compiler_t* c, struct oak_module_t* mod)
{
  for (int i = 0; i < oak_dynarr_count(c->fns.entries); ++i)
  {
    const struct oak_registered_fn_t* e = &c->fns.entries[i];
    if (!e->decl || e->receiver_type_id != OAK_TYPE_VOID)
      continue;
    export_free_fn(c, mod, e);
  }
}

/* Build the methods array of an exported record from its registered methods. */
static void export_record_methods(struct oak_compiler_t* c,
                                  struct oak_module_export_record_t* exp,
                                  const struct oak_registered_record_t* r)
{
  oak_assert(oak_dynarr_init(c->allocator, &exp->methods, sizeof *exp->methods));
  for (int mi = 0; mi < oak_dynarr_count(r->methods); ++mi)
  {
    const struct oak_registered_fn_t* m = &r->methods[mi];
    struct oak_module_export_record_method_t mexp = { 0 };
    mexp.name = m->name;
    mexp.const_idx = m->const_idx;
    mexp.arity = m->arity;
    mexp.is_static = m->is_static;
    if (m->decl)
      mexp.return_type = lower_return_type(c, m->decl);
    else
      mexp.return_type = m->return_type;
    mexp.stub_attrs =
        oak_alloc_attrs(c->allocator, m->attrs, m->attr_count);
    mexp.stub_attr_count = m->attr_count;
    if (m->arity > 0 && m->decl)
      lower_params_from_decl(c, m->decl, m->arity, m->is_static,
                             &mexp.param_types, &mexp.param_mut_flags);
    oak_assert(oak_dynarr_push(&exp->methods, &mexp));
  }
}

static void export_user_records(struct oak_compiler_t* c,
                                struct oak_module_t* mod)
{
  for (int i = c->user_record_start; i < oak_dynarr_count(c->records.entries); ++i)
  {
    const struct oak_registered_record_t* r = &c->records.entries[i];
    struct oak_module_export_record_t exp = { 0 };
    exp.name = r->name;
    oak_assert(oak_dynarr_init(c->allocator, &exp.fields, sizeof *exp.fields));
    for (int fi = 0; fi < oak_dynarr_count(r->fields); ++fi)
    {
      struct oak_module_export_record_field_t field = {
        .name = r->fields[fi].name,
        .type = r->fields[fi].type,
      };
      oak_assert(oak_dynarr_push(&exp.fields, &field));
    }
    export_record_methods(c, &exp, r);
    exp.layout_id = 0;
    const int idx = oak_dynarr_count(mod->exports_record.items);
    oak_assert(oak_dynarr_push(&mod->exports_record.items, &exp));
    oak_htable_insert(
        &mod->exports_record.by_name, exp.name, (int)strlen(exp.name), idx);
  }
}

/* Group enum variants under their containing enum, creating the
 * oak_module_export_enum_t entry lazily on first variant. */
static void export_user_enums(struct oak_compiler_t* c,
                              struct oak_module_t* mod)
{
  if (c->user_enum_start < 0)
    return;
  for (int i = c->user_enum_start; i < oak_dynarr_count(c->enums.variants); ++i)
  {
    const struct oak_enum_variant_t* v = &c->enums.variants[i];
    int eidx = oak_htable_get(
        &mod->exports_enum.by_name, v->enum_name, v->enum_name_len);
    if (eidx < 0)
    {
      struct oak_module_export_enum_t ee = { 0 };
      oak_assert(oak_dynarr_init(c->allocator, &ee.variants, sizeof *ee.variants));
      ee.name = v->enum_name;
      eidx = oak_dynarr_count(mod->exports_enum.items);
      oak_assert(oak_dynarr_push(&mod->exports_enum.items, &ee));
      oak_htable_insert(
          &mod->exports_enum.by_name, ee.name, (int)strlen(ee.name), eidx);
    }
    struct oak_module_export_enum_t* ee = &mod->exports_enum.items[eidx];
    struct oak_module_export_enum_variant_t variant = {
      .name = v->name,
      .value = v->value,
    };
    oak_assert(oak_dynarr_push(&ee->variants, &variant));
  }
}

static void export_user_traits(struct oak_compiler_t* c,
                               struct oak_module_t* mod)
{
  for (int i = c->user_trait_start; i < oak_dynarr_count(c->traits.traits); ++i)
  {
    const struct oak_registered_trait_t* tr = &c->traits.traits[i];
    struct oak_module_export_trait_t exp = { 0 };
    exp.name = tr->name;
    oak_assert(oak_dynarr_init(c->allocator, &exp.methods, sizeof *exp.methods));
    for (int mi = 0; mi < oak_dynarr_count(tr->methods); ++mi)
    {
      const struct oak_trait_method_t* src = &tr->methods[mi];
      struct oak_type_t* ptypes = null;
      u8* pmuts = null;
      if (src->sig_decl)
        lower_params_from_decl(c, src->sig_decl, src->arity, 0, &ptypes, &pmuts);
      if (pmuts)
        OAK_FREE(c->allocator, pmuts);
      struct oak_module_export_trait_method_t tm = {
        .name = src->name,
        .arity = src->arity,
        .self_is_mut = src->self_is_mut,
        .param_types = ptypes,
        .return_type = src->sig_decl ? lower_return_type(c, src->sig_decl)
                                     : src->return_type,
      };
      oak_assert(oak_dynarr_push(&exp.methods, &tm));
    }
    const int tidx = oak_dynarr_count(mod->exports_trait.items);
    oak_assert(oak_dynarr_push(&mod->exports_trait.items, &exp));
    oak_htable_insert(
        &mod->exports_trait.by_name, exp.name, (int)strlen(exp.name), tidx);
  }
}

/* Populate the current module's export tables from the now-fully-populated
 * compiler registries.  Type IDs stored in exports reference this module's
 * type registry (moved to the module after compilation). */
void oak_populate_module_exports(struct oak_compiler_t* c)
{
  if (!c->current_module)
    return;
  struct oak_module_t* mod = c->current_module;
  export_user_fns(c, mod);
  export_user_records(c, mod);
  export_user_enums(c, mod);
  export_user_traits(c, mod);
}
