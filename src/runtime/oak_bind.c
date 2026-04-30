#include "oak_bind.h"

#include "oak_dynarr.h"
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
  opts->source_name = null;
  OAK_DYNARR_INIT(opts->native_types);
  OAK_DYNARR_INIT(opts->native_fns);
  opts->next_type_id = OAK_TYPE_FIRST_USER;
  opts->emit_debug_info = 1;
}

void oak_compile_options_free(struct oak_compile_options_t* opts)
{
  if (!opts)
    return;
  for (int i = 0; i < opts->native_types.count; ++i)
    oak_free(opts->native_types.items[i], OAK_SRC_LOC);
  OAK_DYNARR_FREE(opts->native_types);
  OAK_DYNARR_FREE(opts->native_fns);
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

  struct oak_native_type_t* t =
      oak_alloc(sizeof(struct oak_native_type_t), OAK_SRC_LOC);
  t->kind = kind;
  t->name = name;
  t->name_len = strlen(name);
  t->type_id = opts->next_type_id++;
  t->field_count = 0;
  t->destroy_instance = null;

  OAK_DYNARR_PUSH(opts->native_types, t);
  return t;
}

int oak_bind_field(struct oak_native_type_t* type,
                   const struct oak_native_field_t* p)
{
  if (!type || !p)
    return -1;
  if (!p->name || !p->getter)
    return -1;
  if (type->field_count >= OAK_MAX_NATIVE_FIELDS)
    return -1;

  const usize len = strlen(p->name);

  /* Reject duplicate field names. */
  for (int i = 0; i < type->field_count; ++i)
  {
    if (oak_name_eq(
            type->fields[i].name, type->fields[i].name_len, p->name, len))
      return -1;
  }

  struct oak_native_field_t* f = &type->fields[type->field_count++];
  f->name = p->name;
  f->name_len = len;
  f->field_type_id = p->field_type_id;
  f->shape = p->shape;
  f->getter = p->getter;
  f->setter = p->setter;
  return 0;
}

int oak_bind_fn(struct oak_compile_options_t* opts,
                const oak_bind_fn_params_t* p)
{
  if (!opts || !p)
    return -1;
  if (!p->name || !p->impl || p->arity < 0)
    return -1;
  if (p->return_shape != OAK_BIND_RETURN_SCALAR &&
      p->return_shape != OAK_BIND_RETURN_ARRAY)
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

  OAK_DYNARR_PUSH(opts->native_fns, *p);
  return 0;
}

/* ---------- Runtime helpers ---------- */

struct oak_value_t oak_native_record_new(const struct oak_native_type_t* type,
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
