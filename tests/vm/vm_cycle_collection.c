#include "oak_allocator.h"
#include "oak_count_of.h"
#include "oak_test.h"
#include "oak_test_pipeline.h"
#include "oak_test_run.h"
#include "oak_value.h"

OAK_TEST_DECL(SelfCycleIsCollected)
{
  struct oak_obj_array_t* arr = oak_array_new(oak_test_allocator());
  oak_array_push(arr, OAK_VALUE_OBJ(arr));
  oak_obj_decref(&arr->obj);

  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == 1);
  OAK_CHECK(oak_test_allocator()->cycle_objects == null);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(ReachableCycleIsPreserved)
{
  struct oak_obj_array_t* arr = oak_array_new(oak_test_allocator());
  oak_array_push(arr, OAK_VALUE_OBJ(arr));

  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == 0);
  OAK_CHECK(oak_test_allocator()->cycle_objects == &arr->obj);

  oak_obj_decref(&arr->obj);
  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == 1);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(MultiObjectCycleIsCollected)
{
  struct oak_obj_array_t* a = oak_array_new(oak_test_allocator());
  struct oak_obj_array_t* b = oak_array_new(oak_test_allocator());
  oak_array_push(a, OAK_VALUE_OBJ(b));
  oak_array_push(b, OAK_VALUE_OBJ(a));
  oak_obj_decref(&a->obj);
  oak_obj_decref(&b->obj);

  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == 2);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(RecordArrayCycleIsCollected)
{
  struct oak_obj_record_t* record =
      oak_record_new(oak_test_allocator(), 1, "Node", null, null);
  struct oak_obj_array_t* arr = oak_array_new(oak_test_allocator());
  record->fields[0] = OAK_VALUE_OBJ(arr);
  oak_obj_incref(&arr->obj);
  oak_array_push(arr, OAK_VALUE_OBJ(record));
  oak_obj_decref(&record->obj);
  oak_obj_decref(&arr->obj);

  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == 2);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(MapCycleIsCollected)
{
  struct oak_obj_map_t* map = oak_map_new(oak_test_allocator());
  struct oak_obj_string_t* key =
      oak_string_new(oak_test_allocator(), "self", 4);
  OAK_CHECK(oak_map_set(map, OAK_VALUE_OBJ(key), OAK_VALUE_OBJ(map)));
  oak_obj_decref(&key->obj);
  oak_obj_decref(&map->obj);

  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == 1);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(TraitObjectCycleIsCollected)
{
  struct oak_obj_array_t* value = oak_array_new(oak_test_allocator());
  struct oak_obj_array_t* vtable = oak_array_new(oak_test_allocator());
  struct oak_obj_trait_object_t* trait =
      oak_trait_object_new(oak_test_allocator(), OAK_VALUE_OBJ(value), vtable);
  oak_array_push(value, OAK_VALUE_OBJ(trait));
  oak_obj_decref(&trait->obj);
  oak_obj_decref(&vtable->obj);
  oak_obj_decref(&value->obj);

  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == 3);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DuplicateInternalEdgesAreCollected)
{
  struct oak_obj_array_t* arr = oak_array_new(oak_test_allocator());
  oak_array_push(arr, OAK_VALUE_OBJ(arr));
  oak_array_push(arr, OAK_VALUE_OBJ(arr));
  oak_array_push(arr, OAK_VALUE_OBJ(arr));
  oak_obj_decref(&arr->obj);

  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == 1);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DeadCycleReleasesLiveOutgoingReference)
{
  struct oak_obj_array_t* live = oak_array_new(oak_test_allocator());
  struct oak_obj_array_t* dead = oak_array_new(oak_test_allocator());
  oak_array_push(dead, OAK_VALUE_OBJ(dead));
  oak_array_push(dead, OAK_VALUE_OBJ(live));
  oak_obj_decref(&dead->obj);

  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == 1);
  OAK_CHECK(oak_refcount_load(&live->obj.refcount) == 1);

  oak_obj_decref(&live->obj);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(ReachableRootPreservesDownstreamCycle)
{
  struct oak_obj_array_t* root = oak_array_new(oak_test_allocator());
  struct oak_obj_array_t* a = oak_array_new(oak_test_allocator());
  struct oak_obj_array_t* b = oak_array_new(oak_test_allocator());
  oak_array_push(root, OAK_VALUE_OBJ(a));
  oak_array_push(a, OAK_VALUE_OBJ(b));
  oak_array_push(b, OAK_VALUE_OBJ(a));
  oak_obj_decref(&a->obj);
  oak_obj_decref(&b->obj);

  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == 0);

  oak_obj_decref(&root->obj);
  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == 2);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DeadCycleCollectsOwnedDescendants)
{
  struct oak_obj_array_t* cycle = oak_array_new(oak_test_allocator());
  struct oak_obj_array_t* child = oak_array_new(oak_test_allocator());
  struct oak_obj_array_t* grandchild = oak_array_new(oak_test_allocator());
  oak_array_push(cycle, OAK_VALUE_OBJ(cycle));
  oak_array_push(cycle, OAK_VALUE_OBJ(child));
  oak_array_push(child, OAK_VALUE_OBJ(grandchild));
  oak_obj_decref(&cycle->obj);
  oak_obj_decref(&child->obj);
  oak_obj_decref(&grandchild->obj);

  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == 3);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DeepGraphIsCollectedIteratively)
{
  enum { depth = 128 };
  struct oak_obj_array_t* nodes[depth];
  for (int i = 0; i < depth; ++i)
    nodes[i] = oak_array_new(oak_test_allocator());
  for (int i = 0; i < depth; ++i)
    oak_array_push(nodes[i], OAK_VALUE_OBJ(nodes[(i + 1) % depth]));
  for (int i = 0; i < depth; ++i)
    oak_obj_decref(&nodes[i]->obj);

  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == depth);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(WeakReferenceExpiresAfterCycleCollection)
{
  struct oak_obj_array_t* arr = oak_array_new(oak_test_allocator());
  const struct oak_value_t weak = oak_value_weaken(OAK_VALUE_OBJ(arr));
  oak_value_incref(weak);
  oak_array_push(arr, OAK_VALUE_OBJ(arr));
  oak_obj_decref(&arr->obj);

  OAK_CHECK(oak_collect_cycles(oak_test_allocator()) == 1);
  OAK_CHECK(oak_is_expired_weak(weak));
  oak_value_decref(weak);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(RetainedDecrementsTriggerPeriodicCollection)
{
  for (int i = 0; i < 256; ++i)
  {
    struct oak_obj_array_t* arr = oak_array_new(oak_test_allocator());
    oak_array_push(arr, OAK_VALUE_OBJ(arr));
    oak_obj_decref(&arr->obj);
  }
  OAK_CHECK(oak_test_allocator()->cycle_objects == null);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(LanguageSelfCycleIsCollectedAtVmTeardown)
{
  return expect_ok("record Node {\n"
                   "  links : Node[];\n"
                   "}\n"
                   "let mut node = new Node {\n"
                   "  links : [] as Node[]\n"
                   "};\n"
                   "node.links.push(node);\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(SelfCycleIsCollected),
    OAK_TEST_ENTRY(ReachableCycleIsPreserved),
    OAK_TEST_ENTRY(MultiObjectCycleIsCollected),
    OAK_TEST_ENTRY(RecordArrayCycleIsCollected),
    OAK_TEST_ENTRY(MapCycleIsCollected),
    OAK_TEST_ENTRY(TraitObjectCycleIsCollected),
    OAK_TEST_ENTRY(DuplicateInternalEdgesAreCollected),
    OAK_TEST_ENTRY(DeadCycleReleasesLiveOutgoingReference),
    OAK_TEST_ENTRY(ReachableRootPreservesDownstreamCycle),
    OAK_TEST_ENTRY(DeadCycleCollectsOwnedDescendants),
    OAK_TEST_ENTRY(DeepGraphIsCollectedIteratively),
    OAK_TEST_ENTRY(WeakReferenceExpiresAfterCycleCollection),
    OAK_TEST_ENTRY(RetainedDecrementsTriggerPeriodicCollection),
    OAK_TEST_ENTRY(LanguageSelfCycleIsCollectedAtVmTeardown),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
