#include "internal/oak_module_loader.h"

static int native_module_name_eq(const char* module_name, const char* dotted)
{
  return module_name && dotted && strcmp(module_name, dotted) == 0;
}

int opts_has_native_module(const oak_compile_options_t* opts,
                           const char* dotted)
{
  if (!opts || !dotted)
    return 0;
  const oak_bind_global_fn_t* global_fns =
      OAK_CDATA(oak_bind_global_fn_t, opts->native_global_fns);
  for (usize i = 0; i < oak_size(opts->native_global_fns); ++i)
    if (native_module_name_eq(global_fns[i].module_name, dotted))
      return 1;

  oak_bind_type_t** types =
      OAK_DATA(oak_bind_type_t*, opts->native_types);
  for (usize i = 0; i < oak_size(opts->native_types); ++i)
    if (types[i] && native_module_name_eq(types[i]->module_name, dotted))
      return 1;

  oak_bind_enum_t** enums =
      OAK_DATA(oak_bind_enum_t*, opts->native_enums);
  for (usize i = 0; i < oak_size(opts->native_enums); ++i)
    if (enums[i] && native_module_name_eq(enums[i]->module_name, dotted))
      return 1;

  return 0;
}

static char* native_canonical_path_dup(oak_allocator_t* a,
                                       const char* dotted)
{
  const char* prefix = "native:";
  const usize plen = strlen(prefix);
  const usize dlen = strlen(dotted);
  char* out = oak_alloc(a, plen + dlen + 1u, OAK_HERE);
  memcpy(out, prefix, plen);
  memcpy(out + plen, dotted, dlen);
  out[plen + dlen] = 0;
  return out;
}

static int native_type_in_module(const oak_bind_type_t* type,
                                 const char* dotted)
{
  return type && native_module_name_eq(type->module_name, dotted);
}

/* Value form of oak_lower_bind_ref, for the export structs built as compound
 * literals below. */
static oak_type_t native_ref_type(const oak_bind_type_ref_t* ref)
{
  oak_type_t type;
  oak_lower_bind_ref(ref, &type);
  return type;
}

void module_loader_filter_native_decls(
    const oak_compile_options_t* base_opts,
    const char* dotted,
    oak_compile_options_t* opts)
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

  opts->native_types =
      oak_vector_new(opts->allocator, sizeof(oak_bind_type_t*));
  opts->native_fns =
      oak_vector_new(opts->allocator, sizeof(oak_bind_fn_t));
  opts->native_global_fns =
      oak_vector_new(opts->allocator, sizeof(oak_bind_global_fn_t));
  opts->native_enums =
      oak_vector_new(opts->allocator, sizeof(oak_bind_enum_t*));
  oak_assert(opts->native_types && opts->native_fns &&
             opts->native_global_fns && opts->native_enums);

  oak_bind_type_t** base_types =
      OAK_DATA(oak_bind_type_t*, base_opts->native_types);
  for (usize i = 0; i < oak_size(base_opts->native_types); ++i)
  {
    oak_bind_type_t* type = base_types[i];
    if (is_native_module && type && native_module_name_eq(type->module_name, dotted))
      continue;
    oak_assert(oak_push_back(opts->native_types, &type));
  }

  const oak_bind_fn_t* base_fns =
      OAK_CDATA(oak_bind_fn_t, base_opts->native_fns);
  for (usize i = 0; i < oak_size(base_opts->native_fns); ++i)
  {
    const oak_bind_fn_t* fn = &base_fns[i];
    if (is_native_module && native_type_in_module(fn->receiver_type, dotted))
      continue;
    oak_assert(oak_push_back(opts->native_fns, fn));
  }

  const oak_bind_global_fn_t* base_global_fns =
      OAK_CDATA(oak_bind_global_fn_t, base_opts->native_global_fns);
  for (usize i = 0; i < oak_size(base_opts->native_global_fns); ++i)
  {
    const oak_bind_global_fn_t* fn = &base_global_fns[i];
    if (is_native_module && native_module_name_eq(fn->module_name, dotted))
      continue;
    oak_assert(oak_push_back(opts->native_global_fns, fn));
  }

  oak_bind_enum_t** base_enums =
      OAK_DATA(oak_bind_enum_t*, base_opts->native_enums);
  for (usize i = 0; i < oak_size(base_opts->native_enums); ++i)
  {
    oak_bind_enum_t* e = base_enums[i];
    if (is_native_module && e && native_module_name_eq(e->module_name, dotted))
      continue;
    oak_assert(oak_push_back(opts->native_enums, &e));
  }
}

