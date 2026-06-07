#include "internal/oak_module_loader.h"

static int native_module_name_eq(const char* module_name, const char* dotted)
{
  return module_name && dotted && strcmp(module_name, dotted) == 0;
}

int opts_has_native_module(const struct oak_compile_options_t* opts,
                           const char* dotted)
{
  if (!opts || !dotted)
    return 0;
  for (int i = 0; i < oak_dynarr_count(opts->native_global_fns); ++i)
  {
    const struct oak_bind_global_fn_t* fn = &opts->native_global_fns[i];
    if (native_module_name_eq(fn->module_name, dotted))
      return 1;
  }
  for (int i = 0; i < oak_dynarr_count(opts->native_types); ++i)
  {
    const struct oak_bind_type_t* type = opts->native_types[i];
    if (type && native_module_name_eq(type->module_name, dotted))
      return 1;
  }
  for (int i = 0; i < oak_dynarr_count(opts->native_enums); ++i)
  {
    const struct oak_bind_enum_t* e = opts->native_enums[i];
    if (e && native_module_name_eq(e->module_name, dotted))
      return 1;
  }
  return 0;
}

static char* native_canonical_path_dup(struct oak_allocator_t* a,
                                       const char* dotted)
{
  const char* prefix = "native:";
  const usize plen = strlen(prefix);
  const usize dlen = strlen(dotted);
  char* out = OAK_ALLOC(a, plen + dlen + 1u);
  memcpy(out, prefix, plen);
  memcpy(out + plen, dotted, dlen);
  out[plen + dlen] = 0;
  return out;
}

static int native_type_in_module(const struct oak_bind_type_t* type,
                                 const char* dotted)
{
  return type && native_module_name_eq(type->module_name, dotted);
}

static oak_type_id_t native_ref_id(const struct oak_bind_type_ref_t* ref)
{
  return ref->type ? ref->type->resolved_type_id : ref->id;
}

static oak_type_id_t native_ref_key_id(const struct oak_bind_type_ref_t* ref)
{
  return ref->key_type ? ref->key_type->resolved_type_id : ref->key_id;
}

void module_loader_filter_native_decls(
    const struct oak_compile_options_t* base_opts,
    const char* dotted,
    struct oak_compile_options_t* opts)
{
  /* Always give the module its own copy of the native binding vectors. `opts`
   * starts as a shallow copy of base_opts, so its vectors alias base_opts'
   * buffers; the compiler may append to them (an attribute callback binding new
   * native types/methods), which would realloc — and free — the shared buffer
   * out from under base_opts. Owning the copy makes those appends safe and
   * keeps base_opts pristine across module compilations. When compiling a
   * native module, also drop that module's own native decls (they are provided
   * by the Oak stub instead). */
  const int is_native_module = opts_has_native_module(base_opts, dotted);

  oak_assert(oak_dynarr_init(opts->allocator, &opts->native_types, sizeof *opts->native_types));
  oak_assert(oak_dynarr_init(opts->allocator, &opts->native_fns, sizeof *opts->native_fns));
  oak_assert(oak_dynarr_init(opts->allocator, &opts->native_global_fns, sizeof *opts->native_global_fns));
  oak_assert(oak_dynarr_init(opts->allocator, &opts->native_enums, sizeof *opts->native_enums));

  for (int i = 0; i < oak_dynarr_count(base_opts->native_types); ++i)
  {
    struct oak_bind_type_t* type = base_opts->native_types[i];
    if (is_native_module && type && native_module_name_eq(type->module_name, dotted))
      continue;
    oak_assert(oak_dynarr_push(&opts->native_types, &type));
  }

  for (int i = 0; i < oak_dynarr_count(base_opts->native_fns); ++i)
  {
    const struct oak_bind_fn_t* fn = &base_opts->native_fns[i];
    if (is_native_module && native_type_in_module(fn->receiver_type, dotted))
      continue;
    oak_assert(oak_dynarr_push(&opts->native_fns, fn));
  }

