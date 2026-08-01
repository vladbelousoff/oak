#include "oak_allocator.h"
#include "oak_count_of.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_value.h"
#include "oak_vm.h"

OAK_TEST_DECL(ValueSizeIs8Bytes)
{
  OAK_CHECK(sizeof(struct oak_value_t) == 8);
  OAK_CHECK(OAK_OBJ_TABLE_COUNT == 64u);
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

  struct oak_vm_t vm;
  oak_vm_init(&vm, &allocator);
  const u32 table = vm.object_table;
  OAK_CHECK(table != 0);

  struct oak_obj_string_t* scoped = oak_vm_string_new(&vm, "scoped");

  struct oak_obj_string_t* shared = oak_string_new(&allocator, "shared");
  OAK_CHECK(scoped->obj.table_id == table);
  OAK_CHECK(shared->obj.table_id == 0);

  const struct oak_value_t v = OAK_VALUE_OBJ(scoped);
  OAK_CHECK(oak_value_obj_table(v) == table);
  OAK_CHECK(oak_is_string(v));

  const struct oak_value_t weak = oak_value_weaken(v);
  oak_obj_decref((struct oak_obj_t*)scoped);
  OAK_CHECK(oak_is_expired_weak(weak));

  /* Last object already died, so freeing the VM recycles the entry at once. */
  oak_vm_free(&vm);
  OAK_CHECK(oak_obj_tables[table].state == OAK_OBJ_TABLE_FREE);
  OAK_CHECK(oak_obj_tables[table].slots == null);

  /* A stale weak into the recycled table must still read as expired... */
  OAK_CHECK(oak_is_expired_weak(weak));

  /* ...even after the entry is reacquired and repopulated: fresh slots
   * start above the nonce floor left by the previous incarnation. */
  struct oak_vm_t next_vm;
  oak_vm_init(&next_vm, &allocator);
  const u32 again = next_vm.object_table;
  OAK_CHECK(again == table);
  struct oak_obj_string_t* reborn = oak_vm_string_new(&next_vm, "reborn");
  OAK_CHECK(oak_is_expired_weak(weak));
  OAK_CHECK(oak_is_obj(OAK_VALUE_OBJ(reborn)));

  oak_obj_decref((struct oak_obj_t*)reborn);
  oak_vm_free(&next_vm);
  oak_obj_decref((struct oak_obj_t*)shared);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(TableRegistryRecyclesUnderChurn)
{
  struct oak_allocator_t allocator;
  oak_system_allocator_init(&allocator);

  /* More cycles than registry entries (64): only recycling can keep acquire

   * * from exhausting the registry. */
  for (int i = 0; i < 100; ++i)
  {
    struct oak_vm_t vm;
    oak_vm_init(&vm, &allocator);
    OAK_CHECK(vm.object_table != 0);
    struct oak_obj_string_t* str = oak_vm_string_new(&vm, "churn");
    oak_obj_decref((struct oak_obj_t*)str);
    oak_vm_free(&vm);
  }
  return OAK_TEST_OK;
}

OAK_TEST_DECL(RegistryProvides63VmTables)
{
  struct oak_allocator_t allocator;
  oak_system_allocator_init(&allocator);

  struct oak_vm_t vms[OAK_OBJ_TABLE_COUNT - 1u];
  for (u32 i = 0; i < OAK_OBJ_TABLE_COUNT - 1u; ++i)
  {
    oak_vm_init(&vms[i], &allocator);
    OAK_CHECK(vms[i].object_table == i + 1u);
  }
  for (u32 i = OAK_OBJ_TABLE_COUNT - 1u; i-- > 0u;)
    oak_vm_free(&vms[i]);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(ObjectRefcopyCompatibilityRejectsOtherVmTables)
{
  struct oak_allocator_t allocator;
  oak_system_allocator_init(&allocator);

  struct oak_vm_t vm_a;
  struct oak_vm_t vm_b;
  oak_vm_init(&vm_a, &allocator);
  oak_vm_init(&vm_b, &allocator);
  const u32 table_a = vm_a.object_table;
  const u32 table_b = vm_b.object_table;
  OAK_CHECK(table_a != 0);
  OAK_CHECK(table_b != 0);
  OAK_CHECK(table_a != table_b);

  struct oak_obj_string_t* vm_owned = oak_vm_string_new(&vm_a, "vm");

  struct oak_obj_string_t* shared = oak_string_new(&allocator, "shared");

  const struct oak_value_t vm_value = OAK_VALUE_OBJ(vm_owned);
  const struct oak_value_t shared_value = OAK_VALUE_OBJ(shared);
  OAK_CHECK(oak_value_can_refcopy_to_table(vm_value, table_a));
  OAK_CHECK(!oak_value_can_refcopy_to_table(vm_value, table_b));
  OAK_CHECK(oak_value_can_refcopy_to_table(shared_value, table_a));
  OAK_CHECK(
      !oak_value_can_refcopy_to_table(oak_value_weaken(vm_value), table_b));

  struct oak_obj_array_t* array_b = oak_vm_array_new(&vm_b);
  struct oak_obj_map_t* map_b = oak_vm_map_new(&vm_b);

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
  oak_vm_free(&vm_a);
  oak_vm_free(&vm_b);
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

  struct oak_obj_string_t* owned_by_a = oak_vm_string_new(&vm_a, "vm-a");
  const struct oak_value_t a_value = OAK_VALUE_OBJ(owned_by_a);

  /* A dummy chunk is enough to reach oak_vm_call's argument-transfer
   *
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
    OAK_TEST_ENTRY(RegistryProvides63VmTables),
    OAK_TEST_ENTRY(ObjectRefcopyCompatibilityRejectsOtherVmTables),
    OAK_TEST_ENTRY(VmCallRejectsValuesFromAnotherVm),
    OAK_TEST_ENTRY(HandleRoundTrip61Bits),
    OAK_TEST_ENTRY(TagsAreDistinct),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
