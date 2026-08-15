#include "internal/oak_compiler.h"
#include "internal/oak_interface_registry.h"

#include <string.h>

static void ensure_dep_type_imported(oak_compiler_t* c,
                                     const oak_module_t* dep,
                                     oak_type_id_t src_id);
static void ensure_dep_named_type_imported(oak_compiler_t* c,
                                           const oak_module_t* dep,
                                           const char* name);
static int ensure_full_type_imported(oak_compiler_t* c,
                                     const oak_module_t* dep,
                                     const oak_type_t* type);
static int ensure_sig_types_imported(oak_compiler_t* c,
                                     const oak_module_t* dep,
                                     const oak_type_t* return_type,
                                     const oak_type_t* param_types,
                                     int arity);

/* Catalog names for a dependency type while preserving its qualified ID. */
static oak_type_t import_type_ref(oak_compiler_t* c,
                                         const oak_module_t* dep,
                                         oak_type_t src)
{
  oak_type_t dst = src;
  if (src.id >= OAK_TYPE_FIRST_USER)
  {
    const char* name = oak_type_registry_name(&dep->types, src.id);
    oak_type_registry_intern_with_id(&c->types, name, src.id);
  }
  if (src.key_id >= OAK_TYPE_FIRST_USER)
  {
    const char* name = oak_type_registry_name(&dep->types, src.key_id);
    oak_type_registry_intern_with_id(&c->types, name, src.key_id);
  }
  return dst;
}

/* Allocate and translate a param_types array from a dependency module. */
static oak_type_t* translate_param_types(oak_compiler_t* c,
                                                const oak_module_t* dep,
                                                const oak_type_t* src,
                                                int count)
{
  if (!src || count <= 0)
    return null;
  oak_type_t* dst =
      oak_alloc(c->allocator, sizeof(oak_type_t) * (usize)count, OAK_HERE);
  for (int i = 0; i < count; ++i)
    dst[i] = import_type_ref(c, dep, src[i]);
  return dst;
}

static u8* copy_mut_flags(oak_compiler_t* c, const u8* src, int count)
{
  if (!src || count <= 0)
    return null;
  u8* dst = oak_alloc(c->allocator, sizeof(u8) * (usize)count, OAK_HERE);
  memcpy(dst, src, sizeof(u8) * (usize)count);
  return dst;
}

static void import_interface_from_dep(oak_compiler_t* c,
                                  const oak_module_t* dep,
                                  const oak_module_export_interface_t* exp)
{
  const oak_registered_interface_t* existing =
      oak_interface_find(&c->interfaces, exp->name);
  if (existing)
  {
    if (existing->source_module_id != dep->module_id)
      oak_compiler_error_at(c, null,
                            "import collision: '%s' is already defined",
                            exp->name);
    return;
  }
  const oak_type_id_t tid = oak_type_registry_lookup(&dep->types, exp->name);
  oak_type_registry_intern_with_id(&c->types, exp->name, tid);

  if (!oak_compiler_declare_symbol(
          c, null, exp->name, OAK_SYMBOL_INTERFACE,
          (int)oak_size(c->interfaces.interfaces), dep->module_id, 1))
    return;

  /* Insert a provisional (empty) entry so self-referential or mutually
     recursive interfaces short-circuit through the oak_interface_find check above
     instead of recursing infinitely via ensure_sig_types_imported.  We store
     the index (not a pointer) because ensure_sig_types_imported may trigger
     further imports that reallocate the interfaces array. */
  {
    oak_registered_interface_t provisional = { 0 };
    provisional.methods =
        oak_vector_new(c->allocator, sizeof(oak_interface_method_t));
    oak_assert(provisional.methods);
    provisional.name = exp->name;
    provisional.interface_id = tid;
    provisional.source_module_id = dep->module_id;
    oak_assert(oak_push_back(c->interfaces.interfaces, &provisional));
  }
  const usize interface_idx = oak_size(c->interfaces.interfaces) - 1;

  const oak_module_export_interface_method_t* exp_methods =
      OAK_CDATA(oak_module_export_interface_method_t, exp->methods);
  for (usize mi = 0; mi < oak_size(exp->methods); ++mi)
  {
    const oak_module_export_interface_method_t* src = &exp_methods[mi];
    if (ensure_sig_types_imported(c, dep, &src->return_type,
                                  src->param_types, src->arity) < 0)
      return;
  }

  for (usize mi = 0; mi < oak_size(exp->methods); ++mi)
  {
    const oak_module_export_interface_method_t* src = &exp_methods[mi];
    oak_interface_method_t tm = {
      .name = src->name,
      .arity = src->arity,
      .sig_decl = null,
      .decl = null,
      .self_is_mut = src->self_is_mut,
      .param_types = translate_param_types(c, dep, src->param_types, src->arity),
      .return_type = import_type_ref(c, dep, src->return_type),
    };
    oak_registered_interface_t* entry =
        oak_get(c->interfaces.interfaces, interface_idx);
    oak_assert(oak_push_back(entry->methods, &tm));
  }
}

