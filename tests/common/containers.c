#include "oak_allocator.h"
#include "oak_container.h"
#include "oak_hash_map.h"
#include "oak_hash_set.h"
#include "oak_object.h"
#include "oak_vector.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                            \
  do                                                                           \
  {                                                                            \
    if (!(expr))                                                               \
      return __LINE__;                                                         \
  } while (0)

/* Allocator that can be told to fail, so growth paths can be checked for
 * leaving the container intact rather than half-grown. */
typedef struct fail_state fail_state_t;
struct fail_state
{
  int fail_alloc;
  int fail_realloc;
};

static void* fail_alloc(oak_allocator_t* self,
                        usize size,
                        const char* file,
                        int line)
{
  (void)file;
  (void)line;
  fail_state_t* state = self->state;
  return state->fail_alloc ? null : malloc(size);
}

static void* fail_realloc(oak_allocator_t* self,
                          void* ptr,
                          usize size,
                          const char* file,
                          int line)
{
  (void)file;
  (void)line;
  fail_state_t* state = self->state;
  return state->fail_realloc ? null : realloc(ptr, size);
}

static void fail_free(oak_allocator_t* self,
                      void* ptr,
                      const char* file,
                      int line)
{
  (void)self;
  (void)file;
  (void)line;
  free(ptr);
}

static int fail_shutdown(oak_allocator_t* self)
{
  (void)self;
  return 0;
}

static oak_allocator_t fail_allocator(fail_state_t* state)
{
  oak_allocator_t allocator = {
    .alloc = fail_alloc,
    .realloc = fail_realloc,
    .free = fail_free,
    .shutdown = fail_shutdown,
    .state = state,
  };
  return allocator;
}

/* ---------- vector ---------- */

static int test_vector(oak_allocator_t* a)
{
  oak_container_t* v = oak_vector_new(a, sizeof(int));
  CHECK(v != null);
  CHECK(oak_size(v) == 0);
  CHECK(oak_capacity(v) == 0);
  CHECK(oak_vector_elem_size(v) == sizeof(int));

  /* Type identity walks the chain: vector -> container -> object. */
  CHECK(oak_is(v, oak_vector_type_info()));
  CHECK(oak_is(v, oak_object_type_info()));
  CHECK(!oak_is(v, oak_hash_map_type_info()));
  CHECK(strcmp(oak_type_name(v), "vector") == 0);

  CHECK(oak_query_interface(v, OAK_IID_SEQUENCE) != null);
  CHECK(oak_query_interface(v, OAK_IID_RANDOM_ACCESS) != null);
  CHECK(oak_query_interface(v, OAK_IID_ITERABLE) != null);
  CHECK(oak_query_interface(v, OAK_IID_MAP) == null);
  CHECK(oak_query_interface(v, OAK_IID_SET) == null);

  /* Growth past the 8 -> x2 thresholds. */
  for (int i = 0; i < 40; ++i)
    CHECK(oak_push_back(v, &i));
  CHECK(oak_size(v) == 40);
  CHECK(oak_capacity(v) >= 40);

  const int* data = OAK_CDATA(int, v);
  CHECK(data != null);
  for (int i = 0; i < 40; ++i)
    CHECK(data[i] == i);
  CHECK(*(const int*)oak_cget(v, 7) == 7);
  CHECK(oak_cget(v, 40) == null);

  CHECK(oak_reserve(v, 100));
  CHECK(oak_capacity(v) >= 100);
  CHECK(oak_size(v) == 40);
  CHECK(oak_resize(v, 45));
  for (usize i = 40; i < 45; ++i)
    CHECK(OAK_AT(int, v, i) == 0); /* growth is zero-filled */

  /* insert shifts up, erase shifts down. */
  CHECK(oak_resize(v, 3));
  const int inserted = 99;
  CHECK(oak_insert(v, 1, &inserted));
  CHECK(oak_size(v) == 4);
  CHECK(OAK_AT(int, v, 0) == 0);
  CHECK(OAK_AT(int, v, 1) == 99);
  CHECK(OAK_AT(int, v, 2) == 1);
  CHECK(oak_erase(v, 1));
  CHECK(oak_size(v) == 3);
  CHECK(OAK_AT(int, v, 1) == 1);
  CHECK(!oak_erase(v, 3)); /* out of range */

  /* insert at size appends; past size is rejected. */
  CHECK(oak_insert(v, oak_size(v), &inserted));
  CHECK(OAK_AT(int, v, 3) == 99);
  CHECK(!oak_insert(v, oak_size(v) + 1, &inserted));

  int popped = -1;
  CHECK(oak_pop_back(v, &popped));
  CHECK(popped == 99);

  /* Iteration visits every element in order. */
  int seen = 0;
  int expected = 0;
  for (oak_iterator_t it = oak_begin(v); oak_iter_get(&it);
       oak_next(&it))
  {
    CHECK(*(const int*)oak_iter_get(&it) == expected);
    ++expected;
    ++seen;
  }
  CHECK((usize)seen == oak_size(v));
  CHECK(oak_iter_key(&(oak_iterator_t){ .owner = null }, null) == null);

  oak_clear(v);
  CHECK(oak_size(v) == 0);
  CHECK(oak_capacity(v) >= 100); /* clear keeps capacity */
  CHECK(!oak_pop_back(v, null));

  /* An empty container yields an immediately exhausted cursor. */
  oak_iterator_t empty = oak_begin(v);
  CHECK(oak_iter_get(&empty) == null);
  CHECK(!oak_next(&empty));

  /* Keyed operations are not silently accepted on a vector. */
  const int value = 1;
  CHECK(!oak_put(v, "k", 1, &value));
  CHECK(oak_find(v, "k", 1) == null);
  CHECK(!oak_add(v, "k", 1));
  CHECK(!oak_contains(v, "k", 1));
  CHECK(!oak_erase_key(v, "k", 1));
  CHECK(oak_size(v) == 0);

  oak_destroy(v);

  /* Zero element size and a null allocator are rejected. */
  CHECK(oak_vector_new(a, 0) == null);
  CHECK(oak_vector_new(null, sizeof(int)) == null);
  return 0;
}

