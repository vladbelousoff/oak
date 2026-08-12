#include "internal/oak_hash_table.h"

#include "oak_hash_map.h"

static const oak_type_info_t hash_map_type_info = {
  .name = "hash_map",
  .parent = &oak_type_info_container,
};

static const oak_type_info_t* hash_map_type_of(
    const void* obj)
{
  (void)obj;
  return &hash_map_type_info;
}

static void* hash_map_query_interface(void* obj,
                                      oak_interface_id_t iid)
{
  switch (iid)
  {
    case OAK_IID_MAP:
    case OAK_IID_ITERABLE:
      return obj;
    default:
      return null;
  }
}

static const oak_container_vtable_t hash_map_vtable = {
  .object = {
    .destroy = oak_hash_table_destroy,
    .type_of = hash_map_type_of,
    .query_interface = hash_map_query_interface,
  },
  .size = oak_hash_table_size,
  .clear = oak_hash_table_clear,
  .find = oak_hash_table_find,
  .put = oak_hash_table_put,
  .erase_key = oak_hash_table_erase_key,
  .contains = oak_hash_table_contains,
  .begin = oak_hash_table_begin,
  .next = oak_hash_table_next,
  .iter_get = oak_hash_table_iter_get,
  .iter_key = oak_hash_table_iter_key,
  /* Positional slots stay null: entries have no stable order and storage is
   * not contiguous, so get/push_back/data decline in the dispatcher. */
};

oak_container_t* oak_hash_map_new(oak_allocator_t* allocator,
                                            usize value_size)
{
  if (value_size == 0)
    return null;
  return oak_hash_table_new(allocator, value_size, &hash_map_vtable);
}

usize oak_hash_map_value_size(const oak_container_t* c)
{
  if (!oak_is(c, &hash_map_type_info))
    return 0;
  return oak_hash_table_value_size(c);
}

const oak_type_info_t* oak_hash_map_type_info(void)
{
  return &hash_map_type_info;
}
