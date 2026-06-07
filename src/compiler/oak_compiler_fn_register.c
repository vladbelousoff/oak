#include "internal/oak_compiler.h"

/* ---------- oak_fn_registry_t lifecycle ---------- */

void oak_fn_registry_init(struct oak_fn_registry_t* r,
                          struct oak_allocator_t* allocator)
{
  r->allocator = allocator;
  oak_htable_init(&r->by_name, allocator);
  oak_assert(oak_dynarr_init(r->allocator, &r->entries, sizeof *r->entries));
}

void oak_fn_registry_free(struct oak_fn_registry_t* r)
{
  for (int i = 0; i < oak_dynarr_count(r->entries); ++i)
  {
    if (r->entries[i].attrs)
      OAK_FREE(r->allocator, r->entries[i].attrs);
    if (r->entries[i].param_types)
      OAK_FREE(r->allocator, r->entries[i].param_types);
  }
  oak_htable_free(&r->by_name);
  oak_dynarr_free(&r->entries);
}

struct oak_registered_fn_t*
oak_fn_registry_insert(struct oak_fn_registry_t* r,
                       const struct oak_registered_fn_t* fn)
{
  oak_assert(oak_dynarr_push(&r->entries, fn));
  const int idx = oak_dynarr_count(r->entries) - 1;
  oak_htable_insert(&r->by_name,
                    r->entries[idx].name,
                    strlen(r->entries[idx].name),
                    idx);
  return &r->entries[idx];
}

const struct oak_registered_fn_t* oak_fn_registry_find(
    const struct oak_fn_registry_t* r, const char* name)
{
  const int idx = oak_htable_get(&r->by_name, name, strlen(name));
  if (idx < 0)
    return null;
  return &r->entries[idx];
}

/* ---------- Registration helpers ---------- */

static const struct oak_ast_node_t*
record_decl_type_ident(const struct oak_ast_node_t* record_decl)
{
  if (!record_decl->lhs)
    return null;
  const struct oak_ast_node_t* name_ident = record_decl->lhs;
  if (name_ident->kind == OAK_NODE_TYPE_NAME)
  {
    const struct oak_list_entry_t* tn_first = name_ident->children.next;
    if (tn_first == &name_ident->children)
      return null;
    name_ident = oak_container_of(tn_first, struct oak_ast_node_t, link);
  }
  if (name_ident->kind != OAK_NODE_IDENT)
    return null;
  return name_ident;
}

static void register_regular_fn_decl(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* raw_item,
                                     const struct oak_ast_node_t* item)
{
  const struct oak_ast_node_t* name_node = oak_fn_name_node(item);
  const char* name = oak_token_text(name_node->token);
  const int len = (int)strlen(name);
  const int explicit_arity = oak_count_fn_params(item);
  const struct oak_ast_node_t* self_param =
      oak_fn_self_param(item);

  if (self_param)
  {
    const struct oak_ast_node_t* first_child =
        self_param->lhs ? self_param->lhs : self_param->rhs;
    oak_compiler_error_at(
        c,
        first_child->token,
        "'self' is only valid on instance methods: use `fn TypeName.%s(self, ...)` syntax",
        name);
    return;
  }

  if (oak_fn_registry_find(&c->fns, name))
  {
    oak_compiler_error_at(c, name_node->token, "duplicate function '%s'", name);
    return;
  }

  const u16 mid =
      c->current_module ? c->current_module->module_id : (u16)0xFFFFu;
  struct oak_obj_fn_t* fn_obj = oak_fn_new(c->allocator, 0, explicit_arity, mid);
  char* name_copy = OAK_ALLOC(c->allocator, len + 1u);
  memcpy(name_copy, name, len + 1u);
  fn_obj->name = name_copy;
  const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));

  int attr_count = 0;
  const char** attrs = oak_extract_attrs(c->allocator, raw_item, &attr_count);

  struct oak_attr_param_info_t* pinfo = null;
  if (attr_count > 0 && explicit_arity > 0)
  {
    pinfo = OAK_ALLOC(c->allocator,
                      (usize)explicit_arity * sizeof(struct oak_attr_param_info_t));
    for (int pi = 0; pi < explicit_arity; ++pi)
    {
      const struct oak_ast_node_t* param = oak_fn_param_at(item, pi);
      const struct oak_ast_node_t* id_node = oak_fn_param_ident(param);
      const struct oak_ast_node_t* ty_node = oak_fn_param_type_node(param);
      pinfo[pi].name = id_node ? oak_token_text(id_node->token) : "";
      pinfo[pi].is_mut = oak_param_is_mut(param);
      pinfo[pi].is_weak = 0;
      pinfo[pi].type_name = "";
      pinfo[pi].type_id = -1;
      if (ty_node)
      {
        const struct oak_ast_node_t* resolved = ty_node;
        if (resolved->kind == OAK_NODE_TYPE_WEAK)
        {
          pinfo[pi].is_weak = 1;
          resolved = resolved->child;
        }
        if (resolved && resolved->kind == OAK_NODE_IDENT)
        {
          pinfo[pi].type_name = oak_token_text(resolved->token);
        }
      }
    }
  }

  oak_compiler_dispatch_attr_cbs(c, attrs, attr_count, name, OAK_ATTR_TARGET_FN,
                                 pinfo, explicit_arity, null, 0, (int)idx);
  if (pinfo)
    OAK_FREE(c->allocator, pinfo);

  oak_apply_runtime_attr_hook(c, fn_obj, null, attrs, attr_count);
  struct oak_registered_fn_t entry = {
    .name = name,
    .const_idx = idx,
    .arity = explicit_arity,
    .decl = item,
    .attrs = attrs,
    .attr_count = attr_count,
    .source_module_id = OAK_MODULE_ID_NONE,
  };
  if (!oak_compiler_declare_symbol(c, name_node->token, name,
                                   OAK_SYMBOL_FUNCTION,
                                   oak_dynarr_count(c->fns.entries),
                                   mid, 0))
    return;
  oak_fn_registry_insert(&c->fns, &entry);
}