static void import_enum_from_dep(oak_compiler_t* c,
                                 const oak_module_t* dep,
                                 const oak_module_export_enum_t* exp)
{
  if (oak_is_enum_name(&c->enums, exp->name))
  {
    const oak_registered_enum_t* re =
        oak_enum_find(&c->enums, exp->name);
    if (!re || (re->source_module_id != OAK_MODULE_ID_NONE &&
                re->source_module_id != dep->module_id))
      oak_compiler_error_at(c, null,
                            "import collision: '%s' is already defined",
                            exp->name);
    return;
  }
  const oak_type_id_t enum_type_id =
      oak_type_registry_lookup(&dep->types, exp->name);
  if (enum_type_id < 0)
  {
    oak_compiler_error_at(c, null, "failed to register imported enum '%s'",
                          exp->name);
    return;
  }
  oak_type_registry_intern_with_id(&c->types, exp->name, enum_type_id);
  {
    oak_registered_enum_t re = {
      .name = exp->name,
      .type_id = enum_type_id,
      .source_module_id = dep->module_id,
      .attrs = null,
      .attr_count = 0,
    };
    if (!oak_compiler_declare_symbol(
            c, null, exp->name, OAK_SYMBOL_ENUM,
            (int)oak_size(c->enums.enums), dep->module_id, 1))
      return;
    oak_assert(oak_push_back(c->enums.enums, &re));
  }
  const oak_module_export_enum_variant_t* exp_variants =
      OAK_CDATA(oak_module_export_enum_variant_t, exp->variants);
  for (usize vi = 0; vi < oak_size(exp->variants); ++vi)
  {
    const oak_module_export_enum_variant_t* v = &exp_variants[vi];
    const u16 local_idx =
        oak_compiler_intern_constant(c, OAK_VALUE_I32(v->value));
    if (c->has_error)
      return;
    oak_enum_variant_t ev = {
      .name = v->name,
      .enum_name = exp->name,
      .const_idx = local_idx,
      .value = v->value,
      .type_id = enum_type_id,
    };
    oak_enum_registry_insert(&c->enums, &ev);
  }
}

