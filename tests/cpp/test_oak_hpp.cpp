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

static bool test_vm_owned_string()
{
  oak::Allocator alloc;
  oak::VM first(alloc);
  oak::VM second(alloc);
  oak::Value value = first.string("owned");

  CHECK(value.is_string());
  CHECK(oak_value_obj_table(value.raw()) == first.raw()->object_table);
  CHECK(!oak_value_can_refcopy_to_table(value.raw(),
                                        second.raw()->object_table));
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

static bool test_bind_typed_fn()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);

  opts.bind_fn("add", [](int a, int b) { return a + b; })
      .bind_fn("positive", [](float value) { return value > 0.0f; })
      .bind_fn("consume", [](bool) {});

  auto result = oak::compile(
      "let x = add(10, 20);\n"
      "let y = positive(x);\n"
      "consume(y);\n",
      opts);
  if (!result.ok())
    for (int i = 0; i < result.error_count(); i++)
      std::printf("  compile error: %s\n", result.error(i).message());
  CHECK(result.ok());

  auto bad = oak::compile("add(true, 20);", opts);
  CHECK(!bad.ok());

  oak::VM vm(alloc);
  CHECK(vm.run(result) == OAK_VM_OK);

  return true;
}

static int typed_double_noexcept(int value) noexcept
{
  return value * 2;
}

static bool test_bind_typed_callable_forms()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);
  int calls = 0;
  bool observed = false;

  opts.bind_fn("double_noexcept", &typed_double_noexcept)
      .bind_fn("next_call", [calls_ptr = &calls, local = 0]() mutable {
        ++*calls_ptr;
        return ++local;
      })
      .bind_fn("observe", [&observed](bool value) { observed = value; });

  auto result = oak::compile(
      "let doubled = double_noexcept(21);\n"
      "let first = next_call();\n"
      "let second = next_call();\n"
      "observe(doubled == 42 && first == 1 && second == 2);\n",
      opts);
  CHECK(result.ok());

  oak::VM vm(alloc);
  CHECK(vm.run(result) == OAK_VM_OK);
  CHECK(calls == 2);
  CHECK(observed);

  CHECK(!oak::compile("double_noexcept();", opts).ok());
  CHECK(!oak::compile("observe(1);", opts).ok());
  CHECK(!oak::compile("let value: bool = double_noexcept(1);", opts).ok());

  return true;
}

struct Vec2
{
  float x;
  float y;

  float sum() const { return x + y; }
  void scale(float factor)
  {
    x *= factor;
    y *= factor;
  }
  float dot(const Vec2& other) const { return x * other.x + y * other.y; }
};

struct OtherVec
{
  float x;
};

// Binds a make_vec(x, y) factory returning a native Vec2 record. Instances
// are new-allocated to match TypeBuilder::destructor()'s delete.
static void bind_make_vec(oak::CompileOptions& opts, oak_bind_type_t* raw_type)
{
  opts.bind_fn(
      "make_vec", 2,
      [raw_type](oak::Context& ctx, oak::Args a) -> oak::Value {
        auto* v = new Vec2{a[0].as_f32(), a[1].as_f32()};
        return ctx.native_record(raw_type, v);
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

static bool test_bind_typed_methods()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);

  auto vec_type = opts.bind_type<Vec2>("Vec2");
  vec_type.field("x", &Vec2::x)
      .field("y", &Vec2::y)
      .method("sum", &Vec2::sum)
      .method("scale", &Vec2::scale)
      .method("dot", &Vec2::dot)
      .static_method("twice", [](int value) { return value * 2; })
      .destructor();

  bind_make_vec(opts, vec_type.raw());
  opts.bind_fn("vec_sum", [](const Vec2* value) {
    return value->x + value->y;
  });

  auto result = oak::compile(
      "let mut v = make_vec(3.0, 4.0);\n"
      "let other = make_vec(1.0, 2.0);\n"
      "v.scale(2.0);\n"
      "let sum = v.sum();\n"
      "let dot_value = v.dot(other);\n"
      "let global_sum = vec_sum(v);\n"
      "let doubled = Vec2.twice(sum);\n",
      opts);
  if (!result.ok())
    for (int i = 0; i < result.error_count(); i++)
      std::printf("  compile error: %s\n", result.error(i).message());
  CHECK(result.ok());

  auto bad = oak::compile(
      "let mut v = make_vec(3.0, 4.0);\n"
      "v.scale(false);\n",
      opts);
  CHECK(!bad.ok());

  auto bad_native_arg = oak::compile("vec_sum(false);", opts);
  CHECK(!bad_native_arg.ok());
  CHECK(!oak::compile(
             "let v = make_vec(1.0, 2.0);\n"
             "v.dot(false);\n",
             opts)
             .ok());

  oak::VM vm(alloc);
  CHECK(vm.run(result) == OAK_VM_OK);

  return true;
}

static bool test_bind_typed_native_record_forms()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);
  float observed_x = 0.0f;
  float observed_y = 0.0f;

  auto vec_type = opts.bind_type<Vec2>("Vec2");
  vec_type.field("x", &Vec2::x)
      .field("y", &Vec2::y)
      .static_method("x_of", [](const Vec2& value) { return value.x; })
      .destructor();
  opts.bind_type<OtherVec>("OtherVec").field("x", &OtherVec::x).destructor();

  bind_make_vec(opts, vec_type.raw());
  opts.bind_fn("shift_ref", [](Vec2& value, float amount) {
        value.x += amount;
      })
      .bind_fn("shift_ptr", [](Vec2* value, float amount) {
        value->y += amount;
      })
      .bind_fn("observe_vec", [&observed_x, &observed_y](const Vec2* value) {
        observed_x = value->x;
        observed_y = value->y;
      })
      .bind_fn("other_x", [](const OtherVec& value) { return value.x; });

  auto result = oak::compile(
      "let mut v = make_vec(1.0, 2.0);\n"
      "shift_ref(v, 3.0);\n"
      "shift_ptr(v, 5.0);\n"
      "let x = Vec2.x_of(v);\n"
      "observe_vec(v);\n",
      opts);
  CHECK(result.ok());

  oak::VM vm(alloc);
  CHECK(vm.run(result) == OAK_VM_OK);
  CHECK(observed_x == 4.0f);
  CHECK(observed_y == 7.0f);

  CHECK(!oak::compile(
             "let v = make_vec(1.0, 2.0);\n"
             "other_x(v);\n",
             opts)
             .ok());
  CHECK(!oak::compile("shift_ref(false, 1.0);", opts).ok());
  CHECK(!oak::compile("Vec2.x_of(false);", opts).ok());

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

