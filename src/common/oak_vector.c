#include "internal/oak_object_impl.h"

#include "oak_allocator.h"
#include "oak_vector.h"

#include <stdint.h>
#include <string.h>

/* No embedded base: the handle points straight at this body, and the vtable
 * and allocator live in the hidden header before it. Element storage is a
 * separate allocation, so growing never moves the handle. */
typedef struct oak_vector oak_vector_t;
struct oak_vector
{
  u8* data;
  usize size;
  usize capacity;
  usize elem_size;
};

static const oak_type_info_t vector_type_info = {
  .name = "vector",
  .parent = &oak_type_info_container,
};

static oak_vector_t* as_vector(oak_container_t* c)
{
  return (oak_vector_t*)c;
}

static const oak_vector_t* as_cvector(const oak_container_t* c)
{
  return (const oak_vector_t*)c;
}

static u8* elem_at(const oak_vector_t* v, usize index)
{
  return v->data + index * v->elem_size;
}

static int vector_reserve(oak_container_t* c, usize capacity)
{
  oak_vector_t* v = as_vector(c);
  if (capacity <= v->capacity)
    return 1;
  if (capacity > SIZE_MAX / v->elem_size)
    return 0;

  u8* grown = OAK_REALLOC(
      oak_container_allocator(c), v->data, capacity * v->elem_size);
  if (!grown)
    return 0;
  v->data = grown;
  v->capacity = capacity;
  return 1;
}

/* Room for one more element, growing 0 -> 8 -> x2. */
static int ensure_room(oak_container_t* c)
{
  oak_vector_t* v = as_vector(c);
  if (v->size < v->capacity)
    return 1;
  usize capacity = v->capacity < 8 ? 8 : v->capacity * 2;
  if (capacity <= v->capacity)
    return 0; /* usize overflow */
  return vector_reserve(c, capacity);
}

static usize vector_size(const oak_container_t* c)
{
  return as_cvector(c)->size;
}

static void vector_clear(oak_container_t* c)
{
  as_vector(c)->size = 0;
}

static void* vector_get(oak_container_t* c, usize index)
{
  oak_vector_t* v = as_vector(c);
  return index < v->size ? elem_at(v, index) : null;
}

static int vector_insert(oak_container_t* c,
                         usize index,
                         const void* value)
{
  oak_vector_t* v = as_vector(c);
  if (!value || index > v->size)
    return 0;
  if (!ensure_room(c))
    return 0;
  if (index < v->size)
    memmove(elem_at(v, index + 1),
            elem_at(v, index),
            (v->size - index) * v->elem_size);
  memcpy(elem_at(v, index), value, v->elem_size);
  ++v->size;
  return 1;
}

static int vector_erase(oak_container_t* c, usize index)
{
  oak_vector_t* v = as_vector(c);
  if (index >= v->size)
    return 0;
  if (index + 1 < v->size)
    memmove(elem_at(v, index),
            elem_at(v, index + 1),
            (v->size - index - 1) * v->elem_size);
  --v->size;
  return 1;
}

static int vector_push_back(oak_container_t* c, const void* value)
{
  oak_vector_t* v = as_vector(c);
  if (!value)
    return 0;
  if (!ensure_room(c))
    return 0;
  memcpy(elem_at(v, v->size), value, v->elem_size);
  ++v->size;
  return 1;
}

static int vector_pop_back(oak_container_t* c, void* out_value)
{
  oak_vector_t* v = as_vector(c);
  if (v->size == 0)
    return 0;
  --v->size;
  if (out_value)
    memcpy(out_value, elem_at(v, v->size), v->elem_size);
  return 1;
}

static int vector_resize(oak_container_t* c, usize count)
{
  oak_vector_t* v = as_vector(c);
  if (count > v->capacity && !vector_reserve(c, count))
    return 0;
  if (count > v->size)
    memset(elem_at(v, v->size), 0, (count - v->size) * v->elem_size);
  v->size = count;
  return 1;
}

static usize vector_capacity(const oak_container_t* c)
{
  return as_cvector(c)->capacity;
}

static void* vector_data(oak_container_t* c)
{
  return as_vector(c)->data;
}

static oak_iterator_t vector_begin(oak_container_t* c)
{
  oak_iterator_t it = { .owner = null };
  if (as_vector(c)->size > 0)
  {
    it.owner = c;
    it.state.index = 0;
  }
  return it;
}

static int vector_next(oak_iterator_t* it)
{
  oak_vector_t* v = as_vector((oak_container_t*)it->owner);
  if (++it->state.index < v->size)
    return 1;
  it->owner = null;
  return 0;
}

static void* vector_iter_get(oak_iterator_t* it)
{
  oak_vector_t* v = as_vector((oak_container_t*)it->owner);
  return it->state.index < v->size ? elem_at(v, it->state.index) : null;
}

static void vector_destroy(void* obj)
{
  oak_vector_t* v = (oak_vector_t*)obj;
  if (v->data)
    OAK_FREE(oak_base_header(obj)->allocator, v->data);
  oak_base_free(obj);
}

static const oak_type_info_t* vector_type_of(const void* obj)
{
  (void)obj;
  return &vector_type_info;
}

static void* vector_query_interface(void* obj, oak_interface_id_t iid)
{
  switch (iid)
  {
    case OAK_IID_SEQUENCE:
    case OAK_IID_RANDOM_ACCESS:
    case OAK_IID_ITERABLE:
      return obj;
    default:
      return null;
  }
}

static const oak_container_vtable_t vector_vtable = {
  .object = {
    .destroy = vector_destroy,
    .type_of = vector_type_of,
    .query_interface = vector_query_interface,
  },
  .size = vector_size,
  .clear = vector_clear,
  .get = vector_get,
  .insert = vector_insert,
  .erase = vector_erase,
  .push_back = vector_push_back,
  .pop_back = vector_pop_back,
  .reserve = vector_reserve,
  .resize = vector_resize,
  .capacity = vector_capacity,
  .data = vector_data,
  .begin = vector_begin,
  .next = vector_next,
  .iter_get = vector_iter_get,
  /* find/put/erase_key/contains/add/iter_key stay null: a vector is not
   * keyed, so those operations decline in the dispatcher. */
};

oak_container_t* oak_vector_new(oak_allocator_t* allocator,
                                          usize elem_size)
{
  if (!allocator || elem_size == 0)
    return null;

  oak_vector_t* v =
      oak_base_alloc(allocator, sizeof *v, &vector_vtable.object);
  if (!v)
    return null;

  v->data = null;
  v->size = 0;
  v->capacity = 0;
  v->elem_size = elem_size;
  return (oak_container_t*)v;
}

usize oak_vector_elem_size(const oak_container_t* c)
{
  if (!oak_is(c, &vector_type_info))
    return 0;
  return as_cvector(c)->elem_size;
}

const oak_type_info_t* oak_vector_type_info(void)
{
  return &vector_type_info;
}
