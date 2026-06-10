#include "oak.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define CHECK(expr)                                                            \
  do                                                                           \
  {                                                                            \
    g_tests_run++;                                                             \
    if (!(expr))                                                               \
    {                                                                          \
      std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #expr, __FILE__, __LINE__);  \
      return false;                                                            \
    }                                                                          \
    g_tests_passed++;                                                          \
  } while (0)

static bool test_value_basics()
{
  oak::Value none;
  CHECK(none.is_none());

  oak::Value i = oak::Value::i32(42);
  CHECK(i.is_i32());
  CHECK(i.as_i32() == 42);

  oak::Value f = oak::Value::f32(3.14f);
  CHECK(f.is_f32());
  CHECK(f.as_f32() > 3.13f && f.as_f32() < 3.15f);

  oak::Value b = oak::Value::boolean(true);
  CHECK(b.is_bool());
  CHECK(b.as_bool());

  // Implicit conversions
  oak::Value ii = 10;
  CHECK(ii.is_i32());
  CHECK(ii.as_i32() == 10);

  oak::Value ff = 2.5f;
  CHECK(ff.is_f32());

  oak::Value bb = true;
  CHECK(bb.is_bool());

  // Copy
  oak::Value copy = i;
  CHECK(copy.is_i32());
  CHECK(copy.as_i32() == 42);

  // Move
  oak::Value moved = std::move(copy);
  CHECK(moved.is_i32());
  CHECK(moved.as_i32() == 42);
  CHECK(copy.is_none());

  return true;
}

static bool test_value_string()
{
  oak::Allocator alloc;
  oak::Value s = oak::Value::string(alloc, "hello");
  CHECK(s.is_string());
  CHECK(s.as_string() == "hello");
  CHECK(s.as_string().length() == 5);

  // Copy preserves refcount
  oak::Value s2 = s;
  CHECK(s2.is_string());
  CHECK(s2.as_string() == "hello");

  return true;
}

static bool test_compile_and_run()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);

  auto result = oak::compile("let x = 1 + 2;", opts);
  if (!result.ok())
  {
    for (int i = 0; i < result.error_count(); i++)
      std::printf("  compile error [%d:%d]: %s\n", result.error(i).line(),
                  result.error(i).column(), result.error(i).message());
  }
  CHECK(result.ok());
  CHECK(result.chunk() != null);

  oak::VM vm(alloc);
  auto r = vm.run(result);
  CHECK(r == OAK_VM_OK);

  return true;
}

static bool test_compile_error()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);

  auto result = oak::compile("let x: number = 'bad';", opts);
  CHECK(!result.ok());
  CHECK(result.error_count() > 0);

  auto diag = result.error(0);
  CHECK(std::strlen(diag.message()) > 0);

  return true;
}

static bool test_bind_native_fn()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);

  opts.bind_fn(
      "add", 2,
      [](oak::Context&, oak::Args a) -> oak::Value {
        return oak::Value::i32(a[0].as_i32() + a[1].as_i32());
      },
      OAK_BIND_SCALAR(OAK_TYPE_NUMBER));

  auto result = oak::compile("let x = add(10, 20);", opts);
  if (!result.ok())
  {
    for (int i = 0; i < result.error_count(); i++)
      std::printf("  compile error: %s\n", result.error(i).message());
  }
  CHECK(result.ok());

  oak::VM vm(alloc);
  auto r = vm.run(result);
  CHECK(r == OAK_VM_OK);

  return true;
}