static bool test_parse_and_ast_traversal()
{
  oak::Allocator alloc;
  auto result = oak::parse("let answer = 40 + 2;", alloc);

  CHECK(result.ok());
  CHECK(result.lexer_error_count() == 0);
  CHECK(result.error_count() == 0);

  oak::AstNode root = result.root();
  CHECK(root.kind() == OAK_NODE_PROGRAM);
  CHECK(std::strcmp(root.kind_name(), "PROGRAM") == 0);
  CHECK(root.child_count() == 1);
  CHECK(root.children().size() == 1);

  int direct_children = 0;
  for (oak::AstNode child : root.children())
  {
    CHECK(child.kind() == OAK_NODE_STMT_LET_ASSIGNMENT);
    direct_children++;
  }
  CHECK(direct_children == 1);

  std::vector<oak_node_kind_t> kinds;
  std::string ident;
  int terminal_count = 0;
  oak_token_kind_t ident_token_kind = OAK_TOKEN_AT;
  int ident_line = 0;
  int ident_column = 0;
  int ident_offset = 0;
  oak::walk(root, [&](oak::AstNode node) {
    kinds.push_back(node.kind());
    if (node.is_terminal())
    {
      terminal_count++;
      if (node.kind() == OAK_NODE_IDENT)
      {
        ident = node.text();
        ident_token_kind = node.token_kind();
        ident_line = node.line();
        ident_column = node.column();
        ident_offset = node.offset();
      }
    }
  });

  CHECK(!kinds.empty());
  CHECK(kinds.front() == OAK_NODE_PROGRAM);
  CHECK(terminal_count == 3);
  CHECK(ident == "answer");
  CHECK(ident_token_kind == OAK_TOKEN_IDENT);
  CHECK(ident_line == 1);
  CHECK(ident_column == 5);
  CHECK(ident_offset == 5);

  return true;
}

static bool test_parse_error_and_move()
{
  oak::Allocator alloc;
  auto bad = oak::parse("let value = ;", alloc);

  CHECK(!bad.ok());
  CHECK(!bad.root());
  CHECK(bad.error_count() > 0);
  CHECK(std::strlen(bad.error(0).message()) > 0);

  auto parsed = oak::parse("1 + 2", alloc, OAK_NODE_EXPR);
  CHECK(parsed.ok());
  CHECK(parsed.root().kind() == OAK_NODE_BINARY_ADD);

  oak::ParseResult moved = std::move(parsed);
  CHECK(moved.ok());
  CHECK(!parsed.root());

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
      {"vm_owned_string", test_vm_owned_string},
      {"compile_and_run", test_compile_and_run},
      {"compile_error", test_compile_error},
      {"bind_native_fn", test_bind_native_fn},
      {"bind_multiple_fns", test_bind_multiple_fns},
      {"bind_typed_fn", test_bind_typed_fn},
      {"bind_typed_callable_forms", test_bind_typed_callable_forms},
      {"bind_type_field_access", test_bind_type_field_access},
      {"bind_type_method", test_bind_type_method},
      {"bind_typed_methods", test_bind_typed_methods},
      {"bind_typed_native_record_forms", test_bind_typed_native_record_forms},
      {"number_coercion", test_number_coercion},
      {"bind_enum", test_bind_enum},
      {"source_name", test_source_name},
      {"compile_result_move", test_compile_result_move},
      {"parse_and_ast_traversal", test_parse_and_ast_traversal},
      {"parse_error_and_move", test_parse_error_and_move},
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