  for (int i = 0; i < oak_dynarr_count(base_opts->native_global_fns); ++i)
  {
    const struct oak_bind_global_fn_t* fn = &base_opts->native_global_fns[i];
    if (is_native_module && native_module_name_eq(fn->module_name, dotted))
      continue;
    oak_assert(oak_dynarr_push(&opts->native_global_fns, fn));
  }

  for (int i = 0; i < oak_dynarr_count(base_opts->native_enums); ++i)
  {
    struct oak_bind_enum_t* e = base_opts->native_enums[i];
    if (is_native_module && e && native_module_name_eq(e->module_name, dotted))
      continue;
    oak_assert(oak_dynarr_push(&opts->native_enums, &e));
  }
}

void module_loader_free_filtered_native_decls(
    const struct oak_compile_options_t* base_opts,
    const char* dotted,
    struct oak_compile_options_t* opts)
{
  (void)base_opts;
  (void)dotted;
  /* Mirrors module_loader_filter_native_decls, which always allocates owned
   * copies of these vectors. Frees the copied item arrays (not the bound
   * structs they point at, which the embedder owns). */
  oak_dynarr_free(&opts->native_types);
  oak_dynarr_free(&opts->native_fns);
  oak_dynarr_free(&opts->native_global_fns);
  oak_dynarr_free(&opts->native_enums);
}

void apply_native_module_function_exports(
    struct oak_module_t* mod,
    const struct oak_compile_options_t* opts)
{
  if (!mod || !mod->chunk || !opts || !opts_has_native_module(opts, mod->dotted_name))
    return;
  for (int i = 0; i < oak_dynarr_count(opts->native_global_fns); ++i)
  {
    const struct oak_bind_global_fn_t* fn = &opts->native_global_fns[i];
    if (!native_module_name_eq(fn->module_name, mod->dotted_name))
      continue;
    const struct oak_symbol_t* symbol =
        oak_symbol_registry_find(&mod->exports, fn->name);
    if (!symbol || symbol->kind != OAK_SYMBOL_FUNCTION)
      continue;
    const int eidx = symbol->payload_index;
    struct oak_obj_native_fn_t* native =
        oak_native_fn_new(mod->allocator, fn->impl, fn->arity, fn->name);
    struct oak_module_export_fn_t* exp = &mod->exports.fns[eidx];
    if (exp->stub_attrs && exp->stub_attr_count > 0)
      oak_apply_attr_hooks(
          opts, null, native, exp->stub_attrs, exp->stub_attr_count);
    const u16 const_idx =
        (u16)oak_chunk_add_constant(mod->chunk, OAK_VALUE_OBJ(&native->obj));
    exp->const_idx = const_idx;
    if (fn->param_types && fn->arity > 0)
    {
      /* The binding declares its own parameter types; adopt them so the
       * exported signature matches the installed native callback.  The native
       * binding API carries no mutability metadata, so preserve the stub's
       * param_mut_flags when the arity is unchanged (the flags still align with
       * the new param_types); only drop them if the arity actually changes. */
      const int old_arity = exp->arity;
      if (exp->param_types)
        OAK_FREE(mod->allocator, exp->param_types);
      if (exp->param_mut_flags && fn->arity != old_arity)
      {
        OAK_FREE(mod->allocator, exp->param_mut_flags);
        exp->param_mut_flags = null;
      }
      exp->param_types = OAK_ALLOC(
          mod->allocator, (usize)fn->arity * sizeof(struct oak_type_t));
      for (int pi = 0; pi < fn->arity; ++pi)
      {
        oak_type_clear(&exp->param_types[pi]);
        exp->param_types[pi].kind = fn->param_types[pi].kind;
        exp->param_types[pi].id = native_ref_id(&fn->param_types[pi]);
        if (fn->param_types[pi].kind == OAK_TYPE_KIND_MAP)
          exp->param_types[pi].key_id = native_ref_key_id(&fn->param_types[pi]);
      }
      exp->arity = fn->arity;
    }
    /* Otherwise the stub's parameter contract is authoritative; keep arity
     * consistent with the stub's param_types (never index past it). */
    else if (!exp->param_types || fn->arity == exp->arity)
      exp->arity = fn->arity;
    oak_type_clear(&exp->return_type);
    exp->return_type.kind = fn->return_type.kind;
    exp->return_type.id = native_ref_id(&fn->return_type);
    if (fn->return_type.kind == OAK_TYPE_KIND_MAP)
      exp->return_type.key_id = native_ref_key_id(&fn->return_type);
  }
  for (int ri = 0; ri < oak_dynarr_count(mod->exports.records); ++ri)
  {
    struct oak_module_export_record_t* rec = &mod->exports.records[ri];
    const oak_type_id_t rec_type_id =
        oak_type_registry_intern(&mod->types, rec->name);
    for (int mi = 0; mi < oak_dynarr_count(rec->methods); ++mi)
    {
      struct oak_module_export_record_method_t* me = &rec->methods[mi];
      for (int fi = 0; fi < oak_dynarr_count(opts->native_fns); ++fi)
      {
        const struct oak_bind_fn_t* fn = &opts->native_fns[fi];
        if (!fn->receiver_type ||
            fn->receiver_type->resolved_type_id != rec_type_id)
          continue;
        if (!native_type_in_module(fn->receiver_type, mod->dotted_name))
          continue;
        const int is_instance = !me->is_static;
        const enum oak_bind_fn_kind_t want_kind =
            is_instance ? OAK_BIND_FN_INSTANCE_METHOD
                        : OAK_BIND_FN_STATIC_METHOD;
        if (fn->kind != want_kind || strcmp(fn->name, me->name) != 0)
          continue;
        struct oak_obj_native_fn_t* native =
            oak_native_fn_new(mod->allocator, fn->impl, me->arity, fn->name);
        if (me->stub_attrs && me->stub_attr_count > 0)
          oak_apply_attr_hooks(
              opts, null, native, me->stub_attrs, me->stub_attr_count);
        me->const_idx =
            (u16)oak_chunk_add_constant(mod->chunk, OAK_VALUE_OBJ(&native->obj));
        oak_type_clear(&me->return_type);
        me->return_type.kind = fn->return_type.kind;
        me->return_type.id = native_ref_id(&fn->return_type);
        if (fn->return_type.kind == OAK_TYPE_KIND_MAP)
          me->return_type.key_id = native_ref_key_id(&fn->return_type);
        break;
      }
    }
  }
}

