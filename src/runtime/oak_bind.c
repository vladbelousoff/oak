#include "oak_bind.h"

#include "oak_allocator.h"
#include "oak_dynarr.h"
#include "internal/oak_generic_registry.h"
#include "oak_log.h"
#include "oak_str.h"
#include "oak_type.h"
#include "oak_value.h"

#include <string.h>

/* ---------- Compile-options lifecycle ---------- */

void oak_compile_options_init(struct oak_compile_options_t* opts,
                             struct oak_allocator_t* allocator)
{
  if (!opts)
    return;
  opts->allocator = allocator;
  opts->source_name = null;
  oak_dynarr_init(&opts->native_types.items,
                  &opts->native_types.count,
                  &opts->native_types.capacity);
  oak_dynarr_init(&opts->native_fns.items,
                  &opts->native_fns.count,
                  &opts->native_fns.capacity);
  oak_dynarr_init(&opts->native_global_fns.items,
                  &opts->native_global_fns.count,
                  &opts->native_global_fns.capacity);
  oak_dynarr_init(&opts->native_enums.items,
                  &opts->native_enums.count,
                  &opts->native_enums.capacity);
  oak_dynarr_init(&opts->native_attrs.items,
                  &opts->native_attrs.count,
                  &opts->native_attrs.capacity);
  opts->next_type_id = OAK_TYPE_FIRST_USER;
  opts->emit_debug_info = 1;
  opts->module_registry = null;
  opts->current_module = null;
}

void oak_compile_options_free(struct oak_compile_options_t* opts)
{
  if (!opts)
    return;
  for (int i = 0; i < opts->native_types.count; ++i)
  {
    for (int fi = 0; fi < opts->native_types.items[i]->field_count; ++fi)
      OAK_FREE(opts->allocator, (void*)opts->native_types.items[i]->fields[fi].name);
    oak_dynarr_free(opts->allocator, &opts->native_types.items[i]->fields,
                    &opts->native_types.items[i]->field_count,
                    &opts->native_types.items[i]->field_capacity);
    OAK_FREE(opts->allocator, opts->native_types.items[i]);
  }
  oak_dynarr_free(opts->allocator, &opts->native_types.items,
                  &opts->native_types.count,
                  &opts->native_types.capacity);
  oak_dynarr_free(opts->allocator, &opts->native_fns.items,
                  &opts->native_fns.count,
                  &opts->native_fns.capacity);
  oak_dynarr_free(opts->allocator, &opts->native_global_fns.items,
                  &opts->native_global_fns.count,
                  &opts->native_global_fns.capacity);
  for (int i = 0; i < opts->native_enums.count; ++i)
  {
    oak_dynarr_free(opts->allocator, &opts->native_enums.items[i]->variants,
                    &opts->native_enums.items[i]->variant_count,
                    &opts->native_enums.items[i]->variant_capacity);
    OAK_FREE(opts->allocator, opts->native_enums.items[i]);
  }
  oak_dynarr_free(opts->allocator, &opts->native_enums.items,
                  &opts->native_enums.count,
                  &opts->native_enums.capacity);
  oak_dynarr_free(opts->allocator, &opts->native_attrs.items,
                  &opts->native_attrs.count,
                  &opts->native_attrs.capacity);
  opts->next_type_id = OAK_TYPE_FIRST_USER;
}

/* ---------- Binding API ---------- */

struct oak_bind_type_t* oak_bind_type(struct oak_compile_options_t* opts,
                                      const enum oak_bind_type_kind_t kind,
                                      const char* name)
{
  return oak_bind_type_in_module(opts, null, kind, name);
}

struct oak_bind_type_t* oak_bind_type_in_module(
    struct oak_compile_options_t* opts,
    const char* module_name,
    const enum oak_bind_type_kind_t kind,
    const char* name)
{
  if (!opts || !name)
    return null;

  struct oak_bind_type_t* t =
      OAK_ALLOC(opts->allocator, sizeof(struct oak_bind_type_t));
  t->module_name = module_name;
  t->kind = kind;
  t->name = name;
  t->type_arg_name = null;
  t->type_id = opts->next_type_id++;
  oak_dynarr_init(&t->fields, &t->field_count, &t->field_capacity);
  t->destructor = null;
  t->allocator = opts->allocator;

  oak_dynarr_push(opts->allocator, &opts->native_types.items,
                  &opts->native_types.count,
                  &opts->native_types.capacity,
                  &t,
                  sizeof(t));
  return t;
}

