#include "oak_count_of.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_value.h"
#include "oak_allocator.h"

OAK_TEST_DECL(ValueSizeIs16Bytes)
{
  OAK_CHECK(sizeof(struct oak_value_t) == 16);
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

OAK_TEST_DECL(TagsAreDistinct)
{
  const struct oak_value_t i = OAK_VALUE_I32(42);
  const struct oak_value_t f = OAK_VALUE_F32(42.0f);
  const struct oak_value_t b = OAK_VALUE_BOOL(1);
  const struct oak_value_t n = OAK_VALUE_NONE;

  OAK_CHECK(oak_is_i32(i) && !oak_is_f32(i) && !oak_is_bool(i) && !oak_is_none(i));
  OAK_CHECK(!oak_is_i32(f) && oak_is_f32(f) && !oak_is_bool(f) && !oak_is_none(f));
  OAK_CHECK(!oak_is_i32(b) && !oak_is_f32(b) && oak_is_bool(b) && !oak_is_none(b));
  OAK_CHECK(!oak_is_i32(n) && !oak_is_f32(n) && !oak_is_bool(n) && oak_is_none(n));
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(ValueSizeIs16Bytes),
    OAK_TEST_ENTRY(IntegerRoundTrip),
    OAK_TEST_ENTRY(FloatRoundTrip),
    OAK_TEST_ENTRY(BoolRoundTrip),
    OAK_TEST_ENTRY(NoneValue),
    OAK_TEST_ENTRY(ObjectPointerRoundTrip),
    OAK_TEST_ENTRY(WeakPointerRoundTrip),
    OAK_TEST_ENTRY(TagsAreDistinct),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
