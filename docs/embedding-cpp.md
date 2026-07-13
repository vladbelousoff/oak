# Embedding Oak: C++ API

[`include/oak.hpp`](../include/oak.hpp) is a single-header C++20 wrapper over
the [C API](embedding-c.md). It adds RAII, typed callable binding (function
signatures are deduced and type-checked at Oak compile time), and fluent
builders — with no exceptions and no RTTI. Everything shown here is exercised
by [`tests/cpp/test_oak_hpp.cpp`](../tests/cpp/test_oak_hpp.cpp), one test per
feature.

## Compile and run

```cpp
#include "oak.hpp"

oak::Allocator alloc;
oak::CompileOptions opts(alloc);
opts.source_name("host.oak").debug_info(true);

auto result = oak::compile("let x = 1 + 2;", opts);
if (!result.ok())
{
  for (int i = 0; i < result.error_count(); i++)
    std::printf("[%d:%d] %s\n", result.error(i).line(),
                result.error(i).column(), result.error(i).message());
  return;
}

oak::VM vm(alloc);
oak_vm_result_t r = vm.run(result);
```

`oak::CompileResult` owns the chunk and frees it on destruction (movable, not
copyable). `oak::VM::call()` invokes an Oak function value from C++ after a
chunk has run.

Lifetime rule: the `CompileOptions` object stores the closures behind every
binding, so it must outlive any chunk or VM compiled with it. Name strings
passed to `bind_*` are borrowed until `oak::compile` returns.

## Typed function binding

Pass any callable — lambda (captures allowed), function pointer, `noexcept`,
`mutable` — and the wrapper deduces arity, parameter types, and return type.
Oak call sites are then type-checked against the C++ signature:

```cpp
opts.bind_fn("add", [](int a, int b) { return a + b; })
    .bind_fn("positive", [](float value) { return value > 0.0f; })
    .bind_fn("consume", [](bool) {})
    .bind_fn("double_noexcept", &typed_double_noexcept);

oak::compile("let y = positive(add(10, 20));", opts); // ok
oak::compile("add(true, 20);", opts);                 // compile error
oak::compile("let b: bool = add(1, 2);", opts);       // compile error
```

Every binder also has a module-scoped overload with a leading module argument
(`bind_fn("mathx", "add", ...)`, `bind_type<T>("gfx", "Mesh")`,
`bind_enum("gfx", "Mode", ...)`). Module-scoped bindings pair with an Oak
declaration module and the module loader — see
[Module-scoped bindings](embedding-c.md#module-scoped-bindings) in the C
guide for the pattern the stdlib uses for `io.File`.

When you need dynamic arity or manual value handling, use the untyped form
with `oak::Context` and `oak::Args`:

```cpp
opts.bind_fn(
    "add", 2,
    [](oak::Context&, oak::Args a) -> oak::Value {
      return oak::Value::i32(a[0].as_i32() + a[1].as_i32());
    },
    OAK_BIND_SCALAR(OAK_TYPE_NUMBER));
```

`bind_fn_raw()` is the final escape hatch: it takes a plain
`oak_native_fn_t`-shaped callback, exactly as in the C API.

## Native types

`bind_type<T>()` returns a fluent `TypeBuilder<T>`:

```cpp
struct Vec2
{
  float x;
  float y;

  float sum() const { return x + y; }
  void scale(float factor) { x *= factor; y *= factor; }
  float dot(const Vec2& other) const { return x * other.x + y * other.y; }
};

auto vec2 = opts.bind_type<Vec2>("Vec2");
vec2.field("x", &Vec2::x)
    .field("y", &Vec2::y)
    .method("sum", &Vec2::sum)
    .method("scale", &Vec2::scale)
    .method("dot", &Vec2::dot)
    .static_method("twice", [](int value) { return value * 2; })
    .destructor(); // deletes the T* when the last reference drops
```

- `.field(&T::member)` generates getter and setter from a member pointer.
- `.method(&T::mem_fn)` binds a member function; other native types can
  appear in its signature (see `dot` taking `const Vec2&`).
- `.static_method(...)` is called as `Vec2.twice(...)` from Oak.
- `.destructor()` deletes the instance with `delete`; pass your own
  `oak_bind_destructor_t` to free differently, or skip it to keep lifetime on
  the host side.

Instances enter Oak through a factory function that wraps a C++ object with
`oak_native_record_new`:

```cpp
opts.bind_fn(
    "make_vec", 2,
    [raw = vec2.raw()](oak::Context& ctx, oak::Args a) -> oak::Value {
      auto* v = new Vec2{a[0].as_f32(), a[1].as_f32()};
      return oak::Value::from_raw(
          oak_native_record_new(ctx.allocator(), raw, v));
    },
    OAK_BIND_NATIVE(vec2.raw()));
```

Once a type is registered, typed lambdas can take it as `T*`, `T&`,
`const T*`, or `const T&` — the wrapper maps the parameter to the bound type
and the compiler rejects mismatched arguments:

```cpp
opts.bind_fn("shift", [](Vec2& v, float amount) { v.x += amount; })
    .bind_fn("vec_sum", [](const Vec2* v) { return v->x + v->y; });
```

```oak
let mut v = make_vec(3.0, 4.0);
v.scale(2.0);
print(v.dot(make_vec(1.0, 2.0)));
print(Vec2.twice(21));
```

## Enums

```cpp
opts.bind_enum("Color", {{"Red", 0}, {"Green", 1}, {"Blue", 2}});
```

Oak code uses `Color.Green` with enum-aware type checking.

## Values

`oak::Value` wraps the 8-byte `oak_value_t` with refcount-correct copy and
move semantics:

```cpp
oak::Value i = 42;                              // i32
oak::Value f = 2.5f;                            // f32
oak::Value b = true;                            // bool
oak::Value s = oak::Value::string(alloc, "hi"); // refcounted string
i.is_i32();
s.as_string();                                  // string_view-like access
```

Oak numbers are i32 or f32 at runtime; integer literals arrive i32-tagged, so
native code reading a "number" should coerce (`as_f32()` handles both).

## Parsing and AST inspection

The wrapper also exposes the front end, for tooling that wants syntax without
execution:

```cpp
auto parsed = oak::parse("let answer = 40 + 2;", alloc);
oak::AstNode root = parsed.root();     // kind(), child_count(), children()
oak::walk(root, [](oak::AstNode node) {
  if (node.is_terminal() && node.kind() == OAK_NODE_IDENT)
    std::printf("%s at %d:%d\n", node.text(), node.line(), node.column());
});
```

## Multi-file programs

`oak::ModuleRegistry` plus `oak::load_program()` compile an entry script and
its imports through the module loader, with native bindings visible to every
module:

```cpp
oak::ModuleRegistry registry(alloc);
auto loaded = oak::load_program("main.oak", opts, registry);
if (loaded.ok())
{
  oak::VM vm(alloc);
  vm.set_module_registry(registry);
  vm.run(loaded.entry()->chunk);
}
```

## The cycle invariant

The same rule as the C API: Oak has no runtime cycle collector, so native
code must never create a strong ownership loop between Oak values and native
instances. See [Embedding Oak: C API](embedding-c.md#the-cycle-invariant).