static void import_record_from_dep(oak_compiler_t* c,
                                   const oak_module_t* dep,
                                   const oak_module_export_record_t* exp)
{
  if (oak_records_find(&c->records, exp->name))
  {
    const oak_registered_record_t* existing =
        oak_records_find(&c->records, exp->name);
    if (existing->source_module_id != OAK_MODULE_ID_NONE &&
        existing->source_module_id != dep->module_id)
      oak_compiler_error_at(c, null,
                            "import collision: '%s' is already defined",
                            exp->name);
    return;
  }
  const oak_type_id_t tid = oak_type_registry_lookup(&dep->types, exp->name);
  oak_type_registry_intern_with_id(&c->types, exp->name, tid);
  oak_registered_record_t proto = { 0 };
  proto.name = exp->name;
  proto.source_module_id = dep->module_id;
  proto.type_id = tid;
  proto.is_value = exp->is_value;
  proto.fields =
      oak_vector_new(c->allocator, sizeof(oak_record_field_t));
  proto.methods =
      oak_vector_new(c->allocator, sizeof(oak_registered_fn_t));
  proto.interface_names =
      oak_vector_new(c->allocator, sizeof(const char*));
  proto.interfaces = oak_vector_new(c->allocator, sizeof(oak_type_t));
  oak_assert(proto.fields && proto.methods && proto.interface_names &&
             proto.interfaces);

  /* The record names its interfaces, so they have to come across with it —
     otherwise conformance resolves against an interface registry that has
     never heard of them and `import { Circle } from 'shapes'` is rejected for
     naming an unknown interface. Same reasoning as the field and signature
     types below. */
  const char* const* exp_interface_names =
      (const char* const*)oak_cdata(exp->interface_names);
  for (usize ii = 0; ii < oak_size(exp->interface_names); ++ii)
  {
    ensure_dep_named_type_imported(c, dep, exp_interface_names[ii]);
    if (c->has_error)
      return;
    oak_assert(oak_push_back(proto.interface_names, &exp_interface_names[ii]));
  }

  /* Insert a provisional (empty) entry so self-referential and mutually
     recursive records short-circuit through the oak_records_find check
     above instead of recursing infinitely via ensure_dep_type_imported.
     We store the index (not a pointer) because ensure_dep_type_imported
     may trigger further imports that reallocate the entries array. */
  if (!oak_compiler_declare_symbol(
          c, null, exp->name, OAK_SYMBOL_RECORD,
          (int)oak_size(c->records.entries), dep->module_id, 1))
    return;
  oak_record_registry_insert(&c->records, &proto);
  const usize entry_idx = *(const usize*)oak_cfind_str(c->records.by_name,
                                                       exp->name);

#define REC_ENTRY() ((oak_registered_record_t*)oak_get(               \
    c->records.entries, entry_idx))

  const oak_module_export_record_field_t* exp_fields =
      OAK_CDATA(oak_module_export_record_field_t, exp->fields);
  for (usize fi = 0; fi < oak_size(exp->fields); ++fi)
  {
    ensure_dep_type_imported(c, dep, exp_fields[fi].type.id);
    if (c->has_error)
      return;
    if (exp_fields[fi].type.key_id >= OAK_TYPE_FIRST_USER)
    {
      ensure_dep_type_imported(c, dep, exp_fields[fi].type.key_id);
      if (c->has_error)
        return;
    }
    oak_record_field_t field = {
      .name = exp_fields[fi].name,
      .type = import_type_ref(c, dep, exp_fields[fi].type),
    };
    oak_registered_record_t* e = REC_ENTRY();
    oak_assert(oak_push_back(e->fields, &field));
  }
  const oak_module_export_record_method_t* exp_methods =
      OAK_CDATA(oak_module_export_record_method_t, exp->methods);
  for (usize mi = 0; mi < oak_size(exp->methods); ++mi)
  {
    const oak_module_export_record_method_t* me = &exp_methods[mi];
    if (ensure_sig_types_imported(c, dep, &me->return_type,
                                  me->param_types, me->arity) < 0)
      return;
    oak_registered_fn_t mfn = { 0 };
    mfn.name = me->name;
    mfn.const_idx = 0;
    mfn.arity = me->arity;
    mfn.receiver_type_id = tid;
    mfn.return_type = import_type_ref(c, dep, me->return_type);
    mfn.is_static = me->is_static;
    mfn.decl = null;
    mfn.param_types = translate_param_types(c, dep, me->param_types, me->arity);
    mfn.param_mut_flags = copy_mut_flags(c, me->param_mut_flags, me->arity);
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
    oak_registered_record_t* e = REC_ENTRY();
    oak_assert(oak_push_back(e->methods, &mfn));
  }