/* Strips an OAK_NODE_ATTR_DECL wrapper, returning the inner declaration node.
 * Returns the node unchanged if it is not an attribute declaration. */
const struct oak_ast_node_t*
loader_unwrap_decl(const struct oak_ast_node_t* item)
{
  if (!item || item->kind != OAK_NODE_ATTR_DECL)
    return item;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &item->children)
  {
    const struct oak_ast_node_t* child =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (child->kind != OAK_NODE_ATTR)
      return child;
  }
  return null;
}

static const struct oak_ast_node_t*
loader_fn_decl_name_node(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* proto = decl ? decl->lhs : null;
  const struct oak_ast_node_t* head = proto ? proto->lhs : null;
  return head ? head->rhs : null;
}

static const struct oak_ast_node_t*
loader_fn_decl_param_list(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* proto = decl ? decl->lhs : null;
  const struct oak_ast_node_t* tail = proto ? proto->rhs : null;
  return tail ? tail->lhs : null;
}

static int loader_fn_decl_has_self(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* plist = loader_fn_decl_param_list(decl);
  return plist && plist->lhs;
}

static int loader_fn_decl_param_count(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* plist = loader_fn_decl_param_list(decl);
  if (!plist || !plist->rhs)
    return 0;
  return (int)oak_list_length(&plist->rhs->children);
}

static int loader_fn_decl_is_bodyless(const struct oak_ast_node_t* decl)
{
  return decl && decl->rhs && decl->rhs->kind == OAK_NODE_FN_DECL_SEMICOLON;
}

/* Helpers for top-level METHOD_DECL nodes:
 *   METHOD_DECL (binary: lhs=METHOD_PROTO, rhs=FN_DECL_BODY)
 *     METHOD_PROTO (binary: lhs=METHOD_HEAD, rhs=FN_PARAMS_AND_RET)
 *       METHOD_HEAD (binary: lhs=type IDENT, rhs=method IDENT) */