void module_loader_free_filtered_native_decls(
    const oak_compile_options_t* base_opts,
    const char* dotted,
    oak_compile_options_t* opts)
{
  (void)base_opts;
  (void)dotted;
  /* Mirrors module_loader_filter_native_decls, which always allocates owned
   * copies of these vectors. Frees the copied item arrays (not the bound
   * structs they point at, which the embedder owns). */
  oak_destroy(opts->native_types);
  oak_destroy(opts->native_fns);
  oak_destroy(opts->native_global_fns);
  oak_destroy(opts->native_enums);
}

/* Hands every descriptor bound into this module its type id, taken from the
 * module's own registry.
 *
 * create_native_module does the same for a module with no Oak source. A module
 * that has a stub takes the ordinary compile path instead, and the compiler
 * deliberately skips bindings that name another module (they are reached
 * through `import`, not by being in scope), so nothing on that path ever
 * assigns them -- they would keep the OAK_TYPE_VOID that oak_bind_type_in_module
 * left behind, and every match against a stub declaration below would fail.
 *
 * Both passes run before any signature is lowered, because a parameter or
 * return type may name a record or enum declared later in the binding list. */
static void resolve_native_module_type_ids(
    oak_module_t* mod,
    const oak_compile_options_t* opts)
{
  oak_bind_type_t** types = OAK_DATA(oak_bind_type_t*, opts->native_types);
  for (usize i = 0; i < oak_size(opts->native_types); ++i)
  {
    oak_bind_type_t* type = types[i];
    if (!type || !native_module_name_eq(type->module_name, mod->dotted_name))
      continue;
    type->resolved_type_id = oak_type_registry_intern(&mod->types, type->name);
  }

  oak_bind_enum_t** enums = OAK_DATA(oak_bind_enum_t*, opts->native_enums);
  for (usize i = 0; i < oak_size(opts->native_enums); ++i)
  {
    oak_bind_enum_t* e = enums[i];
    if (!e || !native_module_name_eq(e->module_name, mod->dotted_name))
      continue;
    e->resolved_type_id = oak_type_registry_intern(&mod->types, e->name);
  }
}