#undef REC_ENTRY
}

static int import_named_type_from_dep(oak_compiler_t* c,
                                      const oak_module_t* dep,
                                      const char* name,
                                      const oak_token_t* alias_token)
{
  const oak_module_export_record_t* rec =
      oak_module_find_export_record(dep, name);
  if (rec)
  {
    if (alias_token)
      oak_compiler_error_at(
          c, alias_token, "'as' aliases are not supported for record imports");
    else
      import_record_from_dep(c, dep, rec);
    return 1;
  }

  const oak_module_export_enum_t* enm =
      oak_module_find_export_enum(dep, name);
  if (enm)
  {
    if (alias_token)
      oak_compiler_error_at(
          c, alias_token, "'as' aliases are not supported for enum imports");
    else
      import_enum_from_dep(c, dep, enm);
    return 1;
  }

  const oak_module_export_interface_t* trt =
      oak_module_find_export_interface(dep, name);
  if (trt)
  {
    if (alias_token)
      oak_compiler_error_at(
          c, alias_token, "'as' aliases are not supported for interface imports");
    else
      import_interface_from_dep(c, dep, trt);
    return 1;
  }

  return 0;
}

static void ensure_dep_named_type_imported(oak_compiler_t* c,
                                           const oak_module_t* dep,
                                           const char* name)
{
  if (!name || name[0] == '<')
    return;
  if (import_named_type_from_dep(c, dep, name, null))
    return;

  /* The type is not exported by the immediate dependency — it may have been
     imported transitively. Search the dep's own dependencies for the type
     so we import the correct record/enum layout rather than silently
     interning a bare name that could collide with an unrelated local type. */
  if (!c->module_registry)
    return;
  const u16* dep_imports = OAK_CDATA(u16, dep->import_modules);
  for (usize di = 0; di < oak_size(dep->import_modules); ++di)
  {
    const oak_module_t* transitive =
        oak_module_registry_get(c->module_registry, dep_imports[di]);
    if (!transitive)
      continue;
    if (import_named_type_from_dep(c, transitive, name, null))
      return;
  }
}

static void ensure_dep_type_imported(oak_compiler_t* c,
                                     const oak_module_t* dep,
                                     oak_type_id_t src_id)
{
  if (src_id < OAK_TYPE_FIRST_USER)
    return;
  ensure_dep_named_type_imported(
      c, dep, oak_type_registry_name(&dep->types, src_id));
}

/* Import both the .id and .key_id of a type (handles map key types). */
static int ensure_full_type_imported(oak_compiler_t* c,
                                     const oak_module_t* dep,
                                     const oak_type_t* type)
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
static int ensure_sig_types_imported(oak_compiler_t* c,
                                     const oak_module_t* dep,
                                     const oak_type_t* return_type,
                                     const oak_type_t* param_types,
                                     int arity)
{
  if (return_type && ensure_full_type_imported(c, dep, return_type) < 0)
    return -1;
  for (int i = 0; i < arity && param_types; ++i)
    if (ensure_full_type_imported(c, dep, &param_types[i]) < 0)
      return -1;
  return 0;
}

