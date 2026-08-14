#include "internal/oak_object_impl.h"

#include "oak_allocator.h"

#include <stdint.h>

/*
 * The public entry points take `void*` and recover the vtable from the hidden
 * header, so one set of operations serves every object regardless of type.
 */

const oak_type_info_t oak_type_info_object = {
  .name = "object",
  .parent = null,
};

const oak_type_info_t oak_type_info_container = {
  .name = "container",
  .parent = &oak_type_info_object,
};

void* oak_base_alloc(oak_allocator_t* allocator,
                       usize body_size,
                       const oak_base_vtable_t* vt)
{
  if (!allocator || !vt)
    return null;
  if (body_size > SIZE_MAX - OAK_OBJECT_HEADER_SIZE)
    return null;

  u8* const base =
      oak_alloc(allocator, OAK_OBJECT_HEADER_SIZE + body_size, OAK_HERE);
  if (!base)
    return null;

  oak_base_header_t* const header = (oak_base_header_t*)base;
  header->vt = vt;
  header->allocator = allocator;
#ifdef OAK_DEBUG_LOGGING
  header->magic = OAK_OBJECT_MAGIC;
#endif
  return base + OAK_OBJECT_HEADER_SIZE;
}

void oak_base_free(void* obj)
{
  if (!obj)
    return;
  oak_base_header_t* const header = oak_base_header(obj);
  oak_allocator_t* const allocator = header->allocator;
#ifdef OAK_DEBUG_LOGGING
  /* Cleared so a double free trips the magic check instead of dispatching
   * through a freed vtable pointer. Debug builds only — release has no field
   * to clear and no check to trip. */
  header->magic = 0;
#endif
  oak_free(allocator, header, OAK_HERE);
}

u32 oak_hash_bytes(const void* data, usize len)
{
  const u8* bytes = (const u8*)data;
  u32 h = 2166136261u;
  for (usize i = 0; i < len; ++i)
  {
    h ^= bytes[i];
    h *= 16777619u;
  }
  return h;
}

void oak_destroy(void* obj)
{
  if (!obj)
    return;
  oak_base_check(obj);
  oak_base_header(obj)->vt->destroy(obj);
}

const oak_type_info_t* oak_type_of(const void* obj)
{
  if (!obj)
    return null;
  oak_base_check(obj);
  return oak_base_header(obj)->vt->type_of(obj);
}

int oak_is(const void* obj, const oak_type_info_t* wanted)
{
  if (!obj || !wanted)
    return 0;
  for (const oak_type_info_t* type = oak_type_of(obj); type;
       type = type->parent)
  {
    if (type == wanted)
      return 1;
  }
  return 0;
}

void* oak_query_interface(void* obj, oak_interface_id_t iid)
{
  if (!obj)
    return null;
  oak_base_check(obj);
  return oak_base_header(obj)->vt->query_interface(obj, iid);
}

oak_allocator_t* oak_allocator_of(const void* obj)
{
  if (!obj)
    return null;
  oak_base_check(obj);
  return oak_base_header(obj)->allocator;
}

const oak_type_info_t* oak_base_type_info(void)
{
  return &oak_type_info_object;
}

const char* oak_type_name(const void* obj)
{
  const oak_type_info_t* type = oak_type_of(obj);
  return type ? type->name : "(null)";
}