static const struct oak_ast_node_t*
loader_method_decl_type_node(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* proto = decl ? decl->lhs : null;
  const struct oak_ast_node_t* head = proto ? proto->lhs : null;
  return head ? head->lhs : null;
}

static const struct oak_ast_node_t*
loader_method_decl_name_node(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* proto = decl ? decl->lhs : null;
  const struct oak_ast_node_t* head = proto ? proto->lhs : null;
  return head ? head->rhs : null;
}

static const struct oak_ast_node_t*
loader_method_decl_param_list(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* proto = decl ? decl->lhs : null;
  const struct oak_ast_node_t* params_ret = proto ? proto->rhs : null;
  return params_ret ? params_ret->lhs : null;
}

static int loader_method_decl_has_self(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* plist = loader_method_decl_param_list(decl);
  return plist && plist->lhs;
}

static int loader_method_decl_param_count(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* plist = loader_method_decl_param_list(decl);
  if (!plist || !plist->rhs)
    return 0;
  return (int)oak_list_length(&plist->rhs->children);
}

static const struct oak_ast_node_t*
loader_record_decl_name_node(const struct oak_ast_node_t* record_decl)
{
  const struct oak_ast_node_t* name = record_decl ? record_decl->lhs : null;
  if (name && name->kind == OAK_NODE_TYPE_NAME)
  {
    const struct oak_list_entry_t* first = name->children.next;
    if (first == &name->children)
      return null;
    name = oak_container_of(first, struct oak_ast_node_t, link);
  }
  return (name && name->kind == OAK_NODE_IDENT) ? name : null;
}

static const struct oak_bind_type_t*
find_native_type_decl(const struct oak_compile_options_t* opts,
                      const char* dotted,
                      const char* name)
{
  for (int i = 0; opts && i < oak_dynarr_count(opts->native_types); ++i)
  {
    const struct oak_bind_type_t* type = opts->native_types[i];
    if (type && native_module_name_eq(type->module_name, dotted) &&
        strcmp(type->name, name) == 0)
      return type;
  }
  return null;
}

static int native_global_fn_decl_exists(const struct oak_compile_options_t* opts,
                                        const char* dotted,
                                        const char* name,
                                        int arity)
{
  for (int i = 0; opts && i < oak_dynarr_count(opts->native_global_fns); ++i)
  {
    const struct oak_bind_global_fn_t* fn = &opts->native_global_fns[i];
    if (native_module_name_eq(fn->module_name, dotted) &&
        strcmp(fn->name, name) == 0 && fn->arity == arity)
      return 1;
  }
  return 0;
}

static int native_method_decl_exists(const struct oak_compile_options_t* opts,
                                     const struct oak_bind_type_t* receiver,
                                     const char* name,
                                     int has_self,
                                     int arity)
{
  for (int i = 0; opts && receiver && i < oak_dynarr_count(opts->native_fns); ++i)
  {
    const struct oak_bind_fn_t* fn = &opts->native_fns[i];
    const enum oak_bind_fn_kind_t want_kind =
        has_self ? OAK_BIND_FN_INSTANCE_METHOD : OAK_BIND_FN_STATIC_METHOD;
    if (fn->kind == want_kind && fn->receiver_type == receiver &&
        strcmp(fn->name, name) == 0 && fn->arity == arity)
      return 1;
  }
  return 0;
}

