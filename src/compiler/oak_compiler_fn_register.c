#include "internal/oak_compiler.h"

/* ---------- oak_fn_registry_t lifecycle ---------- */

void oak_fn_registry_init(struct oak_fn_registry_t* r,
                          struct oak_allocator_t* allocator)
{
  r->allocator = allocator;
  oak_htable_init(&r->by_name, allocator);
  oak_dynarr_init(&r->entries.items, &r->entries.count, &r->entries.capacity);
}

void oak_fn_registry_free(struct oak_fn_registry_t* r)
{
  for (int i = 0; i < r->entries.count; ++i)
  {
    if (r->entries.items[i].attrs)
      OAK_FREE(r->allocator, r->entries.items[i].attrs);
    if (r->entries.items[i].param_types)
      OAK_FREE(r->allocator, r->entries.items[i].param_types);
  }
  oak_htable_free(&r->by_name);
  oak_dynarr_free(r->allocator, &r->entries.items, &r->entries.count, &r->entries.capacity);
}

struct oak_registered_fn_t*
oak_fn_registry_insert(struct oak_fn_registry_t* r,
                       const struct oak_registered_fn_t* fn)
{
  oak_dynarr_push(r->allocator, &r->entries.items,
                  &r->entries.count,
                  &r->entries.capacity,
                  fn,
                  sizeof(*fn));
  const int idx = r->entries.count - 1;
  oak_htable_insert(&r->by_name,
                    r->entries.items[idx].name,
                    r->entries.items[idx].name_len,
                    idx);
  return &r->entries.items[idx];
}

const struct oak_registered_fn_t* oak_fn_registry_find(
    const struct oak_fn_registry_t* r, const char* name, usize len)
{
  const int idx = oak_htable_get(&r->by_name, name, len);
  if (idx < 0)
    return null;
  return &r->entries.items[idx];
}

/* ---------- Registration helpers ---------- */

/* lhs of RECORD_DECL: plain IDENT, or TYPE_NAME wrapping IDENT for arrays/maps.
 */
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
  const struct oak_ast_node_t* name_node = oakc_fn_name_node(item);
  const char* name = oak_token_text(name_node->token);
  const usize name_len = oak_token_length(name_node->token);
  const int explicit_arity = oakc_count_fn_params(item);
  const struct oak_ast_node_t* self_param =
      oakc_fn_self_param(item);

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

  if (oak_fn_registry_find(&c->fns, name, name_len))
  {
    oak_compiler_error_at(c, name_node->token, "duplicate function '%s'", name);
    return;
  }

  const u16 mid =
      c->current_module ? c->current_module->module_id : (u16)0xFFFFu;
  struct oak_obj_fn_t* fn_obj = oak_fn_new(c->allocator, 0, explicit_arity, mid);
  char* name_copy = OAK_ALLOC(c->allocator, name_len + 1u);
  memcpy(name_copy, name, name_len);
  name_copy[name_len] = 0;
  fn_obj->name = name_copy;
  const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));

  int attr_count = 0;
  const char** attrs = oakc_extract_attrs(c->allocator, raw_item, &attr_count);
  oakc_dispatch_compile_attr_cbs(c, attrs, attr_count, name, OAK_ATTR_TARGET_FN);
  oakc_apply_runtime_attr_hook(c, fn_obj, null, attrs, attr_count);
  struct oak_registered_fn_t entry = {
    .name = name,
    .name_len = name_len,
    .const_idx = idx,
    .arity = explicit_arity,
    .decl = item,
    .attrs = attrs,
    .attr_count = attr_count,
    .source_module_id = OAK_MODULE_ID_NONE,
  };
  oak_fn_registry_insert(&c->fns, &entry);
}

void oakc_register_method_on_record(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* raw_item,
                                    const struct oak_ast_node_t* item,
                                    struct oak_registered_record_t* sd)
{
  const struct oak_ast_node_t* name_node = oakc_fn_name_node(item);
  const char* name = oak_token_text(name_node->token);
  const usize name_len = oak_token_length(name_node->token);
  const int explicit_arity = oakc_count_fn_params(item);
  const struct oak_ast_node_t* self_param =
      oakc_fn_self_param(item);

  for (int i = 0; i < sd->methods.count; ++i)
  {
    const struct oak_registered_fn_t* e = &sd->methods.items[i];
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
  const char** attrs = oakc_extract_attrs(c->allocator, raw_item, &attr_count);
  oakc_dispatch_compile_attr_cbs(c, attrs, attr_count, name, OAK_ATTR_TARGET_METHOD);

  struct oak_registered_fn_t slot = { 0 };
  slot.name = name;
  slot.name_len = name_len;
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
  char* method_name_copy = OAK_ALLOC(c->allocator, name_len + 1u);
  memcpy(method_name_copy, name, name_len);
  method_name_copy[name_len] = 0;
  fn_obj->name = method_name_copy;
  oakc_apply_runtime_attr_hook(c, fn_obj, null, attrs, attr_count);
  slot.const_idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));
  slot.arity = total_arity;
  oak_dynarr_push(c->allocator, &sd->methods.items,
                  &sd->methods.count,
                  &sd->methods.capacity,
                  &slot,
                  sizeof(slot));
}

void oakc_register_program_fns(
    struct oak_compiler_t* c, const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* raw_item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* item = oakc_unwrap_decl(raw_item);
    if (!item || item->kind != OAK_NODE_FN_DECL)
      continue;
    register_regular_fn_decl(c, raw_item, item);
    if (c->has_error)
      return;
  }
}

void oakc_register_program_methods(struct oak_compiler_t* c,
                                           const struct oak_ast_node_t* program)
{
  (void)c;
  (void)program;
}

const struct oak_registered_fn_t* oakc_find_fn(
    struct oak_compiler_t* c, const char* name, const usize len)
{
  return oak_fn_registry_find(&c->fns, name, len);
}