static bool test_bind_multiple_fns()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);

  opts.bind_fn(
      "double_it", 1,
      [](oak::Context&, oak::Args a) -> oak::Value {
        return oak::Value::i32(a[0].as_i32() * 2);
      },
      OAK_BIND_SCALAR(OAK_TYPE_NUMBER));

  opts.bind_fn(
      "negate", 1,
      [](oak::Context&, oak::Args a) -> oak::Value {
        return oak::Value::i32(-a[0].as_i32());
      },
      OAK_BIND_SCALAR(OAK_TYPE_NUMBER));

  auto result = oak::compile("let x = negate(double_it(5));", opts);
  if (!result.ok())
  {
    for (int i = 0; i < result.error_count(); i++)
      std::printf("  compile error: %s\n", result.error(i).message());
  }
  CHECK(result.ok());

  oak::VM vm(alloc);
  auto r = vm.run(result);
  CHECK(r == OAK_VM_OK);

  return true;
}

struct Vec2
{
  float x;
  float y;
};

// Binds a make_vec(x, y) factory returning a native Vec2 record. Instances
// are new-allocated to match TypeBuilder::destructor()'s delete.
static void bind_make_vec(oak::CompileOptions& opts, oak_bind_type_t* raw_type)
{
  opts.bind_fn(
      "make_vec", 2,
      [raw_type](oak::Context& ctx, oak::Args a) -> oak::Value {
        auto* v = new Vec2{a[0].as_f32(), a[1].as_f32()};
        return oak::Value::from_raw(
            oak_native_record_new(ctx.allocator(), raw_type, v));
      },
      OAK_BIND_NATIVE(raw_type));
}

static bool test_bind_type_field_access()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);

  auto vec_type = opts.bind_type<Vec2>("Vec2");
  vec_type.field("x", &Vec2::x).field("y", &Vec2::y).destructor();
  bind_make_vec(opts, vec_type.raw());

  auto result = oak::compile(
      "let v = make_vec(3.0, 4.0);\n"
      "let sum = v.x + v.y;\n",
      opts);
  if (!result.ok())
  {
    for (int i = 0; i < result.error_count(); i++)
      std::printf("  compile error: %s\n", result.error(i).message());
  }
  CHECK(result.ok());

  oak::VM vm(alloc);
  auto r = vm.run(result);
  CHECK(r == OAK_VM_OK);

  return true;
}

static bool test_bind_type_method()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);

  auto vec_type = opts.bind_type<Vec2>("Vec2");
  vec_type.field("x", &Vec2::x)
      .field("y", &Vec2::y)
      .method(
          "sum", 0,
          [](oak::Context&, oak::Args a) -> oak::Value {
            auto* v = static_cast<Vec2*>(oak_native_instance(a.raw(0)));
            return oak::Value::f32(v->x + v->y);
          },
          OAK_BIND_SCALAR(OAK_TYPE_NUMBER))
      .destructor();

  bind_make_vec(opts, vec_type.raw());

  auto result = oak::compile(
      "let v = make_vec(3.0, 4.0);\n"
      "let s = v.sum();\n",
      opts);
  if (!result.ok())
  {
    for (int i = 0; i < result.error_count(); i++)
      std::printf("  compile error: %s\n", result.error(i).message());
  }
  CHECK(result.ok());

  oak::VM vm(alloc);
  auto r = vm.run(result);
  CHECK(r == OAK_VM_OK);

  return true;
}

static bool test_number_coercion()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);

  auto vec_type = opts.bind_type<Vec2>("Vec2");
  vec_type.field("x", &Vec2::x).field("y", &Vec2::y).destructor();
  bind_make_vec(opts, vec_type.raw());

  // Integer literals are i32-tagged at runtime; both the factory's as_f32()
  // and the float-field setter must coerce instead of asserting.
  auto result = oak::compile(
      "let mut v = make_vec(3, 4);\n"
      "v.x = 7;\n"
      "let s = v.x + v.y;\n",
      opts);
  if (!result.ok())
  {
    for (int i = 0; i < result.error_count(); i++)
      std::printf("  compile error: %s\n", result.error(i).message());
  }
  CHECK(result.ok());

  oak::VM vm(alloc);
  auto r = vm.run(result);
  CHECK(r == OAK_VM_OK);

  return true;
}