int validate_bodyless_native_decls(struct oak_module_loader_result_t* out,
                                   const struct oak_module_t* mod,
                                   const struct oak_compile_options_t* opts)
{
  if (!opts_has_native_module(opts, mod->dotted_name))
    return 1;
  const struct oak_ast_node_t* root = oak_parser_root(&mod->parser);
  if (!root)
    return 1;
  int ok = 1;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &root->children)
  {
    const struct oak_ast_node_t* item =
        loader_unwrap_decl(oak_container_of(pos, struct oak_ast_node_t, link));
    if (!item)
      continue;
    if (item->kind == OAK_NODE_FN_DECL && loader_fn_decl_is_bodyless(item))
    {
      const struct oak_ast_node_t* name_node = loader_fn_decl_name_node(item);
      const char* name = oak_token_text(name_node->token);
      const int arity = loader_fn_decl_param_count(item);
      if (!native_global_fn_decl_exists(opts, mod->dotted_name, name, arity))
      {
        loader_error(out,
                     "%s: bodyless function '%s' has no native binding",
                     mod->dotted_name,
                     name);
        ok = 0;
      }
      continue;
    }
    if (item->kind == OAK_NODE_METHOD_DECL && loader_fn_decl_is_bodyless(item))
    {
      const struct oak_ast_node_t* type_node = loader_method_decl_type_node(item);
      const struct oak_ast_node_t* name_node = loader_method_decl_name_node(item);
      if (!type_node || !name_node)
        continue;
      const char* type_name = oak_token_text(type_node->token);
      const char* name = oak_token_text(name_node->token);
      const int has_self = loader_method_decl_has_self(item);
      const int arity = loader_method_decl_param_count(item);
      const struct oak_bind_type_t* receiver =
          find_native_type_decl(opts, mod->dotted_name, type_name);
      if (!native_method_decl_exists(opts, receiver, name, has_self, arity))
      {
        loader_error(out,
                     "%s: bodyless method '%s.%s' has no native binding",
                     mod->dotted_name,
                     type_name,
                     name);
        ok = 0;
      }
      continue;
    }
    if (item->kind != OAK_NODE_RECORD_DECL || !item->rhs)
      continue;
    const struct oak_ast_node_t* record_name_node =
        loader_record_decl_name_node(item);
    if (!record_name_node)
      continue;
    const char* record_name = oak_token_text(record_name_node->token);
    const struct oak_bind_type_t* receiver =
        find_native_type_decl(opts, mod->dotted_name, record_name);
    struct oak_list_entry_t* mpos;
    oak_list_for_each(mpos, &item->rhs->children)
    {
      const struct oak_ast_node_t* member =
          loader_unwrap_decl(oak_container_of(mpos, struct oak_ast_node_t, link));
      if (!member)
        continue;
      if (member->kind != OAK_NODE_FN_DECL || !loader_fn_decl_is_bodyless(member))
        continue;
      const struct oak_ast_node_t* name_node = loader_fn_decl_name_node(member);
      const char* name = oak_token_text(name_node->token);
      const int has_self = loader_fn_decl_has_self(member);
      const int arity = loader_fn_decl_param_count(member);
      if (!native_method_decl_exists(opts, receiver, name, has_self, arity))
      {
        loader_error(out,
                     "%s: bodyless method '%s.%s' has no native binding",
                     mod->dotted_name,
                     record_name,
                     name);
        ok = 0;
      }
    }
  }
  return ok;
}

struct oak_module_t* create_native_module(
    struct oak_module_registry_t* reg,
    const struct oak_compile_options_t* opts,
    const char* dotted,
    struct oak_module_loader_result_t* out)
{
  struct oak_allocator_t* a = reg->allocator;
  char* canonical = native_canonical_path_dup(a, dotted);
  struct oak_module_t* existing =
      oak_module_registry_find_by_path(reg, canonical);
  if (existing)
  {
    OAK_FREE(a, canonical);
    return existing;
  }

  struct oak_module_t* mod =
      oak_module_registry_create(reg, canonical, dotted);
  OAK_FREE(a, canonical);
  if (!mod)
  {
    loader_error(out, "out of memory creating native module '%s'", dotted);
    return null;
  }

  mod->chunk = OAK_ALLOC(a, sizeof(struct oak_chunk_t));
  oak_chunk_init(mod->chunk, a);
  mod->chunk->module_id = mod->module_id;
  oak_type_registry_init(&mod->types, a);
  oak_type_registry_set_owner(&mod->types, mod->module_id);
  for (int i = 0; i < oak_dynarr_count(opts->native_types); ++i)
  {
    struct oak_bind_type_t* type = opts->native_types[i];
    if (!type || !native_module_name_eq(type->module_name, dotted))
      continue;
    type->resolved_type_id = oak_type_registry_intern(&mod->types, type->name);
  }

