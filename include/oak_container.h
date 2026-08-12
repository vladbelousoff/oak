#pragma once

#include "oak_export.h"
#include "oak_object.h"
#include "oak_types.h"

#include <string.h>

/*
 * Generic container interface.
 *
 * There is one opaque container type rather than separate sequence/map/set
 * types, and operations carry no noun: `oak_push_back`, `oak_put`, `oak_add`.
 * Which operations an implementation actually supports is answered by
 * `oak_query_interface` (OAK_IID_SEQUENCE / OAK_IID_MAP / OAK_IID_SET /
 * OAK_IID_ITERABLE / OAK_IID_RANDOM_ACCESS), not by the C type. Calling an
 * unsupported operation is not undefined: it leaves the container untouched
 * and returns the same failure value as any other rejected call — 0 for the
 * int-returning operations, null for the pointer-returning ones.
 *
 * Elements are stored by value: every operation taking a `const void* value`
 * copies elem_size bytes out of it, and every operation returning `void*`
 * returns a borrowed pointer into the container's own storage.
 *
 * Structural mutation (insert, erase, push_back, put, clear, resize, reserve)
 * invalidates outstanding element pointers and iterators unless an
 * implementation documents otherwise.
 *
 * Note the unrelated `oak_container_of` macro in oak_list.h: that is the
 * intrusive-list member-offset helper, not part of this interface.
 */

typedef struct oak_container oak_container_t;
typedef struct oak_allocator oak_allocator_t;

/* Iteration cursor. Obtained from `oak_begin`, advanced with `oak_next`.
 * Implementations pick whichever `state` member suits them: a vector stores an
 * index, a hash table stores a slot index. */
typedef struct oak_iterator oak_iterator_t;
struct oak_iterator
{
  void* owner; /* the container being iterated; null once exhausted */
  union
  {
    void* ptr;
    usize index;
    usize bits[2];
  } state;
};

/* ---------- universal ---------- */

/* Number of elements. 0 for a null container. */
OAK_API usize oak_size(const oak_container_t* c);

/* Removes every element. Capacity is retained. */
OAK_API void oak_clear(oak_container_t* c);

/* Release with `oak_destroy(c)` — declared in oak_object.h along with the
 * rest of the lifetime and type-identity operations. */

/* ---------- positional (OAK_IID_SEQUENCE) ---------- */

/* Borrowed pointer to the element at `index`, or null when out of range. */
OAK_API void* oak_get(oak_container_t* c, usize index);
OAK_API const void* oak_cget(const oak_container_t* c, usize index);

/* Copies `value` in at `index`, shifting later elements up. `index` may equal
 * the current size, which appends. Returns 1 on success, 0 on failure. */
OAK_API int oak_insert(oak_container_t* c, usize index, const void* value);

/* Removes the element at `index`, shifting later elements down. */
OAK_API int oak_erase(oak_container_t* c, usize index);

/* Appends a copy of `value`. */
OAK_API int oak_push_back(oak_container_t* c, const void* value);

/* Removes the last element, copying it to `out_value` when non-null.
 * Returns 0 if the container is empty. */
OAK_API int oak_pop_back(oak_container_t* c, void* out_value);

/* Grows capacity to at least `capacity`. Never shrinks, never changes size. */
OAK_API int oak_reserve(oak_container_t* c, usize capacity);

/* Sets the element count. Growth is zero-filled. */
OAK_API int oak_resize(oak_container_t* c, usize count);

/* Allocated capacity in elements. */
OAK_API usize oak_capacity(const oak_container_t* c);

/* ---------- contiguous storage (OAK_IID_RANDOM_ACCESS) ---------- */

/* Borrowed pointer to the packed element array, or null when the
 * implementation does not store elements contiguously. Prefer this over
 * repeated `oak_get` when walking every element: hoist it once, then index
 * directly. Invalidated by any structural mutation. */
