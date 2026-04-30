#pragma once

/*
 * Generic dynamic array as an embedded container struct.
 *
 *   OAK_DYNARR(T)              – the type. Use as a struct field:
 *                                  OAK_DYNARR(struct foo_t) entries;
 *   OAK_DYNARR_INIT(arr)       – zero the fields (no allocation).
 *   OAK_DYNARR_PUSH(arr, item) – ensure capacity, append by value.
 *   OAK_DYNARR_FREE(arr)       – free the backing array and zero fields.
 *
 * `arr` is an lvalue of OAK_DYNARR(T) type (the field itself, not its
 * address): pass `obj->entries` or `obj.entries`.
 *
 * PUSH and FREE require oak_mem.h (oak_realloc / oak_free / OAK_SRC_LOC)
 * to be visible in the translation unit.
 *
 * Iteration is plain C:
 *   for (int i = 0; i < arr.count; ++i) { ... arr.items[i] ... }
 *
 * Growth: minimum first allocation of 8 elements, doubles each grow.
 */

#define OAK_DYNARR(T) \
  struct {            \
    T*  items;        \
    int count;        \
    int capacity;     \
  }

#define OAK_DYNARR_INIT(arr) \
  do {                       \
    (arr).items    = null;   \
    (arr).count    = 0;      \
    (arr).capacity = 0;      \
  } while (0)

#define OAK_DYNARR_PUSH(arr, item)                                       \
  do {                                                                    \
    if ((arr).count >= (arr).capacity) {                                  \
      const int _nc = (arr).capacity < 8 ? 8 : (arr).capacity * 2;        \
      (arr).items   = oak_realloc(                                         \
          (arr).items, (usize)_nc * sizeof *(arr).items, OAK_SRC_LOC);   \
      (arr).capacity = _nc;                                               \
    }                                                                     \
    (arr).items[(arr).count++] = (item);                                  \
  } while (0)

#define OAK_DYNARR_FREE(arr)                \
  do {                                      \
    if ((arr).items)                        \
      oak_free((arr).items, OAK_SRC_LOC);  \
    (arr).items    = null;                  \
    (arr).count    = 0;                     \
    (arr).capacity = 0;                     \
  } while (0)