static int validate_type_ref(const struct oak_bind_type_ref_t* r, int gpc);

int oak_bind_field(struct oak_bind_type_t* type,
                   const struct oak_bind_field_t* p)
{
  if (!type || !p)
    return -1;
  if (!p->name || !p->getter)
    return -1;
  /* Inline value types have no runtime type identity, so field access cannot
   * be dispatched on them; they expose data through methods only. */
  if (type->kind == OAK_BIND_TYPE_VALUE)
    return -1;
  /* Native records are not specialized per type argument, so field types must
   * be concrete; a type-parameter field would behave as an unconstrained
   * wildcard in type checking.  Reject any OAK_BIND_PARAM reference. */
  if (p->type.id >= OAK_TYPE_PARAM_BASE || p->type.key_id >= OAK_TYPE_PARAM_BASE)
    return -1;

  /* Reject duplicate field names. */
  for (int i = 0; i < type->field_count; ++i)
  {
    if (strcmp(type->fields[i].name, p->name) == 0)
      return -1;
  }

  const int name_len = (int)strlen(p->name);
  char* name_copy = OAK_ALLOC(type->allocator, name_len + 1u);
  if (!name_copy)
    return -1;
  for (int i = 0; i < name_len; ++i)
    name_copy[i] = p->name[i];
  name_copy[name_len] = 0;

  struct oak_bind_field_t f = {
    .name = name_copy,
    .type = p->type,
    .getter = p->getter,
    .setter = p->setter,
  };
  oak_dynarr_push(type->allocator, &type->fields,
                  &type->field_count,
                  &type->field_capacity,
                  &f,
                  sizeof(f));
  return 0;
}

/* Validate a generic type-parameter name list. */
static int validate_generic_params(const char* const* names, int count)
{
  if (count < 0 || count > OAK_MAX_GENERIC_PARAMS)
    return -1;
  if (count > 0 && !names)
    return -1;
  for (int i = 0; i < count; ++i)
    if (!names[i] || !names[i][0])
      return -1;
  return 0;
}

/* Validate that any type-parameter reference inside `r` (including a map key)
 * indexes a declared generic parameter in [0, gpc). */
static int validate_type_ref(const struct oak_bind_type_ref_t* r, int gpc)
{
  if (r->id >= OAK_TYPE_PARAM_BASE)
  {
    const int idx = (int)(r->id - OAK_TYPE_PARAM_BASE);
    if (idx < 0 || idx >= gpc)
      return -1;
  }
  if (r->kind == OAK_TYPE_KIND_MAP && r->key_id >= OAK_TYPE_PARAM_BASE)
  {
    const int idx = (int)(r->key_id - OAK_TYPE_PARAM_BASE);
    if (idx < 0 || idx >= gpc)
      return -1;
  }
  return 0;
}

/* Mark every type parameter referenced by `r` (value and map key). */
static void mark_ref_params(const struct oak_bind_type_ref_t* r, int* used,
                            int gpc)
{
  if (r->id >= OAK_TYPE_PARAM_BASE)
  {
    const int idx = (int)(r->id - OAK_TYPE_PARAM_BASE);
    if (idx >= 0 && idx < gpc)
      used[idx] = 1;
  }
  if (r->kind == OAK_TYPE_KIND_MAP && r->key_id >= OAK_TYPE_PARAM_BASE)
  {
    const int idx = (int)(r->key_id - OAK_TYPE_PARAM_BASE);
    if (idx >= 0 && idx < gpc)
      used[idx] = 1;
  }
}

/* Every type parameter named in the return type must be inferable from a
 * parameter type — there is no receiver specialization, so a return-only `T`
 * could never be resolved and would leak a wildcard type into callers. */
static int return_params_inferable(const struct oak_bind_type_ref_t* return_type,
                                   const struct oak_bind_type_ref_t* param_types,
                                   int param_count,
                                   int gpc)
{
  int from_params[OAK_MAX_GENERIC_PARAMS] = { 0 };
  if (param_types)
    for (int i = 0; i < param_count; ++i)
      mark_ref_params(&param_types[i], from_params, gpc);

  int in_return[OAK_MAX_GENERIC_PARAMS] = { 0 };
  mark_ref_params(return_type, in_return, gpc);
  for (int i = 0; i < gpc; ++i)
    if (in_return[i] && !from_params[i])
      return 0;
  return 1;
}

/* Validate the generic descriptor and all type refs of a function binding.
 * `arity` is the user-facing parameter count (self excluded for methods). */
