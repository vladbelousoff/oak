/*
 * Core containers: vector, hash map, hash set, and the hidden object header.
 *
 * These sit under the runtime rather than in it, so they are exercised through
 * the C API directly. The through-line is the object framework: one opaque
 * oak_container_t handle, a hidden header carrying the type and allocator, and
 * operations that return 0/null for anything the concrete type does not
 * support instead of invoking undefined behaviour.
 */

#include "oak_test_support.h"

#include "oak_container.h"
#include "oak_hash_map.h"
#include "oak_hash_set.h"
#include "oak_object.h"
#include "oak_vector.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

OAK_TEST_SUITE(containers);

/* Type identity walks the chain vector -> container -> object, and the
 * interface query reports only what a vector actually implements. */
UTEST_F(containers, a_vector_reports_its_type_and_interfaces)
{
  oak_container_t* v = oak_vector_new(OAK_A, sizeof(int));

  ASSERT_TRUE(v != null);
  EXPECT_EQ(0u, oak_size(v));
  EXPECT_EQ(0u, oak_capacity(v));
  EXPECT_EQ(sizeof(int), oak_vector_elem_size(v));

  EXPECT_TRUE(oak_is(v, oak_vector_type_info()));
  EXPECT_TRUE(oak_is(v, oak_base_type_info()));
  EXPECT_FALSE(oak_is(v, oak_hash_map_type_info()));
  EXPECT_STREQ("vector", oak_type_name(v));

  EXPECT_TRUE(oak_query_interface(v, OAK_IID_SEQUENCE) != null);
  EXPECT_TRUE(oak_query_interface(v, OAK_IID_RANDOM_ACCESS) != null);
  EXPECT_TRUE(oak_query_interface(v, OAK_IID_ITERABLE) != null);
  EXPECT_TRUE(oak_query_interface(v, OAK_IID_MAP) == null);
  EXPECT_TRUE(oak_query_interface(v, OAK_IID_SET) == null);

  oak_destroy(v);
}

UTEST_F(containers, a_vector_grows_and_keeps_its_elements_contiguous)
{
  oak_container_t* v = oak_vector_new(OAK_A, sizeof(int));
  const int* data;
  int i;
  usize u;

  ASSERT_TRUE(v != null);

  /* Past the initial 8 and several doublings. */
  for (i = 0; i < 40; ++i)
    ASSERT_TRUE(oak_push_back(v, &i));
  EXPECT_EQ(40u, oak_size(v));
  EXPECT_TRUE(oak_capacity(v) >= 40u);

  data = OAK_CDATA(int, v);
  ASSERT_TRUE(data != null);
  for (i = 0; i < 40; ++i)
    EXPECT_EQ(i, data[i]);
  EXPECT_EQ(7, *(const int*)oak_cget(v, 7));
  /* Out-of-range access reports null rather than reading past the end. */
  EXPECT_TRUE(oak_cget(v, 40) == null);

  ASSERT_TRUE(oak_reserve(v, 100));
  EXPECT_TRUE(oak_capacity(v) >= 100u);
  EXPECT_EQ(40u, oak_size(v));

  /* Growing via resize zero-fills the new tail. */
  ASSERT_TRUE(oak_resize(v, 45));
  for (u = 40; u < 45; ++u)
    EXPECT_EQ(0, OAK_AT(int, v, u));

  oak_destroy(v);
}

