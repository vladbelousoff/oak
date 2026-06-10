#include "oak_allocator.h"
#include "oak_count_of.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_value.h"

#include <math.h>

/* Insert/delete churn at constant size must keep probe sequences terminating:
 * tombstones count toward the load factor, so the table rebuilds before the
 * last EMPTY slot disappears. A regression here makes oak_map_get of a
 * missing key spin forever. */
OAK_TEST_DECL(MapChurnKeepsProbesTerminating)
{
  struct oak_obj_map_t* map = oak_map_new(oak_test_allocator());

  for (int i = 0; i < 10000; ++i)
  {
    OAK_CHECK(oak_map_set(map, oak_value_i32(i), oak_value_i32(i)));
    OAK_CHECK(oak_map_delete(map, oak_value_i32(i)) == 1);
  }

  OAK_CHECK(oak_map_get(map, oak_value_i32(-1), null) == 0);
  OAK_CHECK(map->length == 0);

  oak_obj_decref(&map->obj);
  return OAK_TEST_OK;
}

/* +0.0 and -0.0 compare equal, so they must hash to the same map entry. */
OAK_TEST_DECL(MapZeroFloatKeysAreOneEntry)
{
  struct oak_obj_map_t* map = oak_map_new(oak_test_allocator());

  OAK_CHECK(oak_map_set(map, oak_value_f32(-0.0f), oak_value_i32(1)));
  struct oak_value_t out = oak_value_none();
  OAK_CHECK(oak_map_get(map, oak_value_f32(0.0f), &out) == 1);
  OAK_CHECK(oak_is_i32(out) && oak_as_i32(out) == 1);

  OAK_CHECK(oak_map_set(map, oak_value_f32(0.0f), oak_value_i32(2)));
  OAK_CHECK(map->length == 1);
  OAK_CHECK(oak_map_delete(map, oak_value_f32(-0.0f)) == 1);

  oak_obj_decref(&map->obj);
  return OAK_TEST_OK;
}

/* NaN never equals itself, so it is rejected as a key instead of becoming an
 * entry that can never be looked up or deleted. */
OAK_TEST_DECL(MapRejectsNanKeys)
{
  struct oak_obj_map_t* map = oak_map_new(oak_test_allocator());

  OAK_CHECK(oak_map_set(map, oak_value_f32(NAN), oak_value_i32(1)) == 0);
  OAK_CHECK(oak_map_get(map, oak_value_f32(NAN), null) == 0);
  OAK_CHECK(oak_map_has(map, oak_value_f32(NAN)) == 0);
  OAK_CHECK(oak_map_delete(map, oak_value_f32(NAN)) == 0);
  OAK_CHECK(map->length == 0);

  oak_obj_decref(&map->obj);
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(MapChurnKeepsProbesTerminating),
    OAK_TEST_ENTRY(MapZeroFloatKeysAreOneEntry),
    OAK_TEST_ENTRY(MapRejectsNanKeys),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