static int validate_fn_generics(const char* const* generic_params,
                                int generic_param_count,
                                const struct oak_bind_type_ref_t* return_type,
                                const struct oak_bind_type_ref_t* param_types,
                                int param_count,
                                int arity)
{
  if (validate_generic_params(generic_params, generic_param_count) != 0)
    return -1;
  /* A generic binding with parameters must declare their types, otherwise the
   * generic argument validator silently skips type checking for every call. */
  if (generic_param_count > 0 && arity > 0 && !param_types)
    return -1;
  if (validate_type_ref(return_type, generic_param_count) != 0)
    return -1;
  if (param_types)
    for (int i = 0; i < param_count; ++i)
      if (validate_type_ref(&param_types[i], generic_param_count) != 0)
        return -1;
  if (!return_params_inferable(return_type, param_types, param_count,
                               generic_param_count))
    return -1;
  return 0;
}

int oak_bind_fn_global(struct oak_compile_options_t* opts,
                       const struct oak_bind_global_fn_t* p)
{
  if (!opts || !p)
    return -1;
  if (!p->name || !p->impl || p->arity < 0)
    return -1;
  if (p->param_types && p->param_count != p->arity)
    return -1;
  if (validate_fn_generics(p->generic_params, p->generic_param_count,
                           &p->return_type, p->param_types, p->param_count,
                           p->arity) != 0)
    return -1;
  /* The native module export ABI has no representation for generic signatures,
   * so module-scoped native functions may not be generic. */
  if (p->module_name && p->generic_param_count > 0)
    return -1;
  struct oak_bind_global_fn_t entry = *p;
  oak_dynarr_push(opts->allocator, &opts->native_global_fns.items,
                  &opts->native_global_fns.count,
                  &opts->native_global_fns.capacity,
                  &entry,
                  sizeof(entry));
  return 0;
}

int oak_bind_fn(struct oak_compile_options_t* opts,
                const struct oak_bind_fn_t* p)
{
  if (!opts || !p)
    return -1;
  if (!p->name || !p->impl || p->arity < 0)
    return -1;
  if (p->param_types && p->param_count != p->arity)
    return -1;
  if (p->kind != OAK_BIND_FN_INSTANCE_METHOD &&
      p->kind != OAK_BIND_FN_STATIC_METHOD)
    return -1;
  if (p->receiver_type_id == OAK_TYPE_VOID)
    return -1;
  if (validate_fn_generics(p->generic_params, p->generic_param_count,
                           &p->return_type, p->param_types, p->param_count,
                           p->arity) != 0)
    return -1;
  /* The native module export ABI cannot represent generic method signatures,
   * so a generic method may not target a module-scoped receiver type. */
  if (p->generic_param_count > 0)
  {
    for (int i = 0; i < opts->native_types.count; ++i)
    {
      const struct oak_bind_type_t* t = opts->native_types.items[i];
      if (t && t->type_id == p->receiver_type_id && t->module_name)
        return -1;
    }
  }

  struct oak_bind_fn_t copy = *p;
  oak_dynarr_push(opts->allocator, &opts->native_fns.items,
                  &opts->native_fns.count,
                  &opts->native_fns.capacity,
                  &copy,
                  sizeof(copy));
  return 0;
}

struct oak_bind_enum_t* oak_bind_enum(struct oak_compile_options_t* opts,
                                      const char* name)
{
  return oak_bind_enum_in_module(opts, null, name);
}

struct oak_bind_enum_t* oak_bind_enum_in_module(
    struct oak_compile_options_t* opts,
    const char* module_name,
    const char* name)
{
  if (!opts || !name)
    return null;

  struct oak_bind_enum_t* e =
      OAK_ALLOC(opts->allocator, sizeof(struct oak_bind_enum_t));
  e->module_name = module_name;
  e->name = name;
  oak_dynarr_init(&e->variants, &e->variant_count, &e->variant_capacity);
  e->allocator = opts->allocator;

  oak_dynarr_push(opts->allocator, &opts->native_enums.items,
                  &opts->native_enums.count,
                  &opts->native_enums.capacity,
                  &e,
                  sizeof(e));
  return e;
}

int oak_bind_enum_variant(struct oak_bind_enum_t* e,
                          const char* name,
                          const int value)
{
  if (!e || !name)
    return -1;

  for (int i = 0; i < e->variant_count; ++i)
  {
    if (strcmp(e->variants[i].name, name) == 0)
      return -1;
  }

  struct oak_bind_enum_variant_t v = {
    .name = name,
    .value = value,
  };
  oak_dynarr_push(e->allocator, &e->variants,
                  &e->variant_count,
                  &e->variant_capacity,
                  &v,
                  sizeof(v));
  return 0;
}