static void import_fn_from_dep(oak_compiler_t* c,
                               const oak_module_t* dep,
                               const oak_module_export_fn_t* exp,
                               const char* local_name)
{
  if (oak_fn_registry_find(&c->fns, local_name))
  {
    oak_compiler_error_at(
        c, null, "import collision: '%s' is already defined", local_name);
    return;
  }
  if (ensure_sig_types_imported(c, dep, &exp->return_type,
                                exp->param_types, exp->arity) < 0)
    return;
  oak_registered_fn_t entry = {
    .name = local_name,
    .const_idx = 0,
    .arity = exp->arity,
    .receiver_type_id = OAK_TYPE_VOID,
    .return_type = import_type_ref(c, dep, exp->return_type),
    .decl = null,
    .param_types = translate_param_types(c, dep, exp->param_types, exp->arity),
    .param_mut_flags = copy_mut_flags(c, exp->param_mut_flags, exp->arity),
    .source_module_id = dep->module_id,
    .source_const_idx = exp->const_idx,
  };
  if (!oak_compiler_declare_symbol(
          c, null, local_name, OAK_SYMBOL_FUNCTION,
          (int)oak_size(c->fns.entries), dep->module_id, 1))
    return;
  oak_fn_registry_insert(&c->fns, &entry);
}

/* Resolve the dependency module for the Nth import AST node by indexing into
 * the pre-populated import_modules array. */
static const oak_module_t*
resolve_dep_for_import(oak_compiler_t* c, int import_idx)
{
  if (!c->module_registry || !c->current_module)
    return null;
  const u16* imports =
      OAK_CDATA(u16, c->current_module->import_modules);
  if (import_idx < 0 ||
      (usize)import_idx >= oak_size(c->current_module->import_modules))
    return null;
  return oak_module_registry_get(c->module_registry, imports[import_idx]);
}

static void import_all_from_dep(oak_compiler_t* c,
                                const oak_module_t* dep)
{
  const oak_symbol_t* symbols =
      OAK_CDATA(oak_symbol_t, dep->exports.symbols);
  for (usize i = 0; i < oak_size(dep->exports.symbols); ++i)
  {
    const oak_symbol_t* symbol = &symbols[i];
    const usize payload = (usize)symbol->payload_index;
    switch (symbol->kind)
    {
    case OAK_SYMBOL_RECORD:
      import_record_from_dep(c, dep, oak_cget(dep->exports.records, payload));
      break;
    case OAK_SYMBOL_ENUM:
      import_enum_from_dep(c, dep, oak_cget(dep->exports.enums, payload));
      break;
    case OAK_SYMBOL_INTERFACE:
      import_interface_from_dep(
          c, dep, oak_cget(dep->exports.interfaces, payload));
      break;
    case OAK_SYMBOL_FUNCTION:
    {
      const oak_module_export_fn_t* exp =
          oak_cget(dep->exports.fns, payload);
      import_fn_from_dep(c, dep, exp, exp->name);
      break;
    }
    default:
      break;
    }
    if (c->has_error)
      return;
  }
}

static void import_selective_from_dep(oak_compiler_t* c,
                                      const oak_module_t* dep,
                                      const oak_ast_node_t* names_node)
{
  if (!names_node)
    return;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &names_node->children)
  {
    const oak_ast_node_t* name_node =
        oak_container_of(pos, oak_ast_node_t, link);
    if (name_node->kind != OAK_NODE_IMPORT_NAME)
      continue;

    const oak_ast_node_t* orig = name_node->lhs;
    const oak_ast_node_t* alias_node = name_node->rhs;
    const oak_ast_node_t* alias =
        alias_node ? alias_node->child : null;
    if (!orig)
      continue;
    const char* orig_name = oak_token_text(orig->token);
    const int orig_len = oak_token_size(orig->token);
    const char* local_name = alias ? oak_token_text(alias->token) : orig_name;

    const oak_module_export_fn_t* fn_exp =
        oak_module_find_export_fn(dep, orig_name);
    if (fn_exp)
    {
      import_fn_from_dep(c, dep, fn_exp, local_name);
      if (c->has_error)
        return;
      continue;
    }
    if (import_named_type_from_dep(
            c, dep, orig_name, alias ? alias->token : null))
    {
      if (c->has_error)
        return;
      continue;
    }
    oak_compiler_error_at(c, orig->token,
                          "module '%s' does not export '%.*s'",
                          dep->dotted_name,
                          (int)orig_len, orig_name);
    return;
  }
}

