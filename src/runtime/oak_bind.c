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
  oak_assert(oak_dynarr_init(opts->allocator, &opts->native_types, sizeof *opts->native_types));
  oak_assert(oak_dynarr_init(opts->allocator, &opts->native_fns, sizeof *opts->native_fns));
  oak_assert(oak_dynarr_init(opts->allocator, &opts->native_global_fns, sizeof *opts->native_global_fns));
  oak_assert(oak_dynarr_init(opts->allocator, &opts->native_enums, sizeof *opts->native_enums));
  oak_assert(oak_dynarr_init(opts->allocator, &opts->native_attrs, sizeof *opts->native_attrs));
  opts->next_type_id = OAK_TYPE_FIRST_USER;
  opts->emit_debug_info = 1;
  opts->module_registry = null;
  opts->current_module = null;
}

void oak_compile_options_free(struct oak_compile_options_t* opts)
{
  if (!opts)
    return;
  for (int i = 0; i < oak_dynarr_count(opts->native_types); ++i)
  {
    for (int fi = 0; fi < oak_dynarr_count(opts->native_types[i]->fields); ++fi)
      OAK_FREE(opts->allocator, (void*)opts->native_types[i]->fields[fi].name);
    oak_dynarr_free(&opts->native_types[i]->fields);
    OAK_FREE(opts->allocator, opts->native_types[i]);
  }
  oak_dynarr_free(&opts->native_types);
  oak_dynarr_free(&opts->native_fns);
  oak_dynarr_free(&opts->native_global_fns);
  for (int i = 0; i < oak_dynarr_count(opts->native_enums); ++i)
  {
    oak_dynarr_free(&opts->native_enums[i]->variants);
    OAK_FREE(opts->allocator, opts->native_enums[i]);
  }
  oak_dynarr_free(&opts->native_enums);
  oak_dynarr_free(&opts->native_attrs);
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
  t->allocator = opts->allocator;
  oak_assert(oak_dynarr_init(t->allocator, &t->fields, sizeof *t->fields));
  t->destructor = null;

  oak_assert(oak_dynarr_push(&opts->native_types, &t));
  return t;
}

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

  /* Reject duplicate field names. */
  for (int i = 0; i < oak_dynarr_count(type->fields); ++i)
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
  oak_assert(oak_dynarr_push(&type->fields, &f));
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
  struct oak_bind_global_fn_t entry = *p;
  oak_assert(oak_dynarr_push(&opts->native_global_fns, &entry));
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

  struct oak_bind_fn_t copy = *p;
  oak_assert(oak_dynarr_push(&opts->native_fns, &copy));
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
  e->allocator = opts->allocator;
  oak_assert(oak_dynarr_init(e->allocator, &e->variants, sizeof *e->variants));

  oak_assert(oak_dynarr_push(&opts->native_enums, &e));
  return e;
}

int oak_bind_enum_variant(struct oak_bind_enum_t* e,
                          const char* name,
                          const int value)
{
  if (!e || !name)
    return -1;

  for (int i = 0; i < oak_dynarr_count(e->variants); ++i)
  {
    if (strcmp(e->variants[i].name, name) == 0)
      return -1;
  }

  struct oak_bind_enum_variant_t v = {
    .name = name,
    .value = value,
  };
  oak_assert(oak_dynarr_push(&e->variants, &v));
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
  if (!opts || oak_dynarr_count(opts->native_attrs) == 0 || attr_count == 0)
    return;
  for (int bi = 0; bi < oak_dynarr_count(opts->native_attrs); ++bi)
  {
    const struct oak_bind_attr_t* b = &opts->native_attrs[bi];
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
          /* Expose the live options so callbacks can bind new native
           * types/methods that the post-record-decl native pass registers.
           * Cast away const: opts is the compiler's working options object
           * (never a read-only literal) and the binding API mutates it. */
          .opts = (struct oak_compile_options_t*)opts,
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
  if (!opts || oak_dynarr_count(opts->native_attrs) == 0 || attr_count == 0)
    return;

  int match_count = 0;
  for (int bi = 0; bi < oak_dynarr_count(opts->native_attrs); ++bi)
  {
    const struct oak_bind_attr_t* b = &opts->native_attrs[bi];
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
  for (int bi = 0; bi < oak_dynarr_count(opts->native_attrs); ++bi)
  {
    const struct oak_bind_attr_t* b = &opts->native_attrs[bi];
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
  oak_assert(oak_dynarr_push(&opts->native_attrs, params));
  return 0;
}