void apply_native_module_function_exports(
    oak_module_t* mod,
    const oak_compile_options_t* opts)
{
  if (!mod || !mod->chunk || !opts || !opts_has_native_module(opts, mod->dotted_name))
    return;
  resolve_native_module_type_ids(mod, opts);
  const oak_bind_global_fn_t* global_fns =
      OAK_CDATA(oak_bind_global_fn_t, opts->native_global_fns);
  for (usize i = 0; i < oak_size(opts->native_global_fns); ++i)
  {
    const oak_bind_global_fn_t* fn = &global_fns[i];
    if (!native_module_name_eq(fn->module_name, mod->dotted_name))
      continue;
    const oak_symbol_t* symbol =
        oak_symbol_registry_find(&mod->exports, fn->name);
    if (!symbol || symbol->kind != OAK_SYMBOL_FUNCTION)
      continue;
    oak_obj_native_fn_t* native = oak_native_fn_new(
        mod->allocator, fn->impl, fn->arity, fn->name, fn->user_data);
    oak_module_export_fn_t* exp =
        oak_get(mod->exports.fns, (usize)symbol->payload_index);
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
      const usize old_arity = (usize)exp->arity;
      if (exp->param_types)
        oak_free(mod->allocator, exp->param_types, OAK_HERE);
      if (exp->param_mut_flags && fn->arity != old_arity)
      {
        oak_free(mod->allocator, exp->param_mut_flags, OAK_HERE);
        exp->param_mut_flags = null;
      }
      exp->param_types = oak_alloc(
          mod->allocator, (usize)fn->arity * sizeof(oak_type_t), OAK_HERE);
      for (usize pi = 0; pi < fn->arity; ++pi)
        oak_lower_bind_ref(&fn->param_types[pi], &exp->param_types[pi]);
      exp->arity = (int)fn->arity;
    }
    /* Otherwise the stub's parameter contract is authoritative; keep arity
     * consistent with the stub's param_types (never index past it). */
    else if (!exp->param_types || fn->arity == (usize)exp->arity)
      exp->arity = (int)fn->arity;
    oak_lower_bind_ref(&fn->return_type, &exp->return_type);
  }
  oak_module_export_record_t* records =
      OAK_DATA(oak_module_export_record_t, mod->exports.records);
  const oak_bind_fn_t* native_fns =
      OAK_CDATA(oak_bind_fn_t, opts->native_fns);
  for (usize ri = 0; ri < oak_size(mod->exports.records); ++ri)
  {
    oak_module_export_record_t* rec = &records[ri];
    const oak_type_id_t rec_type_id =
        oak_type_registry_intern(&mod->types, rec->name);
    oak_module_export_record_method_t* methods =
        OAK_DATA(oak_module_export_record_method_t, rec->methods);
    for (usize mi = 0; mi < oak_size(rec->methods); ++mi)
    {
      oak_module_export_record_method_t* me = &methods[mi];
      for (usize fi = 0; fi < oak_size(opts->native_fns); ++fi)
      {
        const oak_bind_fn_t* fn = &native_fns[fi];
        if (!fn->receiver_type ||
            fn->receiver_type->resolved_type_id != rec_type_id)
          continue;
        if (!native_type_in_module(fn->receiver_type, mod->dotted_name))
          continue;
        const int is_instance = !me->is_static;
        const oak_bind_fn_kind_t want_kind =
            is_instance ? OAK_BIND_FN_INSTANCE_METHOD
                        : OAK_BIND_FN_STATIC_METHOD;
        if (fn->kind != want_kind || strcmp(fn->name, me->name) != 0)
          continue;
        oak_obj_native_fn_t* native = oak_native_fn_new(
            mod->allocator, fn->impl, me->arity, fn->name, fn->user_data);
        native->self_type = fn->receiver_type;
        if (me->stub_attrs && me->stub_attr_count > 0)
          oak_apply_attr_hooks(
              opts, null, native, me->stub_attrs, me->stub_attr_count);
        me->const_idx =
            (u16)oak_chunk_add_constant(mod->chunk, OAK_VALUE_OBJ(&native->obj));
        oak_lower_bind_ref(&fn->return_type, &me->return_type);
        break;
      }
    }
  }
}

/* Strips attribute/export wrappers, returning the inner declaration node.
 * Returns the node unchanged if it has no wrapper. */
const oak_ast_node_t*
loader_unwrap_decl(const oak_ast_node_t* item)
{
  if (!item)
    return item;
  if (item->kind == OAK_NODE_EXPORT_DECL)
    return loader_unwrap_decl(item->child);
  if (item->kind != OAK_NODE_ATTR_DECL)
    return item;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &item->children)
  {
    const oak_ast_node_t* child =
        oak_container_of(pos, oak_ast_node_t, link);
    if (child->kind != OAK_NODE_ATTR)
      return loader_unwrap_decl(child);
  }
  return null;
}

static const oak_ast_node_t*
loader_fn_decl_name_node(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* proto = decl ? decl->lhs : null;
  const oak_ast_node_t* head = proto ? proto->lhs : null;
  return head ? head->rhs : null;
}

static const oak_ast_node_t*
loader_fn_decl_param_list(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* proto = decl ? decl->lhs : null;
  const oak_ast_node_t* tail = proto ? proto->rhs : null;
  return tail ? tail->lhs : null;
}

static int loader_fn_decl_has_self(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* plist = loader_fn_decl_param_list(decl);
  return plist && plist->lhs;
}

static usize loader_fn_decl_param_count(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* plist = loader_fn_decl_param_list(decl);
  if (!plist || !plist->rhs)
    return 0;
  return oak_list_length(&plist->rhs->children);
}