/* Walk the program's import AST nodes and register imported symbols.
 * Must run BEFORE oak_register_program_enums/records/fns. */
void oak_resolve_new_style_imports(oak_compiler_t* c,
                               const oak_ast_node_t* program)
{
  if (!c->current_module || !c->module_registry || !program)
    return;

  /* Alias → module id. Iterating the map replaces the old walk over the hash
   * table's raw slot array, which had no iteration API of its own. */
  for (oak_iterator_t it = oak_begin(c->current_module->imports);
       oak_iter_get(&it);
       oak_next(&it))
  {
    const char* alias = oak_iter_key(&it, null);
    const usize module_id = *(const usize*)oak_iter_get(&it);
    if (!oak_compiler_declare_symbol(c,
                                     null,
                                     alias,
                                     OAK_SYMBOL_MODULE_ALIAS,
                                     (int)module_id,
                                     (u16)module_id,
                                     1))
      return;
  }

  int import_idx = 0;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const oak_ast_node_t* item =
        oak_container_of(pos, oak_ast_node_t, link);
    if (item->kind != OAK_NODE_IMPORT_SELECTIVE &&
        item->kind != OAK_NODE_IMPORT_WILDCARD &&
        item->kind != OAK_NODE_IMPORT_DECL)
      continue;

    const oak_module_t* dep = resolve_dep_for_import(c, import_idx);
    import_idx++;
    if (!dep)
      continue;

    if (item->kind == OAK_NODE_IMPORT_DECL)
      continue;
    else if (item->kind == OAK_NODE_IMPORT_WILDCARD)
      import_all_from_dep(c, dep);
    else
      import_selective_from_dep(c, dep, item->lhs);
    if (c->has_error)
      return;
  }
}

/* Lower a parameter list from an AST declaration into param_types/param_mut_flags
 * arrays.  Used by oak_populate_module_exports for both fns and methods. */
static void lower_params_from_decl(oak_compiler_t* c,
                                   const oak_ast_node_t* decl,
                                   int arity,
                                   int is_static,
                                   oak_type_t** out_types,
                                   u8** out_muts)
{
  if (arity <= 0)
  {
    *out_types = null;
    *out_muts = null;
    return;
  }
  const int has_self = !is_static;
  oak_type_t* ptypes =
      oak_alloc(c->allocator, sizeof(oak_type_t) * (usize)arity, OAK_HERE);
  u8* pmuts = oak_alloc(c->allocator, sizeof(u8) * (usize)arity, OAK_HERE);
  for (int i = 0; i < arity; ++i)
  {
    oak_type_clear(&ptypes[i]);
    pmuts[i] = 0;
  }
  if (has_self)
  {
    if (oak_fn_self_is_mut(decl))
      pmuts[0] = 1;
  }
  for (int pi = 0; pi < arity - has_self; ++pi)
  {
    const int slot = pi + has_self;
    const oak_ast_node_t* param = oak_fn_param_at(decl, pi);
    if (!param)
      continue;
    pmuts[slot] = oak_param_is_mut(param) ? 1 : 0;
    const oak_ast_node_t* tn = oak_fn_param_type_node(param);
    if (tn)
      oak_lower_type_node(c, tn, &ptypes[slot]);
  }
  *out_types = ptypes;
  *out_muts = pmuts;
}

/* Lower a return-type AST node into an oak_type_t. */
static oak_type_t lower_return_type(oak_compiler_t* c,
                                           const oak_ast_node_t* decl)
{
  oak_type_t rt;
  oak_type_clear(&rt);
  if (!decl)
    return rt;
  const oak_ast_node_t* rtn = oak_fn_return_type_node(decl);
  if (rtn)
    oak_lower_type_node(c, rtn, &rt);
  return rt;
}