UTEST_F(containers, vector_insert_and_erase_shift_the_tail)
{
  oak_container_t* v = oak_vector_new(OAK_A, sizeof(int));
  const int inserted = 99;
  int i;
  int popped = -1;

  ASSERT_TRUE(v != null);
  for (i = 0; i < 3; ++i)
    ASSERT_TRUE(oak_push_back(v, &i));

  ASSERT_TRUE(oak_insert(v, 1, &inserted));
  EXPECT_EQ(4u, oak_size(v));
  EXPECT_EQ(0, OAK_AT(int, v, 0));
  EXPECT_EQ(99, OAK_AT(int, v, 1));
  EXPECT_EQ(1, OAK_AT(int, v, 2));

  ASSERT_TRUE(oak_erase(v, 1));
  EXPECT_EQ(3u, oak_size(v));
  EXPECT_EQ(1, OAK_AT(int, v, 1));
  EXPECT_FALSE(oak_erase(v, 3));

  /* Inserting at size appends; one past it is rejected. */
  ASSERT_TRUE(oak_insert(v, oak_size(v), &inserted));
  EXPECT_EQ(99, OAK_AT(int, v, 3));
  EXPECT_FALSE(oak_insert(v, oak_size(v) + 1, &inserted));

  ASSERT_TRUE(oak_pop_back(v, &popped));
  EXPECT_EQ(99, popped);

  oak_destroy(v);
}

UTEST_F(containers, vector_iteration_visits_every_element_in_order)
{
  oak_container_t* v = oak_vector_new(OAK_A, sizeof(int));
  oak_iterator_t it;
  oak_iterator_t empty;
  oak_iterator_t detached;
  int i;
  int seen = 0;
  int expected = 0;

  ASSERT_TRUE(v != null);
  for (i = 0; i < 10; ++i)
    ASSERT_TRUE(oak_push_back(v, &i));

  for (it = oak_begin(v); oak_iter_get(&it); oak_next(&it))
  {
    EXPECT_EQ(expected, *(const int*)oak_iter_get(&it));
    ++expected;
    ++seen;
  }
  EXPECT_EQ(oak_size(v), (usize)seen);

  /* A vector has no keys, and an iterator with no owner yields nothing. */
  memset(&detached, 0, sizeof(detached));
  EXPECT_TRUE(oak_iter_key(&detached, null) == null);

  oak_clear(v);
  EXPECT_EQ(0u, oak_size(v));
  /* clear releases the elements but keeps the buffer. */
  EXPECT_TRUE(oak_capacity(v) >= 10u);
  EXPECT_FALSE(oak_pop_back(v, null));

  /* An empty container yields an immediately exhausted cursor. */
  empty = oak_begin(v);
  EXPECT_TRUE(oak_iter_get(&empty) == null);
  EXPECT_FALSE(oak_next(&empty));

  oak_destroy(v);
}

/* Unsupported operations report failure rather than corrupting the container:
 * a vector has no keys, so every keyed call is a no-op. */
UTEST_F(containers, keyed_operations_are_refused_by_a_vector)
{
  oak_container_t* v = oak_vector_new(OAK_A, sizeof(int));
  const int value = 1;

  ASSERT_TRUE(v != null);
  EXPECT_FALSE(oak_put(v, "k", 1, &value));
  EXPECT_TRUE(oak_find(v, "k", 1) == null);
  EXPECT_FALSE(oak_add(v, "k", 1));
  EXPECT_FALSE(oak_contains(v, "k", 1));
  EXPECT_FALSE(oak_erase_key(v, "k", 1));
  EXPECT_EQ(0u, oak_size(v));

  oak_destroy(v);
}

UTEST_F(containers, vector_construction_validates_its_arguments)
{
  EXPECT_TRUE(oak_vector_new(OAK_A, 0) == null);
  EXPECT_TRUE(oak_vector_new(null, sizeof(int)) == null);
}

/* Keys are borrowed byte ranges, so every key here is a string literal that
 * outlives the map. */
static const char* const map_keys[] = { "alpha", "beta",  "gamma",  "delta",
                                        "epsilon", "zeta", "eta",   "theta",
                                        "iota",  "kappa", "lambda", "mu" };

