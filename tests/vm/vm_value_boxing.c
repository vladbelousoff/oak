#include "oak_allocator.h"
#include "oak_count_of.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_value.h"
#include "oak_vm.h"

OAK_TEST_DECL(ValueSizeIs8Bytes)
{
  OAK_CHECK(sizeof(struct oak_value_t) == 8);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(IntegerRoundTrip)
{
  const int values[] = { 0, 1, -1, 127, -128, 2147483647, -2147483647 - 1 };
  for (int i = 0; i < (int)oak_count_of(values); ++i)
  {
    const struct oak_value_t v = OAK_VALUE_I32(values[i]);
    OAK_CHECK(oak_is_i32(v));
    OAK_CHECK(!oak_is_f32(v));
    OAK_CHECK(!oak_is_bool(v));
    OAK_CHECK(!oak_is_none(v));
    OAK_CHECK(oak_as_i32(v) == values[i]);
  }
  return OAK_TEST_OK;
}

OAK_TEST_DECL(FloatRoundTrip)
{
  const float values[] = { 0.0f, 1.5f, -1.5f, 1e10f, -1e10f, 1e-10f };
  for (int i = 0; i < (int)oak_count_of(values); ++i)
  {
    const struct oak_value_t v = OAK_VALUE_F32(values[i]);
    OAK_CHECK(oak_is_f32(v));
    OAK_CHECK(!oak_is_i32(v));
    OAK_CHECK(oak_as_f32(v) == values[i]);
  }
  return OAK_TEST_OK;
}

OAK_TEST_DECL(BoolRoundTrip)
{
  const struct oak_value_t t = OAK_VALUE_BOOL(1);
  const struct oak_value_t f = OAK_VALUE_BOOL(0);
  OAK_CHECK(oak_is_bool(t));
  OAK_CHECK(oak_is_bool(f));
  OAK_CHECK(oak_as_bool(t) == 1);
  OAK_CHECK(oak_as_bool(f) == 0);
  OAK_CHECK(!oak_is_number(t));
  return OAK_TEST_OK;
}

OAK_TEST_DECL(NoneValue)
{
  const struct oak_value_t v = OAK_VALUE_NONE;
  OAK_CHECK(oak_is_none(v));
  OAK_CHECK(oak_is_none_like(v));
  OAK_CHECK(!oak_is_bool(v));
  OAK_CHECK(!oak_is_number(v));
  return OAK_TEST_OK;
}

OAK_TEST_DECL(ObjectPointerRoundTrip)
{
  struct oak_allocator_t allocator;
  oak_system_allocator_init(&allocator);
  struct oak_obj_string_t* str = oak_string_new(&allocator, "hello");

  const struct oak_value_t v = OAK_VALUE_OBJ(str);
  OAK_CHECK(oak_is_obj(v));
  OAK_CHECK(oak_is_string(v));
  OAK_CHECK(oak_val_obj_ptr(v) == (struct oak_obj_t*)str);
  OAK_CHECK(oak_as_string(v) == str);
  OAK_CHECK(oak_as_string(v)->length == 5);

  oak_obj_decref((struct oak_obj_t*)str);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(WeakPointerRoundTrip)
{
  struct oak_allocator_t allocator;
  oak_system_allocator_init(&allocator);
  struct oak_obj_string_t* str = oak_string_new(&allocator, "weak");

  const struct oak_value_t strong = OAK_VALUE_OBJ(str);
  const struct oak_value_t weak = oak_value_weaken(strong);
  OAK_CHECK(oak_is_weak_obj(weak));
  OAK_CHECK(oak_is_obj(weak));
  OAK_CHECK(oak_val_obj_ptr(weak) == (struct oak_obj_t*)str);

  oak_obj_decref((struct oak_obj_t*)str);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(WeakExpiresWhenObjectDies)
{
  struct oak_allocator_t allocator;
  oak_system_allocator_init(&allocator);
  struct oak_obj_string_t* str = oak_string_new(&allocator, "gone");

  const struct oak_value_t weak = oak_value_weaken(OAK_VALUE_OBJ(str));
  OAK_CHECK(oak_is_obj(weak));
  OAK_CHECK(!oak_is_expired_weak(weak));

  oak_obj_decref((struct oak_obj_t*)str);

  OAK_CHECK(!oak_is_obj(weak));
  OAK_CHECK(oak_is_expired_weak(weak));
  OAK_CHECK(oak_is_none_like(weak));
  return OAK_TEST_OK;
}

OAK_TEST_DECL(WeakStaysExpiredAfterSlotReuse)
{
  struct oak_allocator_t allocator;
  oak_system_allocator_init(&allocator);
  struct oak_obj_string_t* first = oak_string_new(&allocator, "first");
  const u32 first_slot = first->obj.slot_index;

  const struct oak_value_t weak = oak_value_weaken(OAK_VALUE_OBJ(first));
  oak_obj_decref((struct oak_obj_t*)first);

  /* The freed slot is at the head of the freelist, so the next allocation
   * reuses it; the bumped nonce must keep the old weak expired. */
  struct oak_obj_string_t* second = oak_string_new(&allocator, "second");
  OAK_CHECK(second->obj.slot_index == first_slot);
  OAK_CHECK(oak_is_expired_weak(weak));
  OAK_CHECK(oak_is_obj(OAK_VALUE_OBJ(second)));

  oak_obj_decref((struct oak_obj_t*)second);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(PerVmTableIsolationAndRecycle)
{
  struct oak_allocator_t allocator;
  oak_system_allocator_init(&allocator);

  const u32 table = oak_obj_table_acquire();
  OAK_CHECK(table != 0);

  const u32 prev = oak_obj_table_set_current(table);
  struct oak_obj_string_t* scoped = oak_string_new(&allocator, "scoped");
  oak_obj_table_set_current(prev);

  struct oak_obj_string_t* shared = oak_string_new(&allocator, "shared");
  OAK_CHECK(scoped->obj.table_id == table);
  OAK_CHECK(shared->obj.table_id == 0);

  const struct oak_value_t v = OAK_VALUE_OBJ(scoped);
  OAK_CHECK(oak_value_obj_table(v) == table);
  OAK_CHECK(oak_is_string(v));

  const struct oak_value_t weak = oak_value_weaken(v);
  oak_obj_decref((struct oak_obj_t*)scoped);
  OAK_CHECK(oak_is_expired_weak(weak));

  /* Last object already died, so detaching recycles the entry at once. */
  oak_obj_table_detach(table);
  OAK_CHECK(oak_obj_tables[table].state == OAK_OBJ_TABLE_FREE);
  OAK_CHECK(oak_obj_tables[table].slots == null);

  /* A stale weak into the recycled table must still read as expired... */
  OAK_CHECK(oak_is_expired_weak(weak));

  /* ...even after the entry is reacquired and repopulated: fresh slots
   * start above the nonce floor left by the previous incarnation. */
  const u32 again = oak_obj_table_acquire();
  OAK_CHECK(again == table);
  const u32 prev2 = oak_obj_table_set_current(again);
  struct oak_obj_string_t* reborn = oak_string_new(&allocator, "reborn");
  oak_obj_table_set_current(prev2);
  OAK_CHECK(oak_is_expired_weak(weak));
  OAK_CHECK(oak_is_obj(OAK_VALUE_OBJ(reborn)));

  oak_obj_decref((struct oak_obj_t*)reborn);
  oak_obj_table_detach(again);
  oak_obj_decref((struct oak_obj_t*)shared);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(TableRegistryRecyclesUnderChurn)
{
  struct oak_allocator_t allocator;
  oak_system_allocator_init(&allocator);

  /* More cycles than registry entries (256): only recycling can keep
   * acquire from exhausting the registry and falling back to table 0. */
  for (int i = 0; i < 300; ++i)
  {
    const u32 table = oak_obj_table_acquire();
    OAK_CHECK(table != 0);
    const u32 prev = oak_obj_table_set_current(table);
    struct oak_obj_string_t* str = oak_string_new(&allocator, "churn");
    oak_obj_table_set_current(prev);
    oak_obj_decref((struct oak_obj_t*)str);
    oak_obj_table_detach(table);
  }
  return OAK_TEST_OK;
}

OAK_TEST_DECL(ObjectRefcopyCompatibilityRejectsOtherVmTables)
{
  struct oak_allocator_t allocator;
  oak_system_allocator_init(&allocator);

  const u32 table_a = oak_obj_table_acquire();
  const u32 table_b = oak_obj_table_acquire();
  OAK_CHECK(table_a != 0);
  OAK_CHECK(table_b != 0);
  OAK_CHECK(table_a != table_b);

  const u32 prev = oak_obj_table_set_current(table_a);
  struct oak_obj_string_t* vm_owned = oak_string_new(&allocator, "vm");
  oak_obj_table_set_current(prev);

  struct oak_obj_string_t* shared = oak_string_new(&allocator, "shared");

  const struct oak_value_t vm_value = OAK_VALUE_OBJ(vm_owned);
  const struct oak_value_t shared_value = OAK_VALUE_OBJ(shared);
  OAK_CHECK(oak_value_can_refcopy_to_table(vm_value, table_a));
  OAK_CHECK(!oak_value_can_refcopy_to_table(vm_value, table_b));
  OAK_CHECK(oak_value_can_refcopy_to_table(shared_value, table_a));
  OAK_CHECK(
      !oak_value_can_refcopy_to_table(oak_value_weaken(vm_value), table_b));

  const u32 prev_b = oak_obj_table_set_current(table_b);
  struct oak_obj_array_t* array_b = oak_array_new(&allocator);
  struct oak_obj_map_t* map_b = oak_map_new(&allocator);
  oak_obj_table_set_current(prev_b);

  OAK_CHECK(!oak_array_push(array_b, vm_value));
  OAK_CHECK(array_b->length == 0u);
  OAK_CHECK(!oak_array_push(array_b, oak_value_weaken(vm_value)));
  OAK_CHECK(array_b->length == 0u);
  OAK_CHECK(oak_array_push(array_b, shared_value));
  OAK_CHECK(array_b->length == 1u);

  OAK_CHECK(!oak_map_set(map_b, OAK_VALUE_I32(1), vm_value));
  OAK_CHECK(!oak_map_set(map_b, vm_value, OAK_VALUE_I32(1)));
  OAK_CHECK(map_b->length == 0u);
  OAK_CHECK(oak_map_set(map_b, OAK_VALUE_I32(1), shared_value));
  OAK_CHECK(map_b->length == 1u);

  oak_obj_decref((struct oak_obj_t*)array_b);
  oak_obj_decref((struct oak_obj_t*)map_b);
  oak_obj_decref((struct oak_obj_t*)vm_owned);
  oak_obj_decref((struct oak_obj_t*)shared);
  oak_obj_table_detach(table_a);
  oak_obj_table_detach(table_b);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(VmCallRejectsValuesFromAnotherVm)
{
  struct oak_allocator_t allocator;
  oak_system_allocator_init(&allocator);

  struct oak_vm_t vm_a;
  struct oak_vm_t vm_b;
  oak_vm_init(&vm_a, &allocator);
  oak_vm_init(&vm_b, &allocator);
  OAK_CHECK(vm_a.object_table != vm_b.object_table);

  const u32 prev = oak_obj_table_set_current(vm_a.object_table);
  struct oak_obj_string_t* owned_by_a = oak_string_new(&allocator, "vm-a");
  oak_obj_table_set_current(prev);
  const struct oak_value_t a_value = OAK_VALUE_OBJ(owned_by_a);

  /* A dummy chunk is enough to reach oak_vm_call's argument-transfer
   * boundary; the foreign value must be rejected before call dispatch. */
  struct oak_chunk_t dummy_chunk = { 0 };
  vm_b.chunk = &dummy_chunk;
  OAK_CHECK(oak_vm_call(&vm_b, a_value, null, 0, null) == OAK_VM_RUNTIME_ERROR);
  OAK_CHECK(vm_b.sp == vm_b.stack);
  OAK_CHECK(oak_vm_call(&vm_b, oak_value_weaken(a_value), null, 0, null) ==
            OAK_VM_RUNTIME_ERROR);
  OAK_CHECK(vm_b.sp == vm_b.stack);

  oak_obj_decref((struct oak_obj_t*)owned_by_a);
  oak_vm_free(&vm_a);
  oak_vm_free(&vm_b);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(HandleRoundTrip61Bits)
{
  const u64 values[] = { 0u, 1u, 0xDEADBEEFu, (1ull << 61) - 1u };
  for (int i = 0; i < (int)oak_count_of(values); ++i)
  {
    const struct oak_value_t v = oak_value_handle(values[i]);
    OAK_CHECK(oak_is_handle(v));
    OAK_CHECK(oak_value_as_handle(v) == values[i]);
  }
  return OAK_TEST_OK;
}

OAK_TEST_DECL(TagsAreDistinct)
{
  const struct oak_value_t i = OAK_VALUE_I32(42);
  const struct oak_value_t f = OAK_VALUE_F32(42.0f);
  const struct oak_value_t b = OAK_VALUE_BOOL(1);
  const struct oak_value_t n = OAK_VALUE_NONE;

  OAK_CHECK(oak_is_i32(i) && !oak_is_f32(i) && !oak_is_bool(i) &&
            !oak_is_none(i));
  OAK_CHECK(!oak_is_i32(f) && oak_is_f32(f) && !oak_is_bool(f) &&
            !oak_is_none(f));
  OAK_CHECK(!oak_is_i32(b) && !oak_is_f32(b) && oak_is_bool(b) &&
            !oak_is_none(b));
  OAK_CHECK(!oak_is_i32(n) && !oak_is_f32(n) && !oak_is_bool(n) &&
            oak_is_none(n));
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(ValueSizeIs8Bytes),
    OAK_TEST_ENTRY(IntegerRoundTrip),
    OAK_TEST_ENTRY(FloatRoundTrip),
    OAK_TEST_ENTRY(BoolRoundTrip),
    OAK_TEST_ENTRY(NoneValue),
    OAK_TEST_ENTRY(ObjectPointerRoundTrip),
    OAK_TEST_ENTRY(WeakPointerRoundTrip),
    OAK_TEST_ENTRY(WeakExpiresWhenObjectDies),
    OAK_TEST_ENTRY(WeakStaysExpiredAfterSlotReuse),
    OAK_TEST_ENTRY(PerVmTableIsolationAndRecycle),
    OAK_TEST_ENTRY(TableRegistryRecyclesUnderChurn),
    OAK_TEST_ENTRY(ObjectRefcopyCompatibilityRejectsOtherVmTables),
    OAK_TEST_ENTRY(VmCallRejectsValuesFromAnotherVm),
    OAK_TEST_ENTRY(HandleRoundTrip61Bits),
    OAK_TEST_ENTRY(TagsAreDistinct),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