/* Export a single free function (no receiver) into the module's export registry. */
static void export_free_fn(oak_compiler_t* c,
                           oak_module_t* mod,
                           const oak_registered_fn_t* e)
{
  oak_type_t* ptypes = null;
  u8* pmuts = null;
  lower_params_from_decl(c, e->decl, e->arity, 1, &ptypes, &pmuts);
  oak_module_export_fn_t exp = {
    .name = e->name,
    .const_idx = e->const_idx,
    .arity = e->arity,
    .param_types = ptypes,
    .param_mut_flags = pmuts,
    .return_type = lower_return_type(c, e->decl),
    .stub_attrs = oak_alloc_attrs(c->allocator, e->attrs, e->attr_count),
    .stub_attr_count = e->attr_count,
  };
  oak_symbol_registry_insert_fn(&mod->exports, e->name, mod->module_id, &exp);
}

static void export_user_fns(oak_compiler_t* c, oak_module_t* mod)
{
  const oak_symbol_t* symbols =
      OAK_CDATA(oak_symbol_t, c->symbols.symbols);
  for (usize i = 0; i < oak_size(c->symbols.symbols); ++i)
  {
    const oak_symbol_t* symbol = &symbols[i];
    if (!symbol->is_exported || symbol->kind != OAK_SYMBOL_FUNCTION)
      continue;
    const oak_registered_fn_t* e =
        oak_cget(c->fns.entries, (usize)symbol->payload_index);
    export_free_fn(c, mod, e);
  }
}

/* Build the methods array of an exported record from its registered methods. */
static void export_record_methods(oak_compiler_t* c,
                                  oak_module_export_record_t* exp,
                                  const oak_registered_record_t* r)
{
  exp->methods = oak_vector_new(
      c->allocator, sizeof(oak_module_export_record_method_t));
  oak_assert(exp->methods);
  const oak_registered_fn_t* methods =
      OAK_CDATA(oak_registered_fn_t, r->methods);
  for (usize mi = 0; mi < oak_size(r->methods); ++mi)
  {
    const oak_registered_fn_t* m = &methods[mi];
    if (!m->is_exported)
      continue;
    oak_module_export_record_method_t mexp = { 0 };
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
    oak_assert(oak_push_back(exp->methods, &mexp));
  }
}

static void export_user_records(oak_compiler_t* c,
                                oak_module_t* mod)
{
  const oak_symbol_t* symbols =
      OAK_CDATA(oak_symbol_t, c->symbols.symbols);
  for (usize i = 0; i < oak_size(c->symbols.symbols); ++i)
  {
    const oak_symbol_t* source_symbol = &symbols[i];
    if (!source_symbol->is_exported ||
        source_symbol->kind != OAK_SYMBOL_RECORD)
      continue;
    const oak_registered_record_t* r =
        oak_cget(c->records.entries, (usize)source_symbol->payload_index);
    oak_module_export_record_t exp = { 0 };
    exp.name = r->name;
    exp.fields = oak_vector_new(
        c->allocator, sizeof(oak_module_export_record_field_t));
    exp.interface_names =
        oak_vector_new(c->allocator, sizeof(const char*));
    oak_assert(exp.fields);
    const oak_record_field_t* fields =
        OAK_CDATA(oak_record_field_t, r->fields);
    for (usize fi = 0; fi < oak_size(r->fields); ++fi)
    {
      oak_module_export_record_field_t field = {
        .name = fields[fi].name,
        .type = fields[fi].type,
      };
      oak_assert(oak_push_back(exp.fields, &field));
    }
    const char* const* interface_names =
        (const char* const*)oak_cdata(r->interface_names);
    for (usize ii = 0; ii < oak_size(r->interface_names); ++ii)
      oak_assert(oak_push_back(exp.interface_names, &interface_names[ii]));
    export_record_methods(c, &exp, r);
    exp.layout_id = 0;
    oak_symbol_registry_insert_record(
        &mod->exports, exp.name, mod->module_id, &exp);
  }
}

