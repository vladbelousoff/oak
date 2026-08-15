/*
 * Runtime: the 8-byte packed value, weak references, and object tables.
 *
 * These are representation invariants rather than language behaviour, so they
 * poke the C API directly. The properties here are the ones that make the
 * whole no-cycle-collector design safe: a weak reference must never resurrect,
 * not even when its slot is reused, and a value from one VM's table must never
 * be storable in another's.
 *
 * Ported to the tracking allocator, so every object created here is also
 * checked for leaks by the fixture teardown.
 */

#include "oak_test_support.h"

/* oak_chunk_impl.h is the private header: this suite stack-allocates a chunk
 * to prime a VM for oak_vm_call, which the public API has no way to express.
 * See vm_call_rejects_a_callable_from_another_vm below. */
#include "oak_chunk_impl.h"
#include "oak_value.h"

OAK_TEST_SUITE(vm_value);

/* The scalar tests below allocate nothing, so they use plain UTEST rather than
 * the leak-checking fixture -- there is nothing for it to check. */

/* The whole design rests on a value being one machine word with a 3-bit tag,
 * and on the table id fitting the registry. */
UTEST(vm_value, value_is_one_word_and_the_registry_is_64_tables)
{
  ASSERT_EQ(8u, (unsigned)sizeof(oak_value_t));
  ASSERT_EQ(64u, (unsigned)OAK_OBJ_TABLE_COUNT);
}

UTEST(vm_value, integers_round_trip_across_the_i32_range)
{
  static const int values[] = {
    0, 1, -1, 127, -128, 2147483647, -2147483647 - 1
  };
  usize i;

  for (i = 0; i < OAK_COUNT_OF(values); ++i)
  {
    const oak_value_t v = OAK_VALUE_I32(values[i]);
    EXPECT_TRUE(oak_is_i32(v));
    EXPECT_FALSE(oak_is_f32(v));
    EXPECT_FALSE(oak_is_bool(v));
    EXPECT_FALSE(oak_is_none(v));
    EXPECT_EQ(values[i], oak_as_i32(v));
  }
}

UTEST(vm_value, floats_round_trip)
{
  static const float values[] = { 0.0f, 1.5f, -1.5f, 1e10f, -1e10f, 1e-10f };
  usize i;

  for (i = 0; i < OAK_COUNT_OF(values); ++i)
  {
    const oak_value_t v = OAK_VALUE_F32(values[i]);
    EXPECT_TRUE(oak_is_f32(v));
    EXPECT_FALSE(oak_is_i32(v));
    EXPECT_EQ(values[i], oak_as_f32(v));
  }
}

UTEST(vm_value, bools_and_none_are_their_own_tags)
{
  const oak_value_t t = OAK_VALUE_BOOL(1);
  const oak_value_t f = OAK_VALUE_BOOL(0);
  const oak_value_t n = OAK_VALUE_NONE;

  EXPECT_TRUE(oak_is_bool(t));
  EXPECT_TRUE(oak_is_bool(f));
  EXPECT_EQ(1, oak_as_bool(t));
  EXPECT_EQ(0, oak_as_bool(f));
  EXPECT_FALSE(oak_is_number(t));

  EXPECT_TRUE(oak_is_none(n));
  EXPECT_TRUE(oak_is_none_like(n));
  EXPECT_FALSE(oak_is_bool(n));
  EXPECT_FALSE(oak_is_number(n));
}

/* Exactly one predicate answers true for each tag; overlapping tags would make
 * every type check in the VM unreliable. */