static bool test_bind_enum()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);

  opts.bind_enum("Color", {{"Red", 0}, {"Green", 1}, {"Blue", 2}});

  auto result = oak::compile("let c = Color.Green;", opts);
  if (!result.ok())
  {
    for (int i = 0; i < result.error_count(); i++)
      std::printf("  compile error: %s\n", result.error(i).message());
  }
  CHECK(result.ok());

  oak::VM vm(alloc);
  auto r = vm.run(result);
  CHECK(r == OAK_VM_OK);

  return true;
}

static bool test_source_name()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);
  opts.source_name("test_source.oak").debug_info(true);

  auto result = oak::compile("let x = 42;", opts);
  CHECK(result.ok());

  oak::VM vm(alloc);
  auto r = vm.run(result);
  CHECK(r == OAK_VM_OK);

  return true;
}

static bool test_compile_result_move()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);

  auto r1 = oak::compile("let x = 1;", opts);
  CHECK(r1.ok());

  oak::CompileResult r2 = std::move(r1);
  CHECK(r2.ok());
  CHECK(!r1.ok());

  return true;
}

static bool test_many_bindings()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);

  // Closure storage is unbounded; this used to overflow a 256-slot
  // trampoline table.
  constexpr int fn_count = 300;
  std::vector<std::string> names;
  names.reserve(fn_count); // c_str pointers are borrowed until compile
  for (int i = 0; i < fn_count; i++)
  {
    names.push_back("fn_" + std::to_string(i));
    opts.bind_fn(
        names.back().c_str(), 0,
        [i](oak::Context&, oak::Args) -> oak::Value {
          return oak::Value::i32(i);
        },
        OAK_BIND_SCALAR(OAK_TYPE_NUMBER));
  }

  auto result = oak::compile("let x = fn_0() + fn_150() + fn_299();", opts);
  if (!result.ok())
  {
    for (int i = 0; i < result.error_count(); i++)
      std::printf("  compile error: %s\n", result.error(i).message());
  }
  CHECK(result.ok());

  oak::VM vm(alloc);
  auto r = vm.run(result);
  CHECK(r == OAK_VM_OK);

  return true;
}

static bool test_bind_fn_raw()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);

  opts.bind_fn_raw(
      "noop", 0,
      [](oak_native_ctx_t*, const oak_value_t*, int,
         oak_value_t* out) -> oak_fn_call_result_t {
        *out = oak_value_none();
        return OAK_FN_CALL_OK;
      });

  auto result = oak::compile("noop();", opts);
  CHECK(result.ok());

  oak::VM vm(alloc);
  auto r = vm.run(result);
  CHECK(r == OAK_VM_OK);

  return true;
}

struct TestEntry
{
  const char* name;
  bool (*fn)();
};

int main()
{
  TestEntry tests[] = {
      {"value_basics", test_value_basics},
      {"value_string", test_value_string},
      {"compile_and_run", test_compile_and_run},
      {"compile_error", test_compile_error},
      {"bind_native_fn", test_bind_native_fn},
      {"bind_multiple_fns", test_bind_multiple_fns},
      {"bind_type_field_access", test_bind_type_field_access},
      {"bind_type_method", test_bind_type_method},
      {"number_coercion", test_number_coercion},
      {"bind_enum", test_bind_enum},
      {"source_name", test_source_name},
      {"compile_result_move", test_compile_result_move},
      {"many_bindings", test_many_bindings},
      {"bind_fn_raw", test_bind_fn_raw},
  };

  int passed = 0;
  int failed = 0;
  for (auto& t : tests)
  {
    std::printf("running %s...\n", t.name);
    if (t.fn())
    {
      std::printf("  passed\n");
      passed++;
    }
    else
    {
      std::printf("  FAILED\n");
      failed++;
    }
  }

  std::printf("\n%d/%d tests passed, %d checks total\n", passed,
              passed + failed, g_tests_passed);

  return failed > 0 ? 1 : 0;
}