/* Group enum variants under their containing enum, creating the
 * oak_module_export_enum_t entry lazily on first variant. */
static void export_user_enums(oak_compiler_t* c,
                              oak_module_t* mod)
{
  const oak_symbol_t* symbols =
      OAK_CDATA(oak_symbol_t, c->symbols.symbols);
  for (usize i = 0; i < oak_size(c->symbols.symbols); ++i)
  {
    const oak_symbol_t* source_symbol = &symbols[i];
    if (!source_symbol->is_exported || source_symbol->kind != OAK_SYMBOL_ENUM)
      continue;
    const oak_registered_enum_t* src =
        oak_cget(c->enums.enums, (usize)source_symbol->payload_index);
    oak_module_export_enum_t ee = { 0 };
    ee.name = src->name;
    ee.variants = oak_vector_new(
        c->allocator, sizeof(oak_module_export_enum_variant_t));
    oak_assert(ee.variants);
    const oak_enum_variant_t* variants =
        OAK_CDATA(oak_enum_variant_t, c->enums.variants);
    for (usize vi = 0; vi < oak_size(c->enums.variants); ++vi)
    {
      const oak_enum_variant_t* v = &variants[vi];
      if (v->type_id != src->type_id)
        continue;
      oak_module_export_enum_variant_t variant = {
        .name = v->name,
        .value = v->value,
      };
      oak_assert(oak_push_back(ee.variants, &variant));
    }
    oak_symbol_registry_insert_enum(
        &mod->exports, ee.name, mod->module_id, &ee);
  }
}

static void export_user_interfaces(oak_compiler_t* c,
                               oak_module_t* mod)
{
  const oak_symbol_t* symbols =
      OAK_CDATA(oak_symbol_t, c->symbols.symbols);
  for (usize i = 0; i < oak_size(c->symbols.symbols); ++i)
  {
    const oak_symbol_t* source_symbol = &symbols[i];
    if (!source_symbol->is_exported || source_symbol->kind != OAK_SYMBOL_INTERFACE)
      continue;
    const oak_registered_interface_t* tr =
        oak_cget(c->interfaces.interfaces, (usize)source_symbol->payload_index);
    oak_module_export_interface_t exp = { 0 };
    exp.name = tr->name;
    exp.methods = oak_vector_new(
        c->allocator, sizeof(oak_module_export_interface_method_t));
    oak_assert(exp.methods);
    const oak_interface_method_t* methods =
        OAK_CDATA(oak_interface_method_t, tr->methods);
    for (usize mi = 0; mi < oak_size(tr->methods); ++mi)
    {
      const oak_interface_method_t* src = &methods[mi];
      oak_type_t* ptypes = null;
      u8* pmuts = null;
      if (src->sig_decl)
        lower_params_from_decl(c, src->sig_decl, src->arity, 0, &ptypes, &pmuts);
      if (pmuts)
        oak_free(c->allocator, pmuts, OAK_HERE);
      oak_module_export_interface_method_t tm = {
        .name = src->name,
        .arity = src->arity,
        .self_is_mut = src->self_is_mut,
        .param_types = ptypes,
        .return_type = src->sig_decl ? lower_return_type(c, src->sig_decl)
                                     : src->return_type,
      };
      oak_assert(oak_push_back(exp.methods, &tm));
    }
    oak_symbol_registry_insert_interface(
        &mod->exports, exp.name, mod->module_id, &exp);
  }
}

/* Populate the current module's export tables from the now-fully-populated
 * compiler registries.  Type IDs stored in exports reference this module's
 * type registry (moved to the module after compilation). */
void oak_populate_module_exports(oak_compiler_t* c)
{
  if (!c->current_module)
    return;
  oak_module_t* mod = c->current_module;
  export_user_fns(c, mod);
  export_user_records(c, mod);
  export_user_enums(c, mod);
  export_user_interfaces(c, mod);
}