UTEST(vm_value, tags_are_mutually_exclusive)
{
  const oak_value_t i = OAK_VALUE_I32(42);
  const oak_value_t f = OAK_VALUE_F32(42.0f);
  const oak_value_t b = OAK_VALUE_BOOL(1);
  const oak_value_t n = OAK_VALUE_NONE;

  EXPECT_TRUE(oak_is_i32(i) && !oak_is_f32(i) && !oak_is_bool(i) &&
              !oak_is_none(i));
  EXPECT_TRUE(!oak_is_i32(f) && oak_is_f32(f) && !oak_is_bool(f) &&
              !oak_is_none(f));
  EXPECT_TRUE(!oak_is_i32(b) && !oak_is_f32(b) && oak_is_bool(b) &&
              !oak_is_none(b));
  EXPECT_TRUE(!oak_is_i32(n) && !oak_is_f32(n) && !oak_is_bool(n) &&
              oak_is_none(n));
}

UTEST(vm_value, opaque_handles_carry_61_bits)
{
  static const u64 values[] = { 0u, 1u, 0xDEADBEEFu, (1ull << 61) - 1u };
  usize i;

  for (i = 0; i < OAK_COUNT_OF(values); ++i)
  {
    const oak_value_t v = oak_value_handle(values[i]);
    EXPECT_TRUE(oak_is_handle(v));
    EXPECT_EQ(values[i], oak_value_as_handle(v));
  }
}

UTEST_F(vm_value, object_values_resolve_back_to_their_object)
{
  oak_obj_string_t* str = oak_string_new(OAK_A, "hello");
  const oak_value_t v = OAK_VALUE_OBJ(str);

  EXPECT_TRUE(oak_is_obj(v));
  EXPECT_TRUE(oak_is_string(v));
  EXPECT_EQ((oak_obj_t*)str, oak_val_obj_ptr(v));
  EXPECT_EQ(str, oak_as_string(v));
  EXPECT_EQ(5u, (unsigned)oak_as_string(v)->length);

  oak_obj_decref((oak_obj_t*)str);
}

UTEST_F(vm_value, weakening_preserves_the_target_while_it_lives)
{
  oak_obj_string_t* str = oak_string_new(OAK_A, "weak");
  const oak_value_t strong = OAK_VALUE_OBJ(str);
  const oak_value_t weak = oak_value_weaken(strong);

  EXPECT_TRUE(oak_is_weak_obj(weak));
  EXPECT_TRUE(oak_is_obj(weak));
  EXPECT_EQ((oak_obj_t*)str, oak_val_obj_ptr(weak));

  oak_obj_decref((oak_obj_t*)str);
}

UTEST_F(vm_value, a_weak_reference_expires_with_its_target)
{
  oak_obj_string_t* str = oak_string_new(OAK_A, "gone");
  const oak_value_t weak = oak_value_weaken(OAK_VALUE_OBJ(str));

  EXPECT_TRUE(oak_is_obj(weak));
  EXPECT_FALSE(oak_is_expired_weak(weak));

  oak_obj_decref((oak_obj_t*)str);

  EXPECT_FALSE(oak_is_obj(weak));
  EXPECT_TRUE(oak_is_expired_weak(weak));
  EXPECT_TRUE(oak_is_none_like(weak));
}

/*
 * The dangerous case: the freed slot goes to the head of the freelist, so the
 * very next allocation reuses it. Only the per-slot nonce keeps the old weak
 * reference from silently resolving to an unrelated object.
 */
UTEST_F(vm_value, a_weak_reference_does_not_resurrect_when_its_slot_is_reused)
{
  oak_obj_string_t* first = oak_string_new(OAK_A, "first");
  const u32 first_slot = first->obj.slot_index;
  const oak_value_t weak = oak_value_weaken(OAK_VALUE_OBJ(first));
  oak_obj_string_t* second;

  oak_obj_decref((oak_obj_t*)first);

  second = oak_string_new(OAK_A, "second");
  ASSERT_EQ(first_slot, second->obj.slot_index);
  EXPECT_TRUE(oak_is_expired_weak(weak));
  EXPECT_TRUE(oak_is_obj(OAK_VALUE_OBJ(second)));

  oak_obj_decref((oak_obj_t*)second);
}

/* Table 0 is shared (chunk constants, embedder objects); every VM gets its own
 * numbered table, which is recycled when the VM is freed. */
