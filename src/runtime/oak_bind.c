#include "oak_bind.h"

#include "oak_allocator.h"
#include "oak_dynarr.h"
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

int oak_bind_field(struct oak_bind_type_t* type,
                   const struct oak_bind_field_t* p)
{
  if (!type || !p)
    return -1;
  if (!p->name || !p->getter)
    return -1;

  /* Reject duplicate field names. */
  for (int i = 0; i < type->field_count; ++i)
  {
    if (strcmp(type->fields[i].name, p->name) == 0)
      return -1;
  }

  const int name_len = oak_strlen(p->name);
  char* name_copy = OAK_ALLOC(type->allocator, name_len + 1u);
  if (!name_copy)
    return -1;
  for (int i = 0; i < name_len; ++i)
    name_copy[i] = p->name[i];
  name_copy[name_len] = 0;

  struct oak_bind_field_t f = {
    .name = name_copy,
    .field_type_id = p->field_type_id,
    .shape = p->shape,
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

int oak_bind_fn_global(struct oak_compile_options_t* opts,
                       const struct oak_bind_global_fn_t* p)
{
  if (!opts || !p)
    return -1;
  if (!p->name || !p->impl || p->arity < 0)
    return -1;
  if (p->return_shape != OAK_BIND_SHAPE_SCALAR &&
      p->return_shape != OAK_BIND_SHAPE_ARRAY)
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
  if (p->return_shape != OAK_BIND_SHAPE_SCALAR &&
      p->return_shape != OAK_BIND_SHAPE_ARRAY)
    return -1;
  if (p->kind != OAK_BIND_FN_INSTANCE_METHOD &&
      p->kind != OAK_BIND_FN_STATIC_METHOD)
    return -1;
  if (p->receiver_type_id == OAK_TYPE_VOID)
    return -1;

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
