#include "oak_bind.h"

#include "oak_log.h"
#include "oak_mem.h"
#include "oak_str.h"
#include "oak_type.h"
#include "oak_value.h"

#include <string.h>

/* ---------- Compile-options lifecycle ---------- */

void oak_compile_options_init(struct oak_compile_options_t* opts)
{
  if (!opts)
    return;
  opts->native_types = null;
  opts->native_type_count = 0;
  opts->native_type_capacity = 0;
  opts->native_fns = null;
  opts->native_fn_count = 0;
  opts->native_fn_capacity = 0;
  opts->next_type_id = OAK_TYPE_FIRST_USER;
}

void oak_compile_options_free(struct oak_compile_options_t* opts)
{
  if (!opts)
    return;
  for (int i = 0; i < opts->native_type_count; ++i)
    oak_free(opts->native_types[i], OAK_SRC_LOC);
  if (opts->native_types)
    oak_free(opts->native_types, OAK_SRC_LOC);
  if (opts->native_fns)
    oak_free(opts->native_fns, OAK_SRC_LOC);
  opts->native_types = null;
  opts->native_type_count = 0;
  opts->native_type_capacity = 0;
  opts->native_fns = null;
  opts->native_fn_count = 0;
  opts->native_fn_capacity = 0;
  opts->next_type_id = OAK_TYPE_FIRST_USER;
}

/* ---------- Binding API ---------- */

struct oak_native_type_t* oak_bind_type(struct oak_compile_options_t* opts,
                                        const enum oak_bind_type_kind_t kind,
                                        const char* name)
{
  if (!opts || !name)
    return null;

  if (opts->next_type_id >= OAK_MAX_TYPES)
    return null;

  /* Grow the native_types array if needed. */
  if (opts->native_type_count >= opts->native_type_capacity)
  {
    const int new_cap =
        opts->native_type_capacity == 0 ? 8 : opts->native_type_capacity * 2;
    struct oak_native_type_t** arr =
        oak_realloc(opts->native_types,
                    (usize)new_cap * sizeof(struct oak_native_type_t*),
                    OAK_SRC_LOC);
    opts->native_types = arr;
    opts->native_type_capacity = new_cap;
  }

  struct oak_native_type_t* t =
      oak_alloc(sizeof(struct oak_native_type_t), OAK_SRC_LOC);
  t->kind = kind;
  t->name = name;
  t->name_len = strlen(name);
  t->type_id = opts->next_type_id++;
  t->field_count = 0;

  opts->native_types[opts->native_type_count++] = t;
  return t;
}

int oak_bind_field(struct oak_native_type_t* type,
                   const char* name,
                   const oak_type_id_t field_type_id,
                   const oak_field_getter_t getter,
                   const oak_field_setter_t setter)
{
  if (!type || !name || !getter)
    return -1;
  if (type->field_count >= OAK_MAX_NATIVE_FIELDS)
    return -1;

  const usize len = strlen(name);

  /* Reject duplicate field names. */
  for (int i = 0; i < type->field_count; ++i)
  {
    if (oak_name_eq(type->fields[i].name, type->fields[i].name_len, name, len))
      return -1;
  }

  struct oak_native_field_t* f = &type->fields[type->field_count++];
  f->name = name;
  f->name_len = len;
  f->field_type_id = field_type_id;
  f->getter = getter;
  f->setter = setter;
  return 0;
}

int oak_bind_fn(struct oak_compile_options_t* opts,
                const oak_type_id_t receiver_type_id,
                const char* name,
                const oak_native_fn_t impl,
                const int arity,
                const oak_type_id_t return_type_id)
{
  if (!opts || !name || !impl || arity < 0)
    return -1;

  /* Grow the native_fns array if needed. */
  if (opts->native_fn_count >= opts->native_fn_capacity)
  {
    const int new_cap =
        opts->native_fn_capacity == 0 ? 8 : opts->native_fn_capacity * 2;
    struct oak_native_fn_binding_t* arr =
        oak_realloc(opts->native_fns,
                    (usize)new_cap * sizeof(struct oak_native_fn_binding_t),
                    OAK_SRC_LOC);
    opts->native_fns = arr;
    opts->native_fn_capacity = new_cap;
  }

  struct oak_native_fn_binding_t* b = &opts->native_fns[opts->native_fn_count++];
  b->receiver_type_id = receiver_type_id;
  b->name = name;
  b->impl = impl;
  b->arity = arity;
  b->return_type_id = return_type_id;
  return 0;
}

/* ---------- Runtime helpers ---------- */

struct oak_value_t oak_native_struct_new(const struct oak_native_type_t* type,
                                         void* instance)
{
  oak_assert(type != null);
  struct oak_obj_native_struct_t* ns =
      oak_obj_native_struct_new(type, instance);
  return OAK_VALUE_OBJ(&ns->obj);
}

void* oak_native_instance(const struct oak_value_t value)
{
  return oak_as_native_struct(value)->instance;
}