/* ---------- hash map ---------- */

static int test_hash_map(oak_allocator_t* a)
{
  oak_container_t* m = oak_hash_map_new(a, sizeof(int));
  CHECK(m != null);
  CHECK(oak_size(m) == 0);
  CHECK(oak_hash_map_value_size(m) == sizeof(int));
  CHECK(oak_is(m, oak_hash_map_type_info()));
  CHECK(oak_query_interface(m, OAK_IID_MAP) != null);
  CHECK(oak_query_interface(m, OAK_IID_RANDOM_ACCESS) == null);

  /* Keys are borrowed, so they must outlive the entries. */
  static const char* const keys[] = { "alpha", "beta", "gamma", "delta",
                                      "epsilon", "zeta", "eta", "theta",
                                      "iota", "kappa", "lambda", "mu" };
  const int key_count = (int)(sizeof keys / sizeof keys[0]);

  for (int i = 0; i < key_count; ++i)
    CHECK(oak_put_str(m, keys[i], &i));
  CHECK(oak_size(m) == (usize)key_count);

  for (int i = 0; i < key_count; ++i)
  {
    const int* found = oak_cfind_str(m, keys[i]);
    CHECK(found != null);
    CHECK(*found == i);
    CHECK(oak_contains_str(m, keys[i]));
  }
  CHECK(oak_cfind_str(m, "absent") == null);
  CHECK(!oak_contains_str(m, "absent"));

  /* put over an existing key overwrites rather than duplicating. */
  const int replaced = 4242;
  CHECK(oak_put_str(m, "gamma", &replaced));
  CHECK(oak_size(m) == (usize)key_count);
  CHECK(*(const int*)oak_cfind_str(m, "gamma") == replaced);

  /* Removal leaves a tombstone; lookups past it must still resolve. */
  CHECK(oak_erase_key_str(m, "beta"));
  CHECK(!oak_erase_key_str(m, "beta")); /* already gone */
  CHECK(oak_size(m) == (usize)key_count - 1);
  CHECK(oak_cfind_str(m, "beta") == null);
  for (int i = 0; i < key_count; ++i)
  {
    if (strcmp(keys[i], "beta") == 0)
      continue;
    CHECK(oak_contains_str(m, keys[i]));
  }

  /* Re-inserting must reuse the tombstone, not append past it. */
  const int reinserted = 7;
  CHECK(oak_put_str(m, "beta", &reinserted));
  CHECK(oak_size(m) == (usize)key_count);
  CHECK(*(const int*)oak_cfind_str(m, "beta") == reinserted);

  /* Enough churn to force a rehash that must drop tombstones. */
  for (int round = 0; round < 20; ++round)
  {
    for (int i = 0; i < key_count; ++i)
    {
      CHECK(oak_erase_key_str(m, keys[i]));
      CHECK(oak_put_str(m, keys[i], &i));
    }
  }
  CHECK(oak_size(m) == (usize)key_count);

  /* Iteration reaches every entry exactly once. */
  int visited = 0;
  for (oak_iterator_t it = oak_begin(m); oak_iter_get(&it);
       oak_next(&it))
  {
    usize key_len = 0;
    const char* key = oak_iter_key(&it, &key_len);
    CHECK(key != null);
    CHECK(key_len == strlen(key));
    CHECK(oak_contains(m, key, key_len));
    ++visited;
  }
  CHECK((usize)visited == oak_size(m));

  /* Positional operations are not silently accepted on a map. */
  const int value = 1;
  CHECK(!oak_push_back(m, &value));
  CHECK(oak_get(m, 0) == null);
  CHECK(oak_data(m) == null);
  CHECK(!oak_resize(m, 4));
  CHECK(!oak_reserve(m, 4));
  CHECK(!oak_pop_back(m, null));
  CHECK(oak_capacity(m) == 0);
  CHECK(oak_size(m) == (usize)key_count);

  oak_clear(m);
  CHECK(oak_size(m) == 0);
  CHECK(oak_cfind_str(m, "alpha") == null);

  oak_destroy(m);

  CHECK(oak_hash_map_new(a, 0) == null);
  CHECK(oak_hash_map_new(null, sizeof(int)) == null);
  return 0;
}

