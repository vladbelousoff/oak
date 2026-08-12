#include "internal/oak_hash_table.h"

#include "oak_hash_set.h"

static const oak_type_info_t hash_set_type_info = {
  .name = "hash_set",
  .parent = &oak_type_info_container,
};

static const oak_type_info_t* hash_set_type_of(
    const void* obj)
{
  (void)obj;
  return &hash_set_type_info;
}

static void* hash_set_query_interface(void* obj,
                                      oak_interface_id_t iid)
{
  switch (iid)
  {
    case OAK_IID_SET:
    case OAK_IID_ITERABLE:
      return obj;
    default:
      return null;
  }
}

static const oak_container_vtable_t hash_set_vtable = {
  .object = {
    .destroy = oak_hash_table_destroy,
    .type_of = hash_set_type_of,
    .query_interface = hash_set_query_interface,
  },
  .size = oak_hash_table_size,
  .clear = oak_hash_table_clear,
  .add = oak_hash_table_add,
  .erase_key = oak_hash_table_erase_key,
  .contains = oak_hash_table_contains,
  .begin = oak_hash_table_begin,
  .next = oak_hash_table_next,
  .iter_key = oak_hash_table_iter_key,
  /* `find` and `put` stay null: a set stores no values, so the members it
   * holds are reached through iter_key rather than iter_get. */
};

oak_container_t* oak_hash_set_new(oak_allocator_t* allocator)
{
  return oak_hash_table_new(allocator, 0, &hash_set_vtable);
}

const oak_type_info_t* oak_hash_set_type_info(void)
{
  return &hash_set_type_info;
}