UTEST_F(containers, a_hash_map_stores_finds_and_overwrites)
{
  oak_container_t* m = oak_hash_map_new(OAK_A, sizeof(int));
  const int key_count = (int)oak_count_of(map_keys);
  const int replaced = 4242;
  int i;

  ASSERT_TRUE(m != null);
  EXPECT_EQ(0u, oak_size(m));
  EXPECT_EQ(sizeof(int), oak_hash_map_value_size(m));
  EXPECT_TRUE(oak_is(m, oak_hash_map_type_info()));
  EXPECT_TRUE(oak_query_interface(m, OAK_IID_MAP) != null);
  EXPECT_TRUE(oak_query_interface(m, OAK_IID_RANDOM_ACCESS) == null);

  for (i = 0; i < key_count; ++i)
    ASSERT_TRUE(oak_put_str(m, map_keys[i], &i));
  EXPECT_EQ((usize)key_count, oak_size(m));

  for (i = 0; i < key_count; ++i)
  {
    const int* found = oak_cfind_str(m, map_keys[i]);
    ASSERT_TRUE(found != null);
    EXPECT_EQ(i, *found);
    EXPECT_TRUE(oak_contains_str(m, map_keys[i]));
  }
  EXPECT_TRUE(oak_cfind_str(m, "absent") == null);
  EXPECT_FALSE(oak_contains_str(m, "absent"));

  /* Putting an existing key replaces the value; it does not add an entry. */
  ASSERT_TRUE(oak_put_str(m, "gamma", &replaced));
  EXPECT_EQ((usize)key_count, oak_size(m));
  EXPECT_EQ(replaced, *(const int*)oak_cfind_str(m, "gamma"));

  oak_destroy(m);
}

/*
 * Removal leaves a tombstone. Lookups must probe past it, re-insertion must
 * reuse it, and a rehash must drop it -- otherwise the table slowly fills with
 * tombstones until probing never terminates.
 */
UTEST_F(containers, hash_map_tombstones_are_probed_past_reused_and_reclaimed)
{
  oak_container_t* m = oak_hash_map_new(OAK_A, sizeof(int));
  const int key_count = (int)oak_count_of(map_keys);
  const int reinserted = 7;
  int i;
  int round;

  ASSERT_TRUE(m != null);
  for (i = 0; i < key_count; ++i)
    ASSERT_TRUE(oak_put_str(m, map_keys[i], &i));

  ASSERT_TRUE(oak_erase_key_str(m, "beta"));
  EXPECT_FALSE(oak_erase_key_str(m, "beta")); /* already gone */
  EXPECT_EQ((usize)key_count - 1u, oak_size(m));
  EXPECT_TRUE(oak_cfind_str(m, "beta") == null);

  /* Everything else must still be reachable across the tombstone. */
  for (i = 0; i < key_count; ++i)
  {
    if (strcmp(map_keys[i], "beta") == 0)
      continue;
    EXPECT_TRUE(oak_contains_str(m, map_keys[i]));
  }

  ASSERT_TRUE(oak_put_str(m, "beta", &reinserted));
  EXPECT_EQ((usize)key_count, oak_size(m));
  EXPECT_EQ(reinserted, *(const int*)oak_cfind_str(m, "beta"));

  /* Enough churn to force a rehash that has to reclaim tombstones. */
  for (round = 0; round < 20; ++round)
  {
    for (i = 0; i < key_count; ++i)
    {
      ASSERT_TRUE(oak_erase_key_str(m, map_keys[i]));
      ASSERT_TRUE(oak_put_str(m, map_keys[i], &i));
    }
  }
  EXPECT_EQ((usize)key_count, oak_size(m));

  oak_destroy(m);
}

UTEST_F(containers, hash_map_iteration_reaches_every_entry_once)
{
  oak_container_t* m = oak_hash_map_new(OAK_A, sizeof(int));
  const int key_count = (int)oak_count_of(map_keys);
  oak_iterator_t it;
  int visited = 0;
  int i;

  ASSERT_TRUE(m != null);
  for (i = 0; i < key_count; ++i)
    ASSERT_TRUE(oak_put_str(m, map_keys[i], &i));

  for (it = oak_begin(m); oak_iter_get(&it); oak_next(&it))
  {
    usize key_len = 0;
    const char* key = oak_iter_key(&it, &key_len);
    ASSERT_TRUE(key != null);
    EXPECT_EQ(strlen(key), key_len);
    EXPECT_TRUE(oak_contains(m, key, key_len));
    ++visited;
  }
  EXPECT_EQ(oak_size(m), (usize)visited);

  oak_clear(m);
  EXPECT_EQ(0u, oak_size(m));
  EXPECT_TRUE(oak_cfind_str(m, "alpha") == null);

  oak_destroy(m);
}