/* ---------- hash set ---------- */

static int test_hash_set(oak_allocator_t* a)
{
  oak_container_t* s = oak_hash_set_new(a);
  CHECK(s != null);
  CHECK(oak_is(s, oak_hash_set_type_info()));
  CHECK(oak_query_interface(s, OAK_IID_SET) != null);
  CHECK(oak_query_interface(s, OAK_IID_MAP) == null);

  static const char* const names[] = { "one", "two", "three", "four", "five",
                                       "six", "seven", "eight", "nine" };
  const int name_count = (int)(sizeof names / sizeof names[0]);

  for (int i = 0; i < name_count; ++i)
    CHECK(oak_add_str(s, names[i]));
  CHECK(oak_size(s) == (usize)name_count);

  /* Adding a member again reports "already present" and does not grow. */
  CHECK(!oak_add_str(s, "three"));
  CHECK(oak_size(s) == (usize)name_count);

  for (int i = 0; i < name_count; ++i)
    CHECK(oak_contains_str(s, names[i]));
  CHECK(!oak_contains_str(s, "ten"));

  CHECK(oak_erase_key_str(s, "four"));
  CHECK(!oak_contains_str(s, "four"));
  CHECK(oak_size(s) == (usize)name_count - 1);
  CHECK(oak_add_str(s, "four")); /* reuses the tombstone */
  CHECK(oak_size(s) == (usize)name_count);

  /* A set stores no values, so members come back through iter_key. */
  int visited = 0;
  for (oak_iterator_t it = oak_begin(s); ; )
  {
    usize len = 0;
    const char* member = oak_iter_key(&it, &len);
    if (!member)
      break;
    CHECK(len == strlen(member));
    CHECK(oak_contains(s, member, len));
    CHECK(oak_iter_get(&it) == null);
    ++visited;
    oak_next(&it);
  }
  CHECK((usize)visited == oak_size(s));

  CHECK(oak_find_str(s, "one") == null); /* no values to find */
  CHECK(!oak_put_str(s, "one", "x"));

  oak_destroy(s);
  CHECK(oak_hash_set_new(null) == null);
  return 0;
}

/* ---------- hidden object header ---------- */

/* The header sits before the handle, so these properties are what make
 * oak_destroy(void*) work for any object without a cast. */
static int test_object_header(oak_allocator_t* a)
{
  oak_container_t* v = oak_vector_new(a, sizeof(int));
  oak_container_t* m = oak_hash_map_new(a, sizeof(int));
  CHECK(v != null && m != null);

  /* Object-level operations take void*, so an unrelated handle type needs no
   * cast at the call site — that is the whole point of the layout. */
  void* opaque = v;
  CHECK(oak_type_of(opaque) == oak_vector_type_info());
  CHECK(oak_is(opaque, oak_object_type_info()));
  CHECK(oak_allocator_of(opaque) == a);

  /* Each object records its own allocator, so nothing has to be threaded
   * alongside the handle. */
  CHECK(oak_allocator_of(m) == a);

  /* The body must be aligned at least as strictly as a pointer: the header is
   * padded up so callers never see a misaligned body. */
  CHECK(((uintptr_t)v % sizeof(void*)) == 0);
  CHECK(((uintptr_t)m % sizeof(void*)) == 0);

  /* Growth reallocates element storage, which is a separate allocation, so
   * the handle itself must not move. */
  oak_container_t* const before = v;
  for (int i = 0; i < 64; ++i)
    CHECK(oak_push_back(v, &i));
  CHECK(v == before);
  CHECK(oak_size(v) == 64);
  CHECK(oak_type_of(v) == oak_vector_type_info());

  /* Same for a rehash. */
  oak_container_t* const m_before = m;
  char keys[64][8];
  for (int i = 0; i < 64; ++i)
  {
    snprintf(keys[i], sizeof(keys[i]), "k%d", i);
    CHECK(oak_put_str(m, keys[i], &i));
  }
  CHECK(m == m_before);
  CHECK(oak_size(m) == 64);

  oak_destroy(v);
  oak_destroy(m);
  return 0;
}