static int loader_fn_decl_is_bodyless(const oak_ast_node_t* decl)
{
  return decl && decl->rhs && decl->rhs->kind == OAK_NODE_FN_DECL_SEMICOLON;
}

/* Helpers for top-level METHOD_DECL nodes:
 *   METHOD_DECL (binary: lhs=METHOD_PROTO, rhs=FN_DECL_BODY)
 *     METHOD_PROTO (binary: lhs=METHOD_HEAD, rhs=FN_PARAMS_AND_RET)
 *       METHOD_HEAD (binary: lhs=type IDENT, rhs=method IDENT) */
static const oak_ast_node_t*
loader_method_decl_type_node(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* proto = decl ? decl->lhs : null;
  const oak_ast_node_t* head = proto ? proto->lhs : null;
  return head ? head->lhs : null;
}

static const oak_ast_node_t*
loader_method_decl_name_node(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* proto = decl ? decl->lhs : null;
  const oak_ast_node_t* head = proto ? proto->lhs : null;
  return head ? head->rhs : null;
}

static const oak_ast_node_t*
loader_method_decl_param_list(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* proto = decl ? decl->lhs : null;
  const oak_ast_node_t* params_ret = proto ? proto->rhs : null;
  return params_ret ? params_ret->lhs : null;
}

static int loader_method_decl_has_self(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* plist = loader_method_decl_param_list(decl);
  return plist && plist->lhs;
}

static usize loader_method_decl_param_count(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* plist = loader_method_decl_param_list(decl);
  if (!plist || !plist->rhs)
    return 0;
  return oak_list_length(&plist->rhs->children);
}

static const oak_ast_node_t*
loader_record_decl_name_node(const oak_ast_node_t* record_decl)
{
  const oak_ast_node_t* name = record_decl ? record_decl->lhs : null;
  if (name && name->kind == OAK_NODE_TYPE_NAME)
  {
    const oak_list_entry_t* first = name->children.next;
    if (first == &name->children)
      return null;
    name = oak_container_of(first, oak_ast_node_t, link);
  }
  return (name && name->kind == OAK_NODE_IDENT) ? name : null;
}

static const oak_bind_type_t*
find_native_type_decl(const oak_compile_options_t* opts,
                      const char* dotted,
                      const char* name)
{
  if (!opts)
    return null;
  oak_bind_type_t** types =
      OAK_DATA(oak_bind_type_t*, opts->native_types);
  for (usize i = 0; i < oak_size(opts->native_types); ++i)
  {
    const oak_bind_type_t* type = types[i];
    if (type && native_module_name_eq(type->module_name, dotted) &&
        strcmp(type->name, name) == 0)
      return type;
  }
  return null;
}

static int native_global_fn_decl_exists(const oak_compile_options_t* opts,
                                        const char* dotted,
                                        const char* name,
                                        usize arity)
{
  if (!opts)
    return 0;
  const oak_bind_global_fn_t* global_fns =
      OAK_CDATA(oak_bind_global_fn_t, opts->native_global_fns);
  for (usize i = 0; i < oak_size(opts->native_global_fns); ++i)
  {
    const oak_bind_global_fn_t* fn = &global_fns[i];
    if (native_module_name_eq(fn->module_name, dotted) &&
        strcmp(fn->name, name) == 0 && fn->arity == arity)
      return 1;
  }
  return 0;
}

static int native_method_decl_exists(const oak_compile_options_t* opts,
                                     const oak_bind_type_t* receiver,
                                     const char* name,
                                     int has_self,
                                     usize arity)
{
  if (!opts || !receiver)
    return 0;
  const oak_bind_fn_t* native_fns =
      OAK_CDATA(oak_bind_fn_t, opts->native_fns);
  for (usize i = 0; i < oak_size(opts->native_fns); ++i)
  {
    const oak_bind_fn_t* fn = &native_fns[i];
    const oak_bind_fn_kind_t want_kind =
        has_self ? OAK_BIND_FN_INSTANCE_METHOD : OAK_BIND_FN_STATIC_METHOD;
    if (fn->kind == want_kind && fn->receiver_type == receiver &&
        strcmp(fn->name, name) == 0 && fn->arity == arity)
      return 1;
  }
  return 0;
}

