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
  for (int i = 0; i < opts->native_global_fns.count; ++i)
  {
    const struct oak_bind_global_fn_t* fn = &opts->native_global_fns.items[i];
    if (native_module_name_eq(fn->module_name, dotted))
      return 1;
  }
  for (int i = 0; i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* type = opts->native_types.items[i];
    if (type && native_module_name_eq(type->module_name, dotted))
      return 1;
  }
  for (int i = 0; i < opts->native_enums.count; ++i)
  {
    const struct oak_bind_enum_t* e = opts->native_enums.items[i];
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

static int native_type_in_module(const struct oak_compile_options_t* opts,
                                 oak_type_id_t type_id,
                                 const char* dotted)
{
  if (!opts || !dotted)
    return 0;
  for (int i = 0; i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* type = opts->native_types.items[i];
    if (type && type->type_id == type_id &&
        native_module_name_eq(type->module_name, dotted))
      return 1;
  }
  return 0;
}

void module_loader_filter_native_decls(
    const struct oak_compile_options_t* base_opts,
    const char* dotted,
    struct oak_compile_options_t* opts)
{
  if (!opts_has_native_module(base_opts, dotted))
    return;

  oak_dynarr_init(
      &opts->native_types.items, &opts->native_types.count, &opts->native_types.capacity);
  oak_dynarr_init(
      &opts->native_fns.items, &opts->native_fns.count, &opts->native_fns.capacity);
  oak_dynarr_init(&opts->native_global_fns.items,
                  &opts->native_global_fns.count,
                  &opts->native_global_fns.capacity);
  oak_dynarr_init(&opts->native_enums.items,
                  &opts->native_enums.count,
                  &opts->native_enums.capacity);

  for (int i = 0; i < base_opts->native_types.count; ++i)
  {
    struct oak_bind_type_t* type = base_opts->native_types.items[i];
    if (type && native_module_name_eq(type->module_name, dotted))
      continue;
    oak_dynarr_push(opts->allocator, &opts->native_types.items,
                    &opts->native_types.count,
                    &opts->native_types.capacity,
                    &type,
                    sizeof(type));
  }

  for (int i = 0; i < base_opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* fn = &base_opts->native_fns.items[i];
    if (native_type_in_module(base_opts, fn->receiver_type_id, dotted))
      continue;
    oak_dynarr_push(opts->allocator, &opts->native_fns.items,
                    &opts->native_fns.count,
                    &opts->native_fns.capacity,
                    fn,
                    sizeof(*fn));
  }

  for (int i = 0; i < base_opts->native_global_fns.count; ++i)
  {
    const struct oak_bind_global_fn_t* fn = &base_opts->native_global_fns.items[i];
    if (native_module_name_eq(fn->module_name, dotted))
      continue;
    oak_dynarr_push(opts->allocator, &opts->native_global_fns.items,
                    &opts->native_global_fns.count,
                    &opts->native_global_fns.capacity,
                    fn,
                    sizeof(*fn));
  }

  for (int i = 0; i < base_opts->native_enums.count; ++i)
  {
    struct oak_bind_enum_t* e = base_opts->native_enums.items[i];
    if (e && native_module_name_eq(e->module_name, dotted))
      continue;
    oak_dynarr_push(opts->allocator, &opts->native_enums.items,
                    &opts->native_enums.count,
                    &opts->native_enums.capacity,
                    &e,
                    sizeof(e));
  }
}

void module_loader_free_filtered_native_decls(
    const struct oak_compile_options_t* base_opts,
    const char* dotted,
    struct oak_compile_options_t* opts)
{
  if (!opts_has_native_module(base_opts, dotted))
    return;
  oak_dynarr_free(opts->allocator,
      &opts->native_types.items, &opts->native_types.count, &opts->native_types.capacity);
  oak_dynarr_free(opts->allocator,
      &opts->native_fns.items, &opts->native_fns.count, &opts->native_fns.capacity);
  oak_dynarr_free(opts->allocator, &opts->native_global_fns.items,
                  &opts->native_global_fns.count,
                  &opts->native_global_fns.capacity);
  oak_dynarr_free(opts->allocator, &opts->native_enums.items,
                  &opts->native_enums.count,
                  &opts->native_enums.capacity);
}

void apply_native_module_function_exports(
    struct oak_module_t* mod,
    const struct oak_compile_options_t* opts)
{
  if (!mod || !mod->chunk || !opts || !opts_has_native_module(opts, mod->dotted_name))
    return;
  for (int i = 0; i < opts->native_global_fns.count; ++i)
  {
    const struct oak_bind_global_fn_t* fn = &opts->native_global_fns.items[i];
    if (!native_module_name_eq(fn->module_name, mod->dotted_name))
      continue;
    const int eidx =
        oak_htable_get(&mod->exports_fn.by_name, fn->name, (int)strlen(fn->name));
    if (eidx < 0)
      continue;
    struct oak_obj_native_fn_t* native =
        oak_native_fn_new(mod->allocator, fn->impl, fn->arity, fn->name);
    struct oak_module_export_fn_t* exp = &mod->exports_fn.items[eidx];
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
        exp->param_types[pi].id = fn->param_types[pi].id;
        if (fn->param_types[pi].kind == OAK_TYPE_KIND_MAP)
          exp->param_types[pi].key_id = fn->param_types[pi].key_id;
      }
      exp->arity = fn->arity;
    }
    /* Otherwise the stub's parameter contract is authoritative; keep arity
     * consistent with the stub's param_types (never index past it). */
    else if (!exp->param_types || fn->arity == exp->arity)
      exp->arity = fn->arity;
    oak_type_clear(&exp->return_type);
    exp->return_type.kind = fn->return_type.kind;
    exp->return_type.id = fn->return_type.id;
    if (fn->return_type.kind == OAK_TYPE_KIND_MAP)
      exp->return_type.key_id = fn->return_type.key_id;
  }
  for (int ri = 0; ri < mod->exports_record.count; ++ri)
  {
    struct oak_module_export_record_t* rec = &mod->exports_record.items[ri];
    const oak_type_id_t rec_type_id =
        oak_type_registry_intern(&mod->types, rec->name, (int)strlen(rec->name));
    for (int mi = 0; mi < rec->method_count; ++mi)
    {
      struct oak_module_export_record_method_t* me = &rec->methods[mi];
      for (int fi = 0; fi < opts->native_fns.count; ++fi)
      {
        const struct oak_bind_fn_t* fn = &opts->native_fns.items[fi];
        if (fn->receiver_type_id != rec_type_id)
          continue;
        if (!native_type_in_module(opts, fn->receiver_type_id, mod->dotted_name))
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
        me->return_type.id = fn->return_type.id;
        if (fn->return_type.kind == OAK_TYPE_KIND_MAP)
          me->return_type.key_id = fn->return_type.key_id;
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
  for (int i = 0; opts && i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* type = opts->native_types.items[i];
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
  for (int i = 0; opts && i < opts->native_global_fns.count; ++i)
  {
    const struct oak_bind_global_fn_t* fn = &opts->native_global_fns.items[i];
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
  for (int i = 0; opts && receiver && i < opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* fn = &opts->native_fns.items[i];
    const enum oak_bind_fn_kind_t want_kind =
        has_self ? OAK_BIND_FN_INSTANCE_METHOD : OAK_BIND_FN_STATIC_METHOD;
    if (fn->kind == want_kind && fn->receiver_type_id == receiver->type_id &&
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
      const int name_len = oak_token_size(name_node->token);
      const int arity = loader_fn_decl_param_count(item);
      if (!native_global_fn_decl_exists(opts, mod->dotted_name, name, arity))
      {
        loader_error(out,
                     "%s: bodyless function '%.*s' has no native binding",
                     mod->dotted_name,
                     (int)name_len,
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
      const int name_len = oak_token_size(name_node->token);
      const int has_self = loader_method_decl_has_self(item);
      const int arity = loader_method_decl_param_count(item);
      const struct oak_bind_type_t* receiver =
          find_native_type_decl(opts, mod->dotted_name, type_name);
      if (!native_method_decl_exists(opts, receiver, name, has_self, arity))
      {
        loader_error(out,
                     "%s: bodyless method '%s.%.*s' has no native binding",
                     mod->dotted_name,
                     type_name,
                     (int)name_len,
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
    const int record_name_len = oak_token_size(record_name_node->token);
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
      const int name_len = oak_token_size(name_node->token);
      const int has_self = loader_fn_decl_has_self(member);
      const int arity = loader_fn_decl_param_count(member);
      if (!native_method_decl_exists(opts, receiver, name, has_self, arity))
      {
        loader_error(out,
                     "%s: bodyless method '%.*s.%.*s' has no native binding",
                     mod->dotted_name,
                     (int)record_name_len,
                     record_name,
                     (int)name_len,
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

  for (int i = 0; i < opts->native_global_fns.count; ++i)
  {
    const struct oak_bind_global_fn_t* fn = &opts->native_global_fns.items[i];
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
        .id = fn->return_type.id,
        .key_id = fn->return_type.kind == OAK_TYPE_KIND_MAP
                      ? fn->return_type.key_id
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
        exp.param_types[pi].id = fn->param_types[pi].id;
        if (fn->param_types[pi].kind == OAK_TYPE_KIND_MAP)
          exp.param_types[pi].key_id = fn->param_types[pi].key_id;
      }
    }
    const int idx = mod->exports_fn.count;
    oak_dynarr_push(a, &mod->exports_fn.items,
                    &mod->exports_fn.count,
                    &mod->exports_fn.capacity,
                    &exp,
                    sizeof(exp));
    oak_htable_insert(&mod->exports_fn.by_name, exp.name, (int)strlen(exp.name), idx);
  }

  for (int i = 0; i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* type = opts->native_types.items[i];
    if (!type || !native_module_name_eq(type->module_name, dotted))
      continue;
    struct oak_module_export_record_t exp = { 0 };
    exp.name = type->name;
    exp.is_value = (type->kind == OAK_BIND_TYPE_VALUE);
    oak_dynarr_init(&exp.fields, &exp.field_count, &exp.field_capacity);
    oak_dynarr_init(&exp.methods, &exp.method_count, &exp.method_capacity);
    for (int fi = 0; fi < type->field_count; ++fi)
    {
      struct oak_module_export_record_field_t field = {
        .name = type->fields[fi].name,
        .type = {
          .id = type->fields[fi].type.id,
          .key_id = type->fields[fi].type.kind == OAK_TYPE_KIND_MAP
                        ? type->fields[fi].type.key_id
                        : OAK_TYPE_VOID,
          .kind = type->fields[fi].type.kind,
        },
      };
      oak_dynarr_push(a, &exp.fields,
                      &exp.field_count,
                      &exp.field_capacity,
                      &field,
                      sizeof(field));
    }
    const int idx = mod->exports_record.count;
    oak_dynarr_push(a, &mod->exports_record.items,
                    &mod->exports_record.count,
                    &mod->exports_record.capacity,
                    &exp,
                    sizeof(exp));
    oak_htable_insert(
        &mod->exports_record.by_name, exp.name, (int)strlen(exp.name), idx);
  }

  for (int i = 0; i < opts->native_enums.count; ++i)
  {
    const struct oak_bind_enum_t* e = opts->native_enums.items[i];
    if (!e || !native_module_name_eq(e->module_name, dotted))
      continue;
    struct oak_module_export_enum_t exp = { 0 };
    exp.name = e->name;
    oak_dynarr_init(&exp.variants, &exp.variant_count, &exp.variant_capacity);
    for (int vi = 0; vi < e->variant_count; ++vi)
    {
      struct oak_module_export_enum_variant_t variant = {
        .name = e->variants[vi].name,
        .value = e->variants[vi].value,
      };
      oak_dynarr_push(a, &exp.variants,
                      &exp.variant_count,
                      &exp.variant_capacity,
                      &variant,
                      sizeof(variant));
    }
    const int idx = mod->exports_enum.count;
    oak_dynarr_push(a, &mod->exports_enum.items,
                    &mod->exports_enum.count,
                    &mod->exports_enum.capacity,
                    &exp,
                    sizeof(exp));
    oak_htable_insert(&mod->exports_enum.by_name, exp.name, (int)strlen(exp.name), idx);
  }

  oak_type_registry_init(&mod->types, a);
  for (int i = 0; i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* type = opts->native_types.items[i];
    if (!type || !native_module_name_eq(type->module_name, dotted))
      continue;
    if (type->type_id >= OAK_TYPE_FIRST_USER)
      oak_type_registry_intern_with_id(
          &mod->types, type->name, (int)strlen(type->name), type->type_id);
  }

  mod->state = OAK_MOD_COMPILED;
  return mod;
}