/* A map has no positions, so every positional call is refused. */
UTEST_F(containers, positional_operations_are_refused_by_a_hash_map)
{
  oak_container_t* m = oak_hash_map_new(OAK_A, sizeof(int));
  const int value = 1;

  ASSERT_TRUE(m != null);
  ASSERT_TRUE(oak_put_str(m, "only", &value));

  EXPECT_FALSE(oak_push_back(m, &value));
  EXPECT_TRUE(oak_get(m, 0) == null);
  EXPECT_TRUE(oak_data(m) == null);
  EXPECT_FALSE(oak_resize(m, 4));
  EXPECT_FALSE(oak_reserve(m, 4));
  EXPECT_FALSE(oak_pop_back(m, null));
  EXPECT_EQ(0u, oak_capacity(m));
  /* None of the refusals disturbed the contents. */
  EXPECT_EQ(1u, oak_size(m));

  oak_destroy(m);
}

UTEST_F(containers, hash_map_construction_validates_its_arguments)
{
  EXPECT_TRUE(oak_hash_map_new(OAK_A, 0) == null);
  EXPECT_TRUE(oak_hash_map_new(null, sizeof(int)) == null);
}

UTEST_F(containers, a_hash_set_holds_each_member_once)
{
  static const char* const names[] = { "one",  "two",   "three", "four", "five",
                                       "six",  "seven", "eight", "nine" };
  oak_container_t* s = oak_hash_set_new(OAK_A);
  const int name_count = (int)oak_count_of(names);
  oak_iterator_t it;
  int visited = 0;
  int i;

  ASSERT_TRUE(s != null);
  EXPECT_TRUE(oak_is(s, oak_hash_set_type_info()));
  EXPECT_TRUE(oak_query_interface(s, OAK_IID_SET) != null);
  EXPECT_TRUE(oak_query_interface(s, OAK_IID_MAP) == null);

  for (i = 0; i < name_count; ++i)
    ASSERT_TRUE(oak_add_str(s, names[i]));
  EXPECT_EQ((usize)name_count, oak_size(s));

  /* Re-adding reports "already present" and does not grow the set. */
  EXPECT_FALSE(oak_add_str(s, "three"));
  EXPECT_EQ((usize)name_count, oak_size(s));

  for (i = 0; i < name_count; ++i)
    EXPECT_TRUE(oak_contains_str(s, names[i]));
  EXPECT_FALSE(oak_contains_str(s, "ten"));

  ASSERT_TRUE(oak_erase_key_str(s, "four"));
  EXPECT_FALSE(oak_contains_str(s, "four"));
  EXPECT_EQ((usize)name_count - 1u, oak_size(s));
  ASSERT_TRUE(oak_add_str(s, "four")); /* reuses the tombstone */
  EXPECT_EQ((usize)name_count, oak_size(s));

  /* A set stores no values, so members arrive through iter_key and iter_get
   * stays null throughout. */
  for (it = oak_begin(s);;)
  {
    usize len = 0;
    const char* member = oak_iter_key(&it, &len);
    if (!member)
      break;
    EXPECT_EQ(strlen(member), len);
    EXPECT_TRUE(oak_contains(s, member, len));
    EXPECT_TRUE(oak_iter_get(&it) == null);
    ++visited;
    oak_next(&it);
  }
  EXPECT_EQ(oak_size(s), (usize)visited);

  /* Value-oriented operations are refused. */
  EXPECT_TRUE(oak_find_str(s, "one") == null);
  EXPECT_FALSE(oak_put_str(s, "one", "x"));

  oak_destroy(s);
  EXPECT_TRUE(oak_hash_set_new(null) == null);
}