/* ---------- null and failure handling ---------- */

static int test_null_safety(void)
{
  CHECK(oak_size(null) == 0);
  CHECK(oak_capacity(null) == 0);
  CHECK(oak_get(null, 0) == null);
  CHECK(oak_data(null) == null);
  CHECK(oak_find(null, "k", 1) == null);
  CHECK(!oak_contains(null, "k", 1));
  CHECK(!oak_push_back(null, "x"));
  CHECK(null == null);
  CHECK(oak_type_of(null) == null);
  CHECK(!oak_is(null, oak_vector_type_info()));
  CHECK(oak_query_interface(null, OAK_IID_SEQUENCE) == null);
  CHECK(strcmp(oak_type_name(null), "(null)") == 0);

  oak_clear(null);
  oak_destroy(null);

  oak_iterator_t it = oak_begin(null);
  CHECK(oak_iter_get(&it) == null);
  CHECK(!oak_next(&it));
  return 0;
}

static int test_allocation_failure(void)
{
  fail_state_t state = { .fail_alloc = 1 };
  oak_allocator_t failing = fail_allocator(&state);

  /* A failed header allocation yields no container at all. */
  CHECK(oak_vector_new(&failing, sizeof(int)) == null);
  CHECK(oak_hash_map_new(&failing, sizeof(int)) == null);
  CHECK(oak_hash_set_new(&failing) == null);

  /* A failed grow must leave size and contents untouched. */
  state.fail_alloc = 0;
  oak_container_t* v = oak_vector_new(&failing, sizeof(int));
  CHECK(v != null);
  for (int i = 0; i < 8; ++i)
    CHECK(oak_push_back(v, &i));
  CHECK(oak_size(v) == 8);

  state.fail_realloc = 1;
  const int ninth = 9;
  CHECK(!oak_push_back(v, &ninth));
  CHECK(oak_size(v) == 8);
  CHECK(!oak_reserve(v, 4096));
  CHECK(!oak_resize(v, 4096));
  CHECK(oak_size(v) == 8);
  for (int i = 0; i < 8; ++i)
    CHECK(OAK_AT(int, v, i) == i);
  state.fail_realloc = 0;
  oak_destroy(v);

  /* Same for the table's rehash: a failed grow keeps every entry findable. */
  oak_container_t* m = oak_hash_map_new(&failing, sizeof(int));
  CHECK(m != null);
  static const char* const keys[] = { "a", "b", "c", "d", "e" };
  for (int i = 0; i < 5; ++i)
    CHECK(oak_put_str(m, keys[i], &i));
  CHECK(oak_size(m) == 5);

  state.fail_alloc = 1;
  const int extra = 5;
  CHECK(!oak_put_str(m, "f", &extra));
  CHECK(!oak_put_str(m, "g", &extra));
  CHECK(oak_size(m) == 5);
  for (int i = 0; i < 5; ++i)
    CHECK(*(const int*)oak_cfind_str(m, keys[i]) == i);
  state.fail_alloc = 0;
  oak_destroy(m);

  /* An oversized element count must be rejected rather than wrapping. */
  oak_container_t* wide = oak_vector_new(&failing, SIZE_MAX / 2);
  CHECK(wide != null);
  CHECK(!oak_reserve(wide, 4));
  CHECK(oak_size(wide) == 0);
  oak_destroy(wide);
  return 0;
}

int main(void)
{
  int failed_line = test_null_safety();
  if (failed_line)
    return failed_line;

  failed_line = test_allocation_failure();
  if (failed_line)
    return failed_line;

  oak_allocator_t tracking;
  oak_tracking_allocator_init(&tracking);

  failed_line = test_vector(&tracking);
  if (failed_line)
    return failed_line;

  failed_line = test_hash_map(&tracking);
  if (failed_line)
    return failed_line;

  failed_line = test_hash_set(&tracking);
  if (failed_line)
    return failed_line;

  failed_line = test_object_header(&tracking);
  if (failed_line)
    return failed_line;

  /* Reports and fails on any container that was not fully released. */
  CHECK(tracking.shutdown(&tracking) == 0);
  return 0;
}