UTEST_F(vm_value, each_vm_owns_a_table_that_is_recycled_on_free)
{
  oak_vm_t vm;
  oak_obj_string_t* scoped;
  oak_obj_string_t* shared;
  oak_value_t v;
  oak_value_t weak;
  u32 table;

  oak_vm_init(&vm, OAK_A);
  table = vm.object_table;
  ASSERT_NE(0u, table);

  scoped = oak_vm_string_new(&vm, "scoped");
  shared = oak_string_new(OAK_A, "shared");
  EXPECT_EQ(table, scoped->obj.table_id);
  EXPECT_EQ(0u, shared->obj.table_id);

  v = OAK_VALUE_OBJ(scoped);
  EXPECT_EQ(table, oak_value_obj_table(v));
  EXPECT_TRUE(oak_is_string(v));

  weak = oak_value_weaken(v);
  oak_obj_decref((oak_obj_t*)scoped);
  EXPECT_TRUE(oak_is_expired_weak(weak));

  /* The last object is already gone, so freeing the VM releases the entry. */
  oak_vm_free(&vm);
  OAK_EXPECT_ENUM(OAK_OBJ_TABLE_FREE, oak_obj_tables[table].state);
  EXPECT_EQ(null, oak_obj_tables[table].slots);
  EXPECT_TRUE(oak_is_expired_weak(weak));

  /* Reacquiring the same entry must not revive the stale weak: fresh slots
   * start above the nonce floor the previous incarnation left behind. */
  {
    oak_vm_t next_vm;
    oak_obj_string_t* reborn;

    oak_vm_init(&next_vm, OAK_A);
    ASSERT_EQ(table, next_vm.object_table);
    reborn = oak_vm_string_new(&next_vm, "reborn");
    EXPECT_TRUE(oak_is_expired_weak(weak));
    EXPECT_TRUE(oak_is_obj(OAK_VALUE_OBJ(reborn)));

    oak_obj_decref((oak_obj_t*)reborn);
    oak_vm_free(&next_vm);
  }

  oak_obj_decref((oak_obj_t*)shared);
}

/* More create/destroy cycles than there are registry entries: without
 * recycling this exhausts the registry and starts failing. */
UTEST_F(vm_value, the_table_registry_survives_churn)
{
  int i;
  for (i = 0; i < 100; ++i)
  {
    oak_vm_t vm;
    oak_obj_string_t* str;

    oak_vm_init(&vm, OAK_A);
    ASSERT_NE(0u, vm.object_table);
    str = oak_vm_string_new(&vm, "churn");
    oak_obj_decref((oak_obj_t*)str);
    oak_vm_free(&vm);
  }
}

/* 64 entries minus the shared table 0 leaves 63 for concurrent VMs. */
UTEST_F(vm_value, sixty_three_vms_can_exist_at_once)
{
  oak_vm_t vms[OAK_OBJ_TABLE_COUNT - 1u];
  u32 i;

  for (i = 0; i < OAK_OBJ_TABLE_COUNT - 1u; ++i)
  {
    oak_vm_init(&vms[i], OAK_A);
    EXPECT_EQ(i + 1u, vms[i].object_table);
  }
  for (i = OAK_OBJ_TABLE_COUNT - 1u; i-- > 0u;)
    oak_vm_free(&vms[i]);
}

/*
 * Cross-VM containment. Objects are reference-counted without synchronization,
 * so a value owned by one VM must never end up reachable from another. Table 0
 * is the deliberate exception: it is shared and read-only in practice.
 */