  for (int i = 0; i < oak_dynarr_count(opts->native_global_fns); ++i)
  {
    const struct oak_bind_global_fn_t* fn = &opts->native_global_fns[i];
    if (!native_module_name_eq(fn->module_name, dotted))
      continue;
    struct oak_obj_native_fn_t* native =
        oak_native_fn_new(a, fn->impl, fn->arity, fn->name);
    const u16 const_idx =
        (u16)oak_chunk_add_constant(mod->chunk, OAK_VALUE_OBJ(&native->obj));
    struct oak_module_export_fn_t exp = {
      .name = fn->name,
      .const_idx = const_idx,
      .arity = fn->arity,
      .return_type = {
        .id = native_ref_id(&fn->return_type),
        .key_id = fn->return_type.kind == OAK_TYPE_KIND_MAP
                      ? native_ref_key_id(&fn->return_type)
                      : OAK_TYPE_VOID,
        .kind = fn->return_type.kind,
      },
    };
    /* Carry the parameter contract so imported calls are type-checked. */
    if (fn->param_types && fn->arity > 0)
    {
      exp.param_types =
          OAK_ALLOC(a, (usize)fn->arity * sizeof(struct oak_type_t));
      for (int pi = 0; pi < fn->arity; ++pi)
      {
        oak_type_clear(&exp.param_types[pi]);
        exp.param_types[pi].kind = fn->param_types[pi].kind;
        exp.param_types[pi].id = native_ref_id(&fn->param_types[pi]);
        if (fn->param_types[pi].kind == OAK_TYPE_KIND_MAP)
          exp.param_types[pi].key_id = native_ref_key_id(&fn->param_types[pi]);
      }
    }
    oak_symbol_registry_insert_fn(
        &mod->exports, exp.name, mod->module_id, &exp);
  }

  for (int i = 0; i < oak_dynarr_count(opts->native_types); ++i)
  {
    const struct oak_bind_type_t* type = opts->native_types[i];
    if (!type || !native_module_name_eq(type->module_name, dotted))
      continue;
    struct oak_module_export_record_t exp = { 0 };
    exp.name = type->name;
    exp.is_value = (type->kind == OAK_BIND_TYPE_VALUE);
    oak_assert(oak_dynarr_init(a, &exp.fields, sizeof *exp.fields));
    oak_assert(oak_dynarr_init(a, &exp.methods, sizeof *exp.methods));
    for (int fi = 0; fi < oak_dynarr_count(type->fields); ++fi)
    {
      struct oak_module_export_record_field_t field = {
        .name = type->fields[fi].name,
        .type = {
          .id = native_ref_id(&type->fields[fi].type),
          .key_id = type->fields[fi].type.kind == OAK_TYPE_KIND_MAP
                        ? native_ref_key_id(&type->fields[fi].type)
                        : OAK_TYPE_VOID,
          .kind = type->fields[fi].type.kind,
        },
      };
      oak_assert(oak_dynarr_push(&exp.fields, &field));
    }
    oak_symbol_registry_insert_record(
        &mod->exports, exp.name, mod->module_id, &exp);
  }

  for (int i = 0; i < oak_dynarr_count(opts->native_enums); ++i)
  {
    const struct oak_bind_enum_t* e = opts->native_enums[i];
    if (!e || !native_module_name_eq(e->module_name, dotted))
      continue;
    struct oak_module_export_enum_t exp = { 0 };
    exp.name = e->name;
    oak_assert(oak_dynarr_init(a, &exp.variants, sizeof *exp.variants));
    for (int vi = 0; vi < oak_dynarr_count(e->variants); ++vi)
    {
      struct oak_module_export_enum_variant_t variant = {
        .name = e->variants[vi].name,
        .value = e->variants[vi].value,
      };
      oak_assert(oak_dynarr_push(&exp.variants, &variant));
    }
    oak_symbol_registry_insert_enum(
        &mod->exports, exp.name, mod->module_id, &exp);
  }

  mod->state = OAK_MOD_COMPILED;
  return mod;
}
