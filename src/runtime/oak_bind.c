#include "oak_bind.h"

#include "oak_log.h"
#include "oak_mem.h"
#include "oak_str.h"
#include "oak_value.h"

#include <string.h>

/* Global counter for pre-assigning stable type ids to native types.
 * Starts at OAK_TYPE_FIRST_USER so it never overlaps built-in ids.
 * Not thread-safe, matching the rest of the codebase. */
static oak_type_id_t g_next_native_type_id = OAK_TYPE_FIRST_USER;

struct oak_native_type_t* oak_bind_type(const enum oak_bind_type_kind_t kind,
                                        const char* name)
{
  if (!name)
    return null;

  if (g_next_native_type_id >= OAK_MAX_TYPES)
    return null;

  struct oak_native_type_t* t =
      oak_alloc(sizeof(struct oak_native_type_t), OAK_SRC_LOC);
  t->kind = kind;
  t->name = name;
  t->name_len = strlen(name);
  t->type_id = g_next_native_type_id++;
  t->field_count = 0;
  return t;
}

void oak_bind_type_free(struct oak_native_type_t* type)
{
  if (type)
    oak_free(type, OAK_SRC_LOC);
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