/*
 * The header sits immediately before the handle the caller holds. These
 * properties are what let oak_destroy(void*) and oak_type_of(void*) work on
 * any object with no cast and no separately-threaded allocator.
 */
UTEST_F(containers, the_object_header_carries_type_and_allocator)
{
  oak_container_t* v = oak_vector_new(OAK_A, sizeof(int));
  oak_container_t* m = oak_hash_map_new(OAK_A, sizeof(int));
  void* opaque = v;

  ASSERT_TRUE(v != null);
  ASSERT_TRUE(m != null);

  EXPECT_TRUE(oak_type_of(opaque) == oak_vector_type_info());
  EXPECT_TRUE(oak_is(opaque, oak_base_type_info()));
  EXPECT_TRUE(oak_allocator_of(opaque) == OAK_A);
  EXPECT_TRUE(oak_allocator_of(m) == OAK_A);

  /* The header is padded so the body is never handed back misaligned. */
  EXPECT_EQ(0u, (unsigned)((uintptr_t)v % sizeof(void*)));
  EXPECT_EQ(0u, (unsigned)((uintptr_t)m % sizeof(void*)));

  oak_destroy(v);
  oak_destroy(m);
}

/* Element storage is a separate allocation, so growing or rehashing must never
 * move the handle the caller is holding. */
UTEST_F(containers, growth_and_rehash_do_not_move_the_handle)
{
  oak_container_t* v = oak_vector_new(OAK_A, sizeof(int));
  oak_container_t* m = oak_hash_map_new(OAK_A, sizeof(int));
  oak_container_t* const v_before = v;
  oak_container_t* const m_before = m;
  char keys[64][8];
  int i;

  ASSERT_TRUE(v != null);
  ASSERT_TRUE(m != null);

  for (i = 0; i < 64; ++i)
    ASSERT_TRUE(oak_push_back(v, &i));
  EXPECT_TRUE(v == v_before);
  EXPECT_EQ(64u, oak_size(v));
  EXPECT_TRUE(oak_type_of(v) == oak_vector_type_info());

  for (i = 0; i < 64; ++i)
  {
    snprintf(keys[i], sizeof(keys[i]), "k%d", i);
    ASSERT_TRUE(oak_put_str(m, keys[i], &i));
  }
  EXPECT_TRUE(m == m_before);
  EXPECT_EQ(64u, oak_size(m));

  oak_destroy(v);
  oak_destroy(m);
}

/* No allocator involved, so these two run outside the leak-checking fixture. */

/* Every entry point accepts null and answers the empty/no-op result, which is
 * what lets callers skip null checks at the call site. */
UTEST(containers, every_operation_tolerates_a_null_container)
{
  oak_iterator_t it;

  EXPECT_EQ(0u, oak_size(null));
  EXPECT_EQ(0u, oak_capacity(null));
  EXPECT_TRUE(oak_get(null, 0) == null);
  EXPECT_TRUE(oak_data(null) == null);
  EXPECT_TRUE(oak_find(null, "k", 1) == null);
  EXPECT_FALSE(oak_contains(null, "k", 1));
  EXPECT_FALSE(oak_push_back(null, "x"));
  EXPECT_TRUE(oak_type_of(null) == null);
  EXPECT_FALSE(oak_is(null, oak_vector_type_info()));
  EXPECT_TRUE(oak_query_interface(null, OAK_IID_SEQUENCE) == null);
  EXPECT_STREQ("(null)", oak_type_name(null));

  oak_clear(null);
  oak_destroy(null);

  it = oak_begin(null);
  EXPECT_TRUE(oak_iter_get(&it) == null);
  EXPECT_FALSE(oak_next(&it));
}

/* An allocator that can be told to fail, so the growth paths can be checked
 * for leaving the container intact rather than half-grown. */
typedef struct fail_state fail_state_t;
struct fail_state
{
  int fail_alloc;
  int fail_realloc;
};

