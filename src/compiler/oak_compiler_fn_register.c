#include "internal/oak_compiler.h"


void oak_fn_registry_init(oak_fn_registry_t* r,
                          oak_allocator_t* allocator)
{
  r->allocator = allocator;
  r->by_name = oak_hash_map_new(allocator, sizeof(usize));
  r->entries = oak_vector_new(allocator, sizeof(oak_registered_fn_t));
  oak_assert(r->by_name && r->entries);
}

void oak_fn_registry_free(oak_fn_registry_t* r)
{
  oak_registered_fn_t* entries =
      OAK_DATA(oak_registered_fn_t, r->entries);
  for (usize i = 0; i < oak_size(r->entries); ++i)
  {
    if (entries[i].attrs)
      oak_free(r->allocator, entries[i].attrs, OAK_HERE);
    if (entries[i].param_types)
      oak_free(r->allocator, entries[i].param_types, OAK_HERE);
    if (entries[i].param_mut_flags)
      oak_free(r->allocator, entries[i].param_mut_flags, OAK_HERE);
  }
  oak_destroy(r->by_name);
  oak_destroy(r->entries);
}

oak_registered_fn_t*
oak_fn_registry_insert(oak_fn_registry_t* r,
                       const oak_registered_fn_t* fn)
{
  oak_assert(oak_push_back(r->entries, fn));
  const usize idx = oak_size(r->entries) - 1;
  oak_registered_fn_t* entry = oak_get(r->entries, idx);
  oak_assert(oak_put_str(r->by_name, entry->name, &idx));
  return entry;
}

const oak_registered_fn_t* oak_fn_registry_find(
    const oak_fn_registry_t* r, const char* name)
{
  const usize* idx = oak_cfind_str(r->by_name, name);
  return idx ? oak_cget(r->entries, *idx) : null;
}


static const oak_ast_node_t*
record_decl_type_ident(const oak_ast_node_t* record_decl)
{
  if (!record_decl->lhs)
    return null;
  const oak_ast_node_t* name_ident = record_decl->lhs;
  /* RECORD_DECL_HEADER is transparent, so lhs is either a bare TYPE_NAME or a
   * HEADER_IMPL whose own lhs is the TYPE_NAME. */
  if (name_ident->kind == OAK_NODE_RECORD_DECL_HEADER_IMPL)
    name_ident = name_ident->lhs;
  if (name_ident && name_ident->kind == OAK_NODE_TYPE_NAME)
  {
    const oak_list_entry_t* tn_first = name_ident->children.next;
    if (tn_first == &name_ident->children)
      return null;
    name_ident = oak_container_of(tn_first, oak_ast_node_t, link);
  }
  if (!name_ident || name_ident->kind != OAK_NODE_IDENT)
    return null;
  return name_ident;
}

