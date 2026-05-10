#include "oak_bind.h"

#include "oak_dynarr.h"
#include "oak_log.h"
#include "oak_mem.h"
#include "oak_type.h"
#include "oak_value.h"

#include <string.h>

/* ---------- Compile-options lifecycle ---------- */

void oak_compile_options_init(struct oak_compile_options_t* opts)
{
  if (!opts)
    return;
  opts->source_name = null;
  oak_dynarr_init(&opts->native_types.items,
                  &opts->native_types.count,
                  &opts->native_types.capacity);
  oak_dynarr_init(&opts->native_fns.items,
                  &opts->native_fns.count,
                  &opts->native_fns.capacity);
  oak_dynarr_init(&opts->native_enums.items,
                  &opts->native_enums.count,
                  &opts->native_enums.capacity);
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
    oak_free(opts->native_types.items[i], OAK_SRC_LOC);
  oak_dynarr_free(&opts->native_types.items,
                  &opts->native_types.count,
                  &opts->native_types.capacity);
  oak_dynarr_free(&opts->native_fns.items,
                  &opts->native_fns.count,
                  &opts->native_fns.capacity);
  for (int i = 0; i < opts->native_enums.count; ++i)
    oak_free(opts->native_enums.items[i], OAK_SRC_LOC);
  oak_dynarr_free(&opts->native_enums.items,
                  &opts->native_enums.count,
                  &opts->native_enums.capacity);
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

  if (opts->next_type_id >= OAK_MAX_TYPES)
    return null;

  struct oak_bind_type_t* t =
      oak_alloc(sizeof(struct oak_bind_type_t), OAK_SRC_LOC);
  t->module_name = module_name;
  t->module_name_len = module_name ? strlen(module_name) : 0u;
  t->kind = kind;
  t->name = name;
  t->name_len = strlen(name);
  t->type_id = opts->next_type_id++;
  t->field_count = 0;
  t->destructor = null;

  oak_dynarr_push(&opts->native_types.items,
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
  if (type->field_count >= OAK_BIND_MAX_FIELDS)
    return -1;

  /* Reject duplicate field names. */
  for (int i = 0; i < type->field_count; ++i)
  {
    if (strcmp(type->fields[i].name, p->name) == 0)
      return -1;
  }

  struct oak_bind_field_t* f = &type->fields[type->field_count++];
  f->name = p->name;
  f->name_len = strlen(p->name);
  f->field_type_id = p->field_type_id;
  f->shape = p->shape;
  f->getter = p->getter;
  f->setter = p->setter;
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
  if (p->kind == OAK_BIND_FN_GLOBAL)
  {
    if (p->receiver_type_id != OAK_TYPE_VOID)
      return -1;
  }
  else if (p->kind == OAK_BIND_FN_INSTANCE_METHOD ||
           p->kind == OAK_BIND_FN_STATIC_METHOD)
  {
    if (p->receiver_type_id == OAK_TYPE_VOID)
      return -1;
  }
  else
    return -1;

  struct oak_bind_fn_t copy = *p;
  if (copy.module_name && copy.module_name_len == 0u)
    copy.module_name_len = strlen(copy.module_name);

  oak_dynarr_push(&opts->native_fns.items,
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
      oak_alloc(sizeof(struct oak_bind_enum_t), OAK_SRC_LOC);
  e->module_name = module_name;
  e->module_name_len = module_name ? strlen(module_name) : 0u;
  e->name = name;
  e->name_len = strlen(name);
  e->variant_count = 0;

  oak_dynarr_push(&opts->native_enums.items,
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
  if (e->variant_count >= OAK_BIND_MAX_ENUM_VARIANTS)
    return -1;

  for (int i = 0; i < e->variant_count; ++i)
  {
    if (strcmp(e->variants[i].name, name) == 0)
      return -1;
  }

  struct oak_bind_enum_variant_t* v = &e->variants[e->variant_count++];
  v->name = name;
  v->name_len = strlen(name);
  v->value = value;
  return 0;
}

/* ---------- Runtime helpers ---------- */

struct oak_value_t oak_native_record_new(const struct oak_bind_type_t* type,
                                         void* instance)
{
  oak_assert(type != null);
  struct oak_obj_native_record_t* ns =
      oak_obj_native_record_new(type, instance);
  return OAK_VALUE_OBJ(&ns->obj);
}

void* oak_native_instance(const struct oak_value_t value)
{
  return oak_as_native_record(value)->instance;
}