OAK_API void* oak_data(oak_container_t* c);
OAK_API const void* oak_cdata(const oak_container_t* c);

/* Typed access over contiguous storage. Hoist OAK_DATA out of loops rather
 * than using OAK_AT repeatedly — OAK_AT re-fetches the base pointer. */
#define OAK_DATA(type, c) ((type*)oak_data(c))
#define OAK_CDATA(type, c) ((const type*)oak_cdata(c))
#define OAK_AT(type, c, i) (((type*)oak_data(c))[i])

/* ---------- keyed (OAK_IID_MAP) ---------- */

/*
 * Keys are borrowed byte ranges: the caller must keep the memory a key points
 * at alive for as long as the entry exists. Keys are compared by content, not
 * by pointer identity.
 */

/* Borrowed pointer to the value stored under `key`, or null when absent. */
OAK_API void* oak_find(oak_container_t* c, const void* key, usize key_len);
OAK_API const void* oak_cfind(const oak_container_t* c,
                              const void* key,
                              usize key_len);

/* Stores a copy of `value` under `key`, overwriting any existing entry. */
OAK_API int oak_put(oak_container_t* c,
                    const void* key,
                    usize key_len,
                    const void* value);

/* Removes the entry for `key`. Returns 0 when it was not present. */
OAK_API int oak_erase_key(oak_container_t* c,
                          const void* key,
                          usize key_len);

/* 1 when `key` is present. Also answers membership for sets. */
OAK_API int oak_contains(const oak_container_t* c,
                         const void* key,
                         usize key_len);

/* ---------- membership (OAK_IID_SET) ---------- */

/* Adds `value` to the set. Returns 1 if it was inserted, 0 if already
 * present or on failure. Remove with `oak_erase_key`, test with
 * `oak_contains`. */
OAK_API int oak_add(oak_container_t* c, const void* value, usize value_len);

/* ---------- iteration (OAK_IID_ITERABLE) ---------- */

/*
 * Canonical loop — `oak_iter_get` is null exactly when the cursor is
 * exhausted, so it doubles as the condition:
 *
 *   for (oak_iterator_t it = oak_begin(c); oak_iter_get(&it);
 *        oak_next(&it))
 *     ...
 *
 * For a set, whose members are keys rather than values, drive the loop on
 * `oak_iter_key` instead.
 */

/* Cursor positioned on the first element, or exhausted when empty. */
OAK_API oak_iterator_t oak_begin(oak_container_t* c);

/* Advances to the next element. Returns 0 once exhausted. */
OAK_API int oak_next(oak_iterator_t* it);

/* Borrowed pointer to the current element (the value, for keyed containers),
 * or null when exhausted. Named apart from `oak_get` because that one is
 * positional. */
OAK_API void* oak_iter_get(oak_iterator_t* it);

/* Borrowed pointer to the current key, or null for non-keyed containers.
 * Writes the key length to `out_key_len` when it is non-null. */
OAK_API const void* oak_iter_key(oak_iterator_t* it, usize* out_key_len);

/* ---------- C-string key sugar ---------- */

/* Every key in Oak's own registries is a C string, so these save the strlen
 * at each call site. */

static inline void* oak_find_str(oak_container_t* c, const char* key)
{
  return oak_find(c, key, strlen(key));
}

static inline const void* oak_cfind_str(const oak_container_t* c,
                                        const char* key)
{
  return oak_cfind(c, key, strlen(key));
}

static inline int oak_put_str(oak_container_t* c,
                              const char* key,
                              const void* value)
{
  return oak_put(c, key, strlen(key), value);
}

static inline int oak_erase_key_str(oak_container_t* c, const char* key)
{
  return oak_erase_key(c, key, strlen(key));
}

static inline int oak_contains_str(const oak_container_t* c,
                                   const char* key)
{
  return oak_contains(c, key, strlen(key));
}

static inline int oak_add_str(oak_container_t* c, const char* value)
{
  return oak_add(c, value, strlen(value));
}