static void register_regular_fn_decl(oak_compiler_t* c,
                                     const oak_ast_node_t* raw_item,
                                     const oak_ast_node_t* item)
{
  const oak_ast_node_t* name_node = oak_fn_name_node(item);
  const char* name = oak_token_text(name_node->token);
  const int len = (int)strlen(name);
  const int explicit_arity = oak_count_fn_params(item);
  const oak_ast_node_t* mode = oak_fn_receiver_mode(item);

  if (mode)
  {
    /* Both markers describe a receiver, and a free function has none. */
    if (mode->kind == OAK_NODE_STATIC_KEYWORD)
      oak_compiler_error_at(c,
                            mode->token,
                            "'static' is only valid on a method declared "
                            "inside a record; '%s' is a free function",
                            name);
    else
      oak_compiler_error_at(c,
                            mode->token,
                            "'mut' after 'fn' declares the receiver mutable, "
                            "and free function '%s' has no receiver",
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
  oak_obj_fn_t* fn_obj = oak_fn_new(c->allocator, 0, explicit_arity, mid);
  char* name_copy = oak_alloc(c->allocator, len + 1u, OAK_HERE);
  memcpy(name_copy, name, len + 1u);
  fn_obj->name = name_copy;
  const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));

  int attr_count = 0;
  const char** attrs = oak_extract_attrs(c->allocator, raw_item, &attr_count);

  oak_attr_param_info_t* pinfo = null;
  if (attr_count > 0 && explicit_arity > 0)
  {
    pinfo = oak_alloc(c->allocator,
                      (usize)explicit_arity * sizeof(oak_attr_param_info_t),
                      OAK_HERE);
    for (int pi = 0; pi < explicit_arity; ++pi)
    {
      const oak_ast_node_t* param = oak_fn_param_at(item, pi);
      const oak_ast_node_t* id_node = oak_fn_param_ident(param);
      const oak_ast_node_t* ty_node = oak_fn_param_type_node(param);
      pinfo[pi].name = id_node ? oak_token_text(id_node->token) : "";
      pinfo[pi].is_mut = oak_param_is_mut(param);
      pinfo[pi].is_weak = 0;
      pinfo[pi].type_name = "";
      pinfo[pi].type_id = -1;
      if (ty_node)
      {
        const oak_ast_node_t* resolved = ty_node;
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
    oak_free(c->allocator, pinfo, OAK_HERE);

  oak_apply_runtime_attr_hook(c, fn_obj, null, attrs, attr_count);
  oak_registered_fn_t entry = {
    .name = name,
    .const_idx = idx,
    .arity = explicit_arity,
    .is_exported = oak_decl_is_exported(raw_item),
    .decl = item,
    .attrs = attrs,
    .attr_count = attr_count,
    .source_module_id = OAK_MODULE_ID_NONE,
  };
  if (!oak_compiler_declare_symbol(c, name_node->token, name,
                                   OAK_SYMBOL_FUNCTION,
                                   (int)oak_size(c->fns.entries),
                                   mid, 0))
    return;
  oak_fn_registry_insert(&c->fns, &entry);
  if (entry.is_exported)
    oak_compiler_mark_symbol_exported(c, name);
}

void oak_register_method_on_record(oak_compiler_t* c,
                                    const oak_ast_node_t* raw_item,
                                    const oak_ast_node_t* item,
                                    oak_registered_record_t* sd)
{
  const oak_ast_node_t* name_node = oak_fn_name_node(item);
  const char* name = oak_token_text(name_node->token);
  const int len = (int)strlen(name);
  const int explicit_arity = oak_count_fn_params(item);
  const int is_static = oak_fn_is_static(item);

  const oak_registered_fn_t* methods =
      OAK_CDATA(oak_registered_fn_t, sd->methods);
  for (usize i = 0; i < oak_size(sd->methods); ++i)
  {
    const oak_registered_fn_t* e = &methods[i];
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

  oak_registered_fn_t slot = { 0 };
  slot.name = name;
  slot.receiver_type_id = sd->type_id;
  oak_type_clear(&slot.return_type);
  slot.is_static = is_static;
  slot.is_exported = oak_decl_is_exported(raw_item);
  slot.decl = item;
  slot.attrs = attrs;
  slot.attr_count = attr_count;
  slot.source_module_id = OAK_MODULE_ID_NONE;
  /* Arity counts the receiver; the parameter list no longer does. */
  const int total_arity = is_static ? explicit_arity : explicit_arity + 1;
  const u16 mid =
      c->current_module ? c->current_module->module_id : (u16)0xFFFFu;
  oak_obj_fn_t* fn_obj = oak_fn_new(c->allocator, 0, total_arity, mid);
  char* method_name_copy = oak_alloc(c->allocator, len + 1u, OAK_HERE);
  memcpy(method_name_copy, name, len + 1u);
  fn_obj->name = method_name_copy;
  oak_apply_runtime_attr_hook(c, fn_obj, null, attrs, attr_count);
  slot.const_idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));
  slot.arity = total_arity;
  oak_assert(oak_push_back(sd->methods, &slot));
}

void oak_register_program_fns(
    oak_compiler_t* c, const oak_ast_node_t* program)
{
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const oak_ast_node_t* raw_item =
        oak_container_of(pos, oak_ast_node_t, link);
    const oak_ast_node_t* item = oak_unwrap_decl(raw_item);
    if (!item || item->kind != OAK_NODE_FN_DECL)
      continue;
    register_regular_fn_decl(c, raw_item, item);
    if (c->has_error)
      return;
  }
}

/* Register the methods in one record body against the registry entry the
 * record pass already created for it. */
static void register_record_body_methods(oak_compiler_t* c,
                                         const oak_ast_node_t* record_decl,
                                         oak_registered_record_t* sd)
{
  /* RECORD_DECL is BINARY: rhs = RECORD_MEMBERS. A bodyless `record P;` is
   * RECORD_DECL_EMPTY and has no member list to walk. */
  if (record_decl->kind != OAK_NODE_RECORD_DECL || !record_decl->rhs)
    return;

  oak_list_entry_t* pos;
  oak_list_for_each(pos, &record_decl->rhs->children)
  {
    const oak_ast_node_t* raw_member =
        oak_container_of(pos, oak_ast_node_t, link);
    const oak_ast_node_t* member = oak_unwrap_decl(raw_member);
    if (!member)
      continue;
    /* Fields were handled by the record pass. */
    if (member->kind == OAK_NODE_RECORD_FIELD_DECL)
      continue;
    if (member->kind != OAK_NODE_FN_DECL)
    {
      /* The `export` and `@Attr` wrappers accept any declaration, so a nested
         record or enum parses here and has to be turned away by name. Only a
         terminal node carries a token, and none of these do, so the record's
         own name is the closest honest location. */
      oak_compiler_error_at(c,
                            sd->decl_token,
                            "a record body holds only fields and methods; "
                            "move this declaration out of record '%s'",
                            sd->name);
      return;
    }
    oak_register_method_on_record(c, raw_member, member, sd);
    if (c->has_error)
      return;
  }
}

void oak_register_program_methods(oak_compiler_t* c,
                                           const oak_ast_node_t* program)
{
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const oak_ast_node_t* raw_item =
        oak_container_of(pos, oak_ast_node_t, link);
    const oak_ast_node_t* item = oak_unwrap_decl(raw_item);
    if (!item || item->kind != OAK_NODE_RECORD_DECL)
      continue;

    const oak_ast_node_t* type_ident = record_decl_type_ident(item);
    if (!type_ident)
      continue; /* the record pass already reported the malformed header */

    const char* rname = oak_token_text(type_ident->token);
    oak_registered_record_t* records =
        OAK_DATA(oak_registered_record_t, c->records.entries);
    for (usize i = 0; i < oak_size(c->records.entries); ++i)
    {
      if (oak_name_eq(records[i].name, rname))
      {
        register_record_body_methods(c, item, &records[i]);
        break;
      }
    }
    if (c->has_error)
      return;
  }
}

const oak_registered_fn_t* oak_find_fn(
    oak_compiler_t* c, const char* name)
{
  return oak_fn_registry_find(&c->fns, name);
}