void oak_register_method_on_record(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* raw_item,
                                    const struct oak_ast_node_t* item,
                                    struct oak_registered_record_t* sd)
{
  const struct oak_ast_node_t* name_node = oak_fn_name_node(item);
  const char* name = oak_token_text(name_node->token);
  const int len = (int)strlen(name);
  const int explicit_arity = oak_count_fn_params(item);
  const struct oak_ast_node_t* self_param =
      oak_fn_self_param(item);

  for (int i = 0; i < oak_dynarr_count(sd->methods); ++i)
  {
    const struct oak_registered_fn_t* e = &sd->methods[i];
    if (strcmp(e->name, name) == 0)
    {
      oak_compiler_error_at(c,
                            name_node->token,
                            "duplicate method '%s' on record '%s'",
                            name,
                            sd->name);
      return;
    }
  }

  int attr_count = 0;
  const char** attrs = oak_extract_attrs(c->allocator, raw_item, &attr_count);
  oak_compiler_dispatch_attr_cbs(c, attrs, attr_count, name, OAK_ATTR_TARGET_METHOD,
                                 null, 0, null, 0, -1);

  struct oak_registered_fn_t slot = { 0 };
  slot.name = name;
  slot.receiver_type_id = sd->type_id;
  oak_type_clear(&slot.return_type);
  slot.is_static = (self_param == null);
  slot.decl = item;
  slot.attrs = attrs;
  slot.attr_count = attr_count;
  slot.source_module_id = OAK_MODULE_ID_NONE;
  const int total_arity = self_param ? explicit_arity + 1 : explicit_arity;
  const u16 mid =
      c->current_module ? c->current_module->module_id : (u16)0xFFFFu;
  struct oak_obj_fn_t* fn_obj = oak_fn_new(c->allocator, 0, total_arity, mid);
  char* method_name_copy = OAK_ALLOC(c->allocator, len + 1u);
  memcpy(method_name_copy, name, len + 1u);
  fn_obj->name = method_name_copy;
  oak_apply_runtime_attr_hook(c, fn_obj, null, attrs, attr_count);
  slot.const_idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));
  slot.arity = total_arity;
  oak_assert(oak_dynarr_push(&sd->methods, &slot));
}

void oak_register_program_fns(
    struct oak_compiler_t* c, const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* raw_item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* item = oak_unwrap_decl(raw_item);
    if (!item || item->kind != OAK_NODE_FN_DECL)
      continue;
    register_regular_fn_decl(c, raw_item, item);
    if (c->has_error)
      return;
  }
}

void oak_register_program_methods(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* program)
{
  (void)c;
  (void)program;
}

const struct oak_registered_fn_t* oak_find_fn(
    struct oak_compiler_t* c, const char* name)
{
  return oak_fn_registry_find(&c->fns, name);
}