/* ---------- Runtime helpers ---------- */

struct oak_value_t oak_native_record_new(struct oak_allocator_t* allocator,
                                         const struct oak_bind_type_t* type,
                                         void* instance)
{
  oak_assert(type != null);
  struct oak_obj_native_record_t* ns =
      oak_obj_native_record_new(allocator, type, instance);
  return OAK_VALUE_OBJ(&ns->obj);
}

void* oak_native_instance(const struct oak_value_t value)
{
  return oak_as_native_record(value)->instance;
}

struct oak_value_t oak_native_value_new(void* payload)
{
  return oak_value_native(payload);
}

void* oak_native_value(const struct oak_value_t value)
{
  return oak_as_native_value(value);
}

void oak_dispatch_compile_attr_cbs(const struct oak_compile_options_t* opts,
                                   const char** attrs,
                                   int attr_count,
                                   const char* decl_name,
                                   enum oak_attr_target_t target,
                                   const struct oak_attr_param_info_t* params,
                                   int param_count,
                                   const struct oak_attr_field_info_t* fields,
                                   int field_count,
                                   int const_index)
{
  if (!opts || opts->native_attrs.count == 0 || attr_count == 0)
    return;
  for (int bi = 0; bi < opts->native_attrs.count; ++bi)
  {
    const struct oak_bind_attr_t* b = &opts->native_attrs.items[bi];
    if (!b->on_decl)
      continue;
    for (int ai = 0; ai < attr_count; ++ai)
    {
      if (strcmp(b->name, attrs[ai]) == 0)
      {
        struct oak_attr_compile_ctx_t ctx = {
          .target = target,
          .decl_name = decl_name,
          .user_data = b->user_data,
          .param_count = param_count,
          .params = params,
          .field_count = field_count,
          .fields = fields,
          .const_index = const_index,
        };
        b->on_decl(&ctx);
        break;
      }
    }
  }
}

void oak_apply_attr_hooks(const struct oak_compile_options_t* opts,
                          struct oak_obj_fn_t* fn_obj,
                          struct oak_obj_native_fn_t* native_obj,
                          const char** attrs,
                          int attr_count)
{
  if (!opts || opts->native_attrs.count == 0 || attr_count == 0)
    return;

  int match_count = 0;
  for (int bi = 0; bi < opts->native_attrs.count; ++bi)
  {
    const struct oak_bind_attr_t* b = &opts->native_attrs.items[bi];
    if (!b->on_call)
      continue;
    for (int ai = 0; ai < attr_count; ++ai)
    {
      if (strcmp(b->name, attrs[ai]) == 0)
      {
        ++match_count;
        break;
      }
    }
  }
  if (match_count == 0)
    return;

  struct oak_allocator_t* a = fn_obj ? fn_obj->obj.allocator
                             : native_obj ? native_obj->obj.allocator
                             : null;
  if (!a)
    return;
  struct oak_attr_hook_entry_t* hooks = OAK_ALLOC(a,
      (usize)match_count * sizeof(struct oak_attr_hook_entry_t));
  int idx = 0;
  for (int bi = 0; bi < opts->native_attrs.count; ++bi)
  {
    const struct oak_bind_attr_t* b = &opts->native_attrs.items[bi];
    if (!b->on_call)
      continue;
    for (int ai = 0; ai < attr_count; ++ai)
    {
      if (strcmp(b->name, attrs[ai]) == 0)
      {
        hooks[idx].cb = b->on_call;
        hooks[idx].ud = b->user_data;
        ++idx;
        break;
      }
    }
  }

  if (fn_obj)
  {
    fn_obj->attr_hooks = hooks;
    fn_obj->attr_hook_count = match_count;
  }
  else if (native_obj)
  {
    native_obj->attr_hooks = hooks;
    native_obj->attr_hook_count = match_count;
  }
  else
  {
    OAK_FREE(a, hooks);
  }
}

int oak_bind_attr(struct oak_compile_options_t* opts,
                  const struct oak_bind_attr_t* params)
{
  if (!opts || !params || !params->name)
    return -1;
  oak_dynarr_push(opts->allocator, &opts->native_attrs.items,
                  &opts->native_attrs.count,
                  &opts->native_attrs.capacity,
                  params,
                  sizeof(*params));
  return 0;
}
