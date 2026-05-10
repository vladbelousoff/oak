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
  for (int i = 0; i < opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* fn = &opts->native_fns.items[i];
    if (fn->kind == OAK_BIND_FN_GLOBAL &&
        native_module_name_eq(fn->module_name, dotted))
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

static char* native_canonical_path_dup(const char* dotted)
{
  const char* prefix = "native:";
  const usize plen = strlen(prefix);
  const usize dlen = strlen(dotted);
  char* out = oak_alloc(plen + dlen + 1u, OAK_SRC_LOC);
  memcpy(out, prefix, plen);
  memcpy(out + plen, dotted, dlen);
  out[plen + dlen] = 0;
  return out;
}

static const char* builtin_type_name(const oak_type_id_t id)
{
  switch (id)
  {
    case OAK_TYPE_NUMBER:
      return "number";
    case OAK_TYPE_STRING:
      return "string";
    case OAK_TYPE_BOOL:
      return "bool";
    default:
      return "unknown";
  }
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
  oak_dynarr_init(&opts->native_enums.items,
                  &opts->native_enums.count,
                  &opts->native_enums.capacity);

  for (int i = 0; i < base_opts->native_types.count; ++i)
  {
    struct oak_bind_type_t* type = base_opts->native_types.items[i];
    if (type && native_module_name_eq(type->module_name, dotted))
      continue;
    oak_dynarr_push(&opts->native_types.items,
                    &opts->native_types.count,
                    &opts->native_types.capacity,
                    &type,
                    sizeof(type));
  }

  for (int i = 0; i < base_opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* fn = &base_opts->native_fns.items[i];
    if (fn->kind == OAK_BIND_FN_GLOBAL &&
        native_module_name_eq(fn->module_name, dotted))
      continue;
    if (fn->receiver_type_id != OAK_TYPE_VOID &&
        native_type_in_module(base_opts, fn->receiver_type_id, dotted))
      continue;
    oak_dynarr_push(&opts->native_fns.items,
                    &opts->native_fns.count,
                    &opts->native_fns.capacity,
                    fn,
                    sizeof(*fn));
  }

  for (int i = 0; i < base_opts->native_enums.count; ++i)
  {
    struct oak_bind_enum_t* e = base_opts->native_enums.items[i];
    if (e && native_module_name_eq(e->module_name, dotted))
      continue;
    oak_dynarr_push(&opts->native_enums.items,
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
  oak_dynarr_free(
      &opts->native_types.items, &opts->native_types.count, &opts->native_types.capacity);
  oak_dynarr_free(
      &opts->native_fns.items, &opts->native_fns.count, &opts->native_fns.capacity);
  oak_dynarr_free(&opts->native_enums.items,
                  &opts->native_enums.count,
                  &opts->native_enums.capacity);
}

void apply_native_module_function_exports(
    struct oak_module_t* mod,
    const struct oak_compile_options_t* opts)
{
  if (!mod || !mod->chunk || !opts || !opts_has_native_module(opts, mod->dotted_name))
    return;
  for (int i = 0; i < opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* fn = &opts->native_fns.items[i];
    if (fn->kind != OAK_BIND_FN_GLOBAL ||
        !native_module_name_eq(fn->module_name, mod->dotted_name))
      continue;
    const int eidx =
        oak_htable_get(&mod->exports_fn.by_name, fn->name, strlen(fn->name));
    if (eidx < 0)
      continue;
    struct oak_obj_native_fn_t* native =
        oak_native_fn_new(fn->impl, fn->arity, fn->name);
    const u16 const_idx =
        (u16)oak_chunk_add_constant(mod->chunk, OAK_VALUE_OBJ(&native->obj));
    struct oak_module_export_fn_t* exp = &mod->exports_fn.items[eidx];
    exp->const_idx = const_idx;
    exp->arity = fn->arity;
    exp->return_type_node = null;
    exp->return_type_id = fn->return_type_id;
    exp->return_kind = (fn->return_shape == OAK_BIND_SHAPE_ARRAY)
                           ? OAK_TYPE_KIND_ARRAY
                           : OAK_TYPE_KIND_SCALAR;
  }
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
  for (int i = 0; opts && i < opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* fn = &opts->native_fns.items[i];
    if (fn->kind == OAK_BIND_FN_GLOBAL &&
        native_module_name_eq(fn->module_name, dotted) &&
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
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind == OAK_NODE_FN_DECL && loader_fn_decl_is_bodyless(item))
    {
      const struct oak_ast_node_t* name_node = loader_fn_decl_name_node(item);
      const char* name = oak_token_text(name_node->token);
      const usize name_len = oak_token_length(name_node->token);
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
    if (item->kind != OAK_NODE_RECORD_DECL || !item->rhs)
      continue;
    const struct oak_ast_node_t* record_name_node =
        loader_record_decl_name_node(item);
    if (!record_name_node)
      continue;
    const char* record_name = oak_token_text(record_name_node->token);
    const usize record_name_len = oak_token_length(record_name_node->token);
    const struct oak_bind_type_t* receiver =
        find_native_type_decl(opts, mod->dotted_name, record_name);
    struct oak_list_entry_t* mpos;
    oak_list_for_each(mpos, &item->rhs->children)
    {
      const struct oak_ast_node_t* member =
          oak_container_of(mpos, struct oak_ast_node_t, link);
      if (member->kind != OAK_NODE_FN_DECL || !loader_fn_decl_is_bodyless(member))
        continue;
      const struct oak_ast_node_t* name_node = loader_fn_decl_name_node(member);
      const char* name = oak_token_text(name_node->token);
      const usize name_len = oak_token_length(name_node->token);
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
  char* canonical = native_canonical_path_dup(dotted);
  struct oak_module_t* existing =
      oak_module_registry_find_by_path(reg, canonical);
  if (existing)
  {
    oak_free(canonical, OAK_SRC_LOC);
    return existing;
  }

  struct oak_module_t* mod =
      oak_module_registry_create(reg, canonical, dotted);
  oak_free(canonical, OAK_SRC_LOC);
  if (!mod)
  {
    loader_error(out, "out of memory creating native module '%s'", dotted);
    return null;
  }

  mod->chunk = oak_alloc(sizeof(struct oak_chunk_t), OAK_SRC_LOC);
  oak_chunk_init(mod->chunk);
  mod->chunk->module_id = mod->module_id;

  for (int i = 0; i < opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* fn = &opts->native_fns.items[i];
    if (fn->kind != OAK_BIND_FN_GLOBAL ||
        !native_module_name_eq(fn->module_name, dotted))
      continue;
    struct oak_obj_native_fn_t* native =
        oak_native_fn_new(fn->impl, fn->arity, fn->name);
    const u16 const_idx =
        (u16)oak_chunk_add_constant(mod->chunk, OAK_VALUE_OBJ(&native->obj));
    struct oak_module_export_fn_t exp = {
      .name = fn->name,
      .name_len = strlen(fn->name),
      .const_idx = const_idx,
      .arity = fn->arity,
      .return_type_node = null,
      .return_type_id = fn->return_type_id,
      .return_kind = (fn->return_shape == OAK_BIND_SHAPE_ARRAY)
                         ? OAK_TYPE_KIND_ARRAY
                         : OAK_TYPE_KIND_SCALAR,
    };
    const int idx = mod->exports_fn.count;
    oak_dynarr_push(&mod->exports_fn.items,
                    &mod->exports_fn.count,
                    &mod->exports_fn.capacity,
                    &exp,
                    sizeof(exp));
    oak_htable_insert(&mod->exports_fn.by_name, exp.name, exp.name_len, idx);
  }

  for (int i = 0; i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* type = opts->native_types.items[i];
    if (!type || !native_module_name_eq(type->module_name, dotted))
      continue;
    struct oak_module_export_record_t exp = { 0 };
    exp.name = type->name;
    exp.name_len = type->name_len;
    exp.field_count = type->field_count > OAK_MODULE_MAX_RECORD_FIELDS
                          ? OAK_MODULE_MAX_RECORD_FIELDS
                          : type->field_count;
    for (int fi = 0; fi < exp.field_count; ++fi)
    {
      exp.fields[fi].name = type->fields[fi].name;
      exp.fields[fi].name_len = type->fields[fi].name_len;
      exp.fields[fi].type_name = builtin_type_name(type->fields[fi].field_type_id);
      exp.fields[fi].type_name_len = strlen(exp.fields[fi].type_name);
    }
    const int idx = mod->exports_record.count;
    oak_dynarr_push(&mod->exports_record.items,
                    &mod->exports_record.count,
                    &mod->exports_record.capacity,
                    &exp,
                    sizeof(exp));
    oak_htable_insert(
        &mod->exports_record.by_name, exp.name, exp.name_len, idx);
  }

  for (int i = 0; i < opts->native_enums.count; ++i)
  {
    const struct oak_bind_enum_t* e = opts->native_enums.items[i];
    if (!e || !native_module_name_eq(e->module_name, dotted))
      continue;
    struct oak_module_export_enum_t exp = { 0 };
    exp.name = e->name;
    exp.name_len = e->name_len;
    exp.variant_count = e->variant_count > OAK_MODULE_MAX_ENUM_VARIANTS
                            ? OAK_MODULE_MAX_ENUM_VARIANTS
                            : e->variant_count;
    for (int vi = 0; vi < exp.variant_count; ++vi)
    {
      exp.variants[vi].name = e->variants[vi].name;
      exp.variants[vi].name_len = e->variants[vi].name_len;
      exp.variants[vi].value = e->variants[vi].value;
    }
    const int idx = mod->exports_enum.count;
    oak_dynarr_push(&mod->exports_enum.items,
                    &mod->exports_enum.count,
                    &mod->exports_enum.capacity,
                    &exp,
                    sizeof(exp));
    oak_htable_insert(&mod->exports_enum.by_name, exp.name, exp.name_len, idx);
  }

  mod->state = OAK_MOD_COMPILED;
  return mod;
}