static void* fail_alloc(oak_allocator_t* self, usize size, oak_source_loc_t at)
{
  fail_state_t* state = self->state;
  (void)at;
  return state->fail_alloc ? null : malloc(size);
}

static void* fail_realloc(oak_allocator_t* self,
                          void* ptr,
                          usize size,
                          oak_source_loc_t at)
{
  fail_state_t* state = self->state;
  (void)at;
  return state->fail_realloc ? null : realloc(ptr, size);
}

static void fail_free(oak_allocator_t* self, void* ptr, oak_source_loc_t at)
{
  (void)self;
  (void)at;
  free(ptr);
}

static int fail_shutdown(oak_allocator_t* self)
{
  (void)self;
  return 0;
}

static oak_allocator_t fail_allocator(fail_state_t* state)
{
  oak_allocator_t allocator;
  allocator.alloc = fail_alloc;
  allocator.realloc = fail_realloc;
  allocator.free = fail_free;
  allocator.shutdown = fail_shutdown;
  allocator.state = state;
  allocator.malloc_fn = null;
  allocator.realloc_fn = null;
  allocator.free_fn = null;
  return allocator;
}

/*
 * Out of memory must be a clean failure, never a partially-applied one. A
 * half-grown container that reports the new size is worse than no growth at
 * all, so each case checks the contents survived intact.
 */
UTEST(containers, allocation_failure_leaves_containers_intact)
{
  fail_state_t state;
  oak_allocator_t failing;
  oak_container_t* v;
  oak_container_t* m;
  oak_container_t* wide;
  static const char* const keys[] = { "a", "b", "c", "d", "e" };
  const int ninth = 9;
  const int extra = 5;
  int i;

  state.fail_alloc = 1;
  state.fail_realloc = 0;
  failing = fail_allocator(&state);

  /* A failed header allocation yields no container at all. */
  EXPECT_TRUE(oak_vector_new(&failing, sizeof(int)) == null);
  EXPECT_TRUE(oak_hash_map_new(&failing, sizeof(int)) == null);
  EXPECT_TRUE(oak_hash_set_new(&failing) == null);

  /* A failed grow must leave size and contents untouched. */
  state.fail_alloc = 0;
  v = oak_vector_new(&failing, sizeof(int));
  ASSERT_TRUE(v != null);
  for (i = 0; i < 8; ++i)
    ASSERT_TRUE(oak_push_back(v, &i));
  EXPECT_EQ(8u, oak_size(v));

  state.fail_realloc = 1;
  EXPECT_FALSE(oak_push_back(v, &ninth));
  EXPECT_EQ(8u, oak_size(v));
  EXPECT_FALSE(oak_reserve(v, 4096));
  EXPECT_FALSE(oak_resize(v, 4096));
  EXPECT_EQ(8u, oak_size(v));
  for (i = 0; i < 8; ++i)
    EXPECT_EQ(i, OAK_AT(int, v, i));
  state.fail_realloc = 0;
  oak_destroy(v);

  /* Same for the table's rehash: every entry stays findable. */
  m = oak_hash_map_new(&failing, sizeof(int));
  ASSERT_TRUE(m != null);
  for (i = 0; i < 5; ++i)
    ASSERT_TRUE(oak_put_str(m, keys[i], &i));
  EXPECT_EQ(5u, oak_size(m));

  state.fail_alloc = 1;
  EXPECT_FALSE(oak_put_str(m, "f", &extra));
  EXPECT_FALSE(oak_put_str(m, "g", &extra));
  EXPECT_EQ(5u, oak_size(m));
  for (i = 0; i < 5; ++i)
    EXPECT_EQ(i, *(const int*)oak_cfind_str(m, keys[i]));
  state.fail_alloc = 0;
  oak_destroy(m);

  /* elem_size * count must be rejected rather than wrapping around. */
  wide = oak_vector_new(&failing, SIZE_MAX / 2);
  ASSERT_TRUE(wide != null);
  EXPECT_FALSE(oak_reserve(wide, 4));
  EXPECT_EQ(0u, oak_size(wide));
  oak_destroy(wide);
}