int validate_bodyless_native_decls(oak_module_loader_result_t* out,
                                   const oak_module_t* mod,
                                   const oak_compile_options_t* opts)
{
  if (!opts_has_native_module(opts, mod->dotted_name))
    return 1;
  const oak_ast_node_t* root = oak_parser_root(mod->parser);
  if (!root)
    return 1;
  int ok = 1;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &root->children)
  {
    const oak_ast_node_t* item =
        loader_unwrap_decl(oak_container_of(pos, oak_ast_node_t, link));
    if (!item)
      continue;
    if (item->kind == OAK_NODE_FN_DECL && loader_fn_decl_is_bodyless(item))
    {
      const oak_ast_node_t* name_node = loader_fn_decl_name_node(item);
      const char* name = oak_token_text(name_node->token);
      const usize arity = loader_fn_decl_param_count(item);
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
      const oak_ast_node_t* type_node = loader_method_decl_type_node(item);
      const oak_ast_node_t* name_node = loader_method_decl_name_node(item);
      if (!type_node || !name_node)
        continue;
      const char* type_name = oak_token_text(type_node->token);
      const char* name = oak_token_text(name_node->token);
      const int has_self = loader_method_decl_has_self(item);
      const usize arity = loader_method_decl_param_count(item);
      const oak_bind_type_t* receiver =
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
    const oak_ast_node_t* record_name_node =
        loader_record_decl_name_node(item);
    if (!record_name_node)
      continue;
    const char* record_name = oak_token_text(record_name_node->token);
    const oak_bind_type_t* receiver =
        find_native_type_decl(opts, mod->dotted_name, record_name);
    oak_list_entry_t* mpos;
    oak_list_for_each(mpos, &item->rhs->children)
    {
      const oak_ast_node_t* member =
          loader_unwrap_decl(oak_container_of(mpos, oak_ast_node_t, link));
      if (!member)
        continue;
      if (member->kind != OAK_NODE_FN_DECL || !loader_fn_decl_is_bodyless(member))
        continue;
      const oak_ast_node_t* name_node = loader_fn_decl_name_node(member);
      const char* name = oak_token_text(name_node->token);
      const int has_self = loader_fn_decl_has_self(member);
      const usize arity = loader_fn_decl_param_count(member);
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

oak_module_t* create_native_module(
    oak_module_registry_t* reg,
    const oak_compile_options_t* opts,
    const char* dotted,
    oak_module_loader_result_t* out)
{
  oak_allocator_t* a = reg->allocator;
  char* canonical = native_canonical_path_dup(a, dotted);
  oak_module_t* existing =
      oak_module_registry_find_by_path(reg, canonical);
  if (existing)
  {
    oak_free(a, canonical, OAK_HERE);
    return existing;
  }

  oak_module_t* mod =
      oak_module_registry_new(reg, canonical, dotted);
  oak_free(a, canonical, OAK_HERE);
  if (!mod)
  {
    loader_error(out, "out of memory creating native module '%s'", dotted);
    return null;
  }

  mod->chunk = oak_alloc(a, sizeof(oak_chunk_t), OAK_HERE);
  oak_chunk_init(mod->chunk, a);
  mod->chunk->module_id = mod->module_id;
  oak_type_registry_init(&mod->types, a);
  oak_type_registry_set_owner(&mod->types, mod->module_id);
  oak_bind_type_t** native_types =
      OAK_DATA(oak_bind_type_t*, opts->native_types);
  for (usize i = 0; i < oak_size(opts->native_types); ++i)
  {
    oak_bind_type_t* type = native_types[i];
    if (!type || !native_module_name_eq(type->module_name, dotted))
      continue;
    type->resolved_type_id = oak_type_registry_intern(&mod->types, type->name);
  }

  /* Intern enum type ids in the same early pass as the record types: a
   * function signature further down may reference an enum through
   * OAK_BIND_ENUM, and the lowering reads the id assigned here. The export
   * entries themselves are built later, alongside the other exports. */
  {
    oak_bind_enum_t** early_enums =
        OAK_DATA(oak_bind_enum_t*, opts->native_enums);
    for (usize i = 0; i < oak_size(opts->native_enums); ++i)
    {
      oak_bind_enum_t* e = early_enums[i];
      if (!e || !native_module_name_eq(e->module_name, dotted))
        continue;
      e->resolved_type_id = oak_type_registry_intern(&mod->types, e->name);
    }
  }

  const oak_bind_global_fn_t* global_fns =
      OAK_CDATA(oak_bind_global_fn_t, opts->native_global_fns);
  for (usize i = 0; i < oak_size(opts->native_global_fns); ++i)
  {
    const oak_bind_global_fn_t* fn = &global_fns[i];
    if (!native_module_name_eq(fn->module_name, dotted))
      continue;
    oak_obj_native_fn_t* native =
        oak_native_fn_new(a, fn->impl, fn->arity, fn->name, fn->user_data);
    const u16 const_idx =
        (u16)oak_chunk_add_constant(mod->chunk, OAK_VALUE_OBJ(&native->obj));
    oak_module_export_fn_t exp = {
      .name = fn->name,
      .const_idx = const_idx,
      .arity = (int)fn->arity,
      .return_type = native_ref_type(&fn->return_type),
    };
    /* Carry the parameter contract so imported calls are type-checked. */
    if (fn->param_types && fn->arity > 0)
    {
      exp.param_types =
          oak_alloc(a, (usize)fn->arity * sizeof(oak_type_t), OAK_HERE);
      for (usize pi = 0; pi < fn->arity; ++pi)
        oak_lower_bind_ref(&fn->param_types[pi], &exp.param_types[pi]);
    }
    oak_symbol_registry_insert_fn(
        &mod->exports, exp.name, mod->module_id, &exp);
  }

  for (usize i = 0; i < oak_size(opts->native_types); ++i)
  {
    const oak_bind_type_t* type = native_types[i];
    if (!type || !native_module_name_eq(type->module_name, dotted))
      continue;
    oak_module_export_record_t exp = { 0 };
    exp.name = type->name;
    exp.is_value = (type->kind == OAK_BIND_TYPE_VALUE);
    exp.fields = oak_vector_new(
        a, sizeof(oak_module_export_record_field_t));
    exp.methods = oak_vector_new(
        a, sizeof(oak_module_export_record_method_t));
    oak_assert(exp.fields && exp.methods);
    const oak_bind_field_t* type_fields =
        OAK_CDATA(oak_bind_field_t, type->fields);
    for (usize fi = 0; fi < oak_size(type->fields); ++fi)
    {
      oak_module_export_record_field_t field = {
        .name = type_fields[fi].name,
        .type = native_ref_type(&type_fields[fi].type),
      };
      oak_assert(oak_push_back(exp.fields, &field));
    }
    oak_symbol_registry_insert_record(
        &mod->exports, exp.name, mod->module_id, &exp);
  }

  oak_bind_enum_t** native_enums =
      OAK_DATA(oak_bind_enum_t*, opts->native_enums);
  for (usize i = 0; i < oak_size(opts->native_enums); ++i)
  {
    oak_bind_enum_t* e = native_enums[i];
    if (!e || !native_module_name_eq(e->module_name, dotted))
      continue;
    /* The type ID was interned in the early pass above, exactly as the native
     * types get one. Importers resolve an exported enum by looking its name up
     * in the registry; without that entry the lookup fails and the import is
     * rejected ("failed to register imported enum"). */
    oak_module_export_enum_t exp = { 0 };
    exp.name = e->name;
    exp.variants = oak_vector_new(
        a, sizeof(oak_module_export_enum_variant_t));
    oak_assert(exp.variants);
    const oak_bind_enum_variant_t* variants =
        OAK_CDATA(oak_bind_enum_variant_t, e->variants);
    for (usize vi = 0; vi < oak_size(e->variants); ++vi)
    {
      oak_module_export_enum_variant_t variant = {
        .name = variants[vi].name,
        .value = variants[vi].value,
      };
      oak_assert(oak_push_back(exp.variants, &variant));
    }
    oak_symbol_registry_insert_enum(
        &mod->exports, exp.name, mod->module_id, &exp);
  }

  mod->state = OAK_MOD_COMPILED;
  return mod;
}
