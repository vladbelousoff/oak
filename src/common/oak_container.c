#include "internal/oak_object_impl.h"

/*
 * Dispatch shims. Each one resolves the container's vtable, and returns the
 * ordinary failure value when the slot is null — that is how an
 * implementation declines an operation it does not support (a hash map has no
 * push_back, a vector has no put). Callers get 0 or null either way, so an
 * unsupported call and a rejected call are handled by the same code path.
 */

/* `oak_size`/`oak_capacity`/`oak_contains`/`oak_cget`/`oak_cfind`/`oak_cdata`
 * are logically const, but the vtable slots take a mutable container so a
 * single implementation can serve both. The cast is confined to this file. */
static oak_container_t* unconst(const oak_container_t* c)
{
  return (oak_container_t*)c;
}

usize oak_size(const oak_container_t* c)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->size ? vt->size(c) : 0;
}

void oak_clear(oak_container_t* c)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  if (vt && vt->clear)
    vt->clear(c);
}

void* oak_get(oak_container_t* c, usize index)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->get ? vt->get(c, index) : OAK_NULL;
}

const void* oak_cget(const oak_container_t* c, usize index)
{
  return oak_get(unconst(c), index);
}

int oak_insert(oak_container_t* c, usize index, const void* value)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->insert ? vt->insert(c, index, value) : 0;
}

int oak_erase(oak_container_t* c, usize index)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->erase ? vt->erase(c, index) : 0;
}

int oak_push_back(oak_container_t* c, const void* value)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->push_back ? vt->push_back(c, value) : 0;
}

int oak_pop_back(oak_container_t* c, void* out_value)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->pop_back ? vt->pop_back(c, out_value) : 0;
}

int oak_reserve(oak_container_t* c, usize capacity)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->reserve ? vt->reserve(c, capacity) : 0;
}

int oak_resize(oak_container_t* c, usize count)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->resize ? vt->resize(c, count) : 0;
}

usize oak_capacity(const oak_container_t* c)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->capacity ? vt->capacity(c) : 0;
}

void* oak_data(oak_container_t* c)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->data ? vt->data(c) : OAK_NULL;
}

const void* oak_cdata(const oak_container_t* c)
{
  return oak_data(unconst(c));
}

void* oak_find(oak_container_t* c, const void* key, usize key_len)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->find ? vt->find(c, key, key_len) : OAK_NULL;
}

const void* oak_cfind(const oak_container_t* c,
                      const void* key,
                      usize key_len)
{
  return oak_find(unconst(c), key, key_len);
}

int oak_put(oak_container_t* c,
            const void* key,
            usize key_len,
            const void* value)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->put ? vt->put(c, key, key_len, value) : 0;
}

int oak_erase_key(oak_container_t* c, const void* key, usize key_len)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->erase_key ? vt->erase_key(c, key, key_len) : 0;
}

int oak_contains(const oak_container_t* c,
                 const void* key,
                 usize key_len)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->contains ? vt->contains(c, key, key_len) : 0;
}

int oak_add(oak_container_t* c, const void* value, usize value_len)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  return vt && vt->add ? vt->add(c, value, value_len) : 0;
}

oak_iterator_t oak_begin(oak_container_t* c)
{
  const oak_container_vtable_t* vt = oak_container_vt(c);
  if (vt && vt->begin)
    return vt->begin(c);
  /* An exhausted cursor: oak_next reports 0, oak_iter_get reports null. */
  oak_iterator_t it = { .owner = OAK_NULL };
  return it;
}

/* Iterator operations dispatch through the container the cursor came from.
 * `owner` is cleared once the cursor is exhausted, which is also what makes
 * an iterator over an unsupported container behave like an empty one. */
static const oak_container_vtable_t* iter_vt(
    const oak_iterator_t* it)
{
  if (!it || !it->owner)
    return OAK_NULL;
  return oak_container_vt((const oak_container_t*)it->owner);
}

int oak_next(oak_iterator_t* it)
{
  const oak_container_vtable_t* vt = iter_vt(it);
  return vt && vt->next ? vt->next(it) : 0;
}

void* oak_iter_get(oak_iterator_t* it)
{
  const oak_container_vtable_t* vt = iter_vt(it);
  return vt && vt->iter_get ? vt->iter_get(it) : OAK_NULL;
}

const void* oak_iter_key(oak_iterator_t* it, usize* out_key_len)
{
  const oak_container_vtable_t* vt = iter_vt(it);
  if (vt && vt->iter_key)
    return vt->iter_key(it, out_key_len);
  if (out_key_len)
    *out_key_len = 0;
  return OAK_NULL;
}