UTEST_F(vm_value, values_cannot_cross_between_vm_tables)
{
  oak_vm_t vm_a;
  oak_vm_t vm_b;
  oak_obj_string_t* vm_owned;
  oak_obj_string_t* shared;
  oak_obj_array_t* array_b;
  oak_obj_map_t* map_b;
  oak_value_t vm_value;
  oak_value_t shared_value;

  oak_vm_init(&vm_a, OAK_A);
  oak_vm_init(&vm_b, OAK_A);
  ASSERT_NE(vm_a.object_table, vm_b.object_table);

  vm_owned = oak_vm_string_new(&vm_a, "vm");
  shared = oak_string_new(OAK_A, "shared");
  vm_value = OAK_VALUE_OBJ(vm_owned);
  shared_value = OAK_VALUE_OBJ(shared);

  EXPECT_TRUE(oak_value_can_refcopy_to_table(vm_value, vm_a.object_table));
  EXPECT_FALSE(oak_value_can_refcopy_to_table(vm_value, vm_b.object_table));
  /* Table 0 objects are copyable into any VM. */
  EXPECT_TRUE(oak_value_can_refcopy_to_table(shared_value, vm_a.object_table));
  /* Weakening does not launder ownership. */
  EXPECT_FALSE(oak_value_can_refcopy_to_table(oak_value_weaken(vm_value),
                                              vm_b.object_table));

  /* The containers must enforce it too, not just the predicate. */
  array_b = oak_vm_array_new(&vm_b);
  map_b = oak_vm_map_new(&vm_b);

  EXPECT_FALSE(oak_array_push(array_b, vm_value));
  EXPECT_EQ(0u, array_b->length);
  EXPECT_FALSE(oak_array_push(array_b, oak_value_weaken(vm_value)));
  EXPECT_EQ(0u, array_b->length);
  EXPECT_TRUE(oak_array_push(array_b, shared_value));
  EXPECT_EQ(1u, array_b->length);

  /* Rejected as a value and as a key. */
  EXPECT_FALSE(oak_map_set(map_b, OAK_VALUE_I32(1), vm_value));
  EXPECT_FALSE(oak_map_set(map_b, vm_value, OAK_VALUE_I32(1)));
  EXPECT_EQ(0u, map_b->length);
  EXPECT_TRUE(oak_map_set(map_b, OAK_VALUE_I32(1), shared_value));
  EXPECT_EQ(1u, map_b->length);

  oak_obj_decref((oak_obj_t*)array_b);
  oak_obj_decref((oak_obj_t*)map_b);
  oak_obj_decref((oak_obj_t*)vm_owned);
  oak_obj_decref((oak_obj_t*)shared);
  oak_vm_free(&vm_a);
  oak_vm_free(&vm_b);
}

/* The host-call entry point must reject a foreign callable before it sets up a
 * frame, and must leave the stack balanced when it does. */
UTEST_F(vm_value, vm_call_rejects_a_callable_from_another_vm)
{
  oak_vm_t vm_a;
  oak_vm_t vm_b;
  oak_obj_string_t* owned_by_a;
  oak_value_t a_value;
  oak_chunk_t dummy_chunk = { 0 };

  oak_vm_init(&vm_a, OAK_A);
  oak_vm_init(&vm_b, OAK_A);
  ASSERT_NE(vm_a.object_table, vm_b.object_table);

  owned_by_a = oak_vm_string_new(&vm_a, "vm-a");
  a_value = OAK_VALUE_OBJ(owned_by_a);

  /* A dummy chunk is enough to reach the argument-transfer boundary. */
  vm_b.chunk = &dummy_chunk;
  OAK_EXPECT_ENUM(OAK_VM_RUNTIME_ERROR,
                  oak_vm_call(&vm_b, a_value, null, 0, null));
  EXPECT_EQ(vm_b.stack, vm_b.sp);
  OAK_EXPECT_ENUM(
      OAK_VM_RUNTIME_ERROR,
      oak_vm_call(&vm_b, oak_value_weaken(a_value), null, 0, null));
  EXPECT_EQ(vm_b.stack, vm_b.sp);

  oak_obj_decref((oak_obj_t*)owned_by_a);
  oak_vm_free(&vm_a);
  oak_vm_free(&vm_b);
}
