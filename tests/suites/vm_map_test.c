/*
 * Runtime: the object map's hash table.
 *
 * Three regression guards against bugs that are hangs or silent corruption
 * rather than wrong answers, which is why they are tested at the C level
 * instead of through Oak source.
 */

#include "oak_test_support.h"

#include "oak_value.h"

#include <math.h>

OAK_TEST_SUITE(vm_map);

/*
 * Insert/delete churn at constant size. Tombstones count toward the load
 * factor so the table rebuilds before the last EMPTY slot disappears; without
 * that, a lookup of a missing key probes forever and the VM hangs.
 */
UTEST_F(vm_map, churn_keeps_probe_sequences_terminating)
{
  oak_obj_map_t* map = oak_map_new(OAK_A);
  int i;

  for (i = 0; i < 10000; ++i)
  {
    ASSERT_TRUE(oak_map_set(map, oak_value_i32(i), oak_value_i32(i)));
    ASSERT_EQ(1, oak_map_delete(map, oak_value_i32(i)));
  }

  /* The lookup below is the one that hangs if tombstones are mishandled. */
  EXPECT_EQ(0, oak_map_get(map, oak_value_i32(-1), OAK_NULL));
  EXPECT_EQ(0u, map->length);

  oak_obj_decref(&map->obj);
}

/* +0.0 and -0.0 compare equal, so they must hash to the same entry -- or a
 * value stored under one becomes unreachable through the other. */
UTEST_F(vm_map, positive_and_negative_zero_are_the_same_key)
{
  oak_obj_map_t* map = oak_map_new(OAK_A);
  oak_value_t out = oak_value_none();

  ASSERT_TRUE(oak_map_set(map, oak_value_f32(-0.0f), oak_value_i32(1)));
  ASSERT_EQ(1, oak_map_get(map, oak_value_f32(0.0f), &out));
  EXPECT_TRUE(oak_is_i32(out));
  EXPECT_EQ(1, oak_as_i32(out));

  ASSERT_TRUE(oak_map_set(map, oak_value_f32(0.0f), oak_value_i32(2)));
  EXPECT_EQ(1u, map->length);
  EXPECT_EQ(1, oak_map_delete(map, oak_value_f32(-0.0f)));

  oak_obj_decref(&map->obj);
}

/* NaN never equals itself, so accepting it as a key would create an entry that
 * can never be found, deleted, or overwritten. It is rejected instead. */
UTEST_F(vm_map, nan_is_rejected_as_a_key)
{
  oak_obj_map_t* map = oak_map_new(OAK_A);
  const oak_value_t nan_key = oak_value_f32((float)NAN);

  EXPECT_EQ(0, oak_map_set(map, nan_key, oak_value_i32(1)));
  EXPECT_EQ(0, oak_map_get(map, nan_key, OAK_NULL));
  EXPECT_EQ(0, oak_map_has(map, nan_key));
  EXPECT_EQ(0, oak_map_delete(map, nan_key));
  EXPECT_EQ(0u, map->length);

  oak_obj_decref(&map->obj);
}
