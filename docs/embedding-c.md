# Embedding Oak in C

Acorn is Oak's C library. You compile Oak source, run it on a VM, and
you can add your own functions, types, and enums. Those bindings go
through the same type checker as Oak code — a script that calls your
function with the wrong type will not compile.

Include the public headers from [`include/`](../include/) and link
against `acorn`.

The complete example that CI compiles and runs is
[`tests/public_api/oak_embed_smoke.c`](../tests/public_api/oak_embed_smoke.c).
It registers a function, a record type, an enum, and an attribute,
then tears everything down in the documented order. It builds against
`include/` alone, with no `-DOAK_*` flags, so it matches what an
embedder actually gets.

More usage lives in
[`tests/suites/bind_fn_test.c`](../tests/suites/bind_fn_test.c),
[`tests/suites/bind_type_test.c`](../tests/suites/bind_type_test.c),
and [`src/stdlib/`](../src/stdlib/) (the real `io.File` bindings).

`sqrt`, `pow`, and the string methods are **not** native bindings. The
compiler installs them itself. `oak_stdlib_register()` only registers
`io.File`. There is no `math` module to import, and no way to add to
or remove those built-ins.

## Compile and run

The pipeline is `source → lexer → parser → compiler → chunk → VM`.
`oak_program_t` runs all of it and owns the intermediate results, so
they are freed together and in the right order:

```c
#include "oak_allocator.h"
#include "oak_bind.h"
#include "oak_diagnostic.h"
#include "oak_program.h"
#include "oak_vm.h"

oak_allocator_t allocator;
oak_system_allocator_init(&allocator);

oak_compile_options_t opts;
oak_compile_options_init(&opts, &allocator);
/* register native bindings on `opts` here, before compiling */

oak_program_t prog;
if (oak_program_compile(&prog, source, &opts))
{
  oak_vm_t vm;
  oak_vm_init(&vm, &allocator);
  if (oak_vm_run(&vm, oak_program_chunk(&prog)) != OAK_VM_OK)
  {
    const oak_diagnostic_t* e = oak_vm_last_error(&vm);
    if (e)
      fprintf(stderr, "%d:%d: %s\n", e->line, e->column, e->message);
  }
  oak_vm_free(&vm);
}
else
{
  oak_diagnostics_print(oak_program_errors(&prog),
                        oak_program_error_count(&prog));
}

oak_program_free(&prog);
oak_compile_options_free(&opts);
```

Every stage uses `opts->allocator`, so there is one place to set it.
`oak_program_free` is safe on a failed or already-freed program.

Free the compile options last. Native values keep pointers into those
descriptors.

You can still drive the stages by hand — `oak_lexer_tokenize`,
`oak_parse`, and `oak_compile_ex` are public — if you need the token
stream or AST for tooling. If you do, free the parser result before
the lexer: the AST arena borrows the lexer's tokens.

### Errors

Compile-time and run-time errors are both `oak_diagnostic_t`
(`line`, `column`, `message`). `oak_diagnostics_print` writes an
array of them to stderr.

| Stage | Where to read them |
|---|---|
| lex / parse / compile via `oak_program_t` | `oak_program_errors()` / `oak_program_error_count()` |
| parsing directly | `oak_parser_errors()` / `oak_parser_error_count()` |
| compiling directly | `oak_compile_result_t::errors` / `::error_count` |
| module loading | `oak_module_loader_result_t::errors` / `::error_count` |
| run time | `oak_vm_last_error()` |

`oak_vm_last_error` returns the most recent runtime error, or `NULL`
if the last `oak_vm_run` / `oak_vm_call` succeeded. Runtime errors
are also written to stderr; this is how to get them as data.

### Allocators

To use your own allocator, pass any `malloc` / `realloc` / `free`
compatible functions:

```c
oak_allocator_t allocator_storage;
oak_allocator_init(&allocator_storage, my_malloc, my_realloc, my_free);
```

Allocate through `oak_alloc` / `oak_realloc` / `oak_free`. Each takes
an `oak_source_loc_t` saying where the memory was asked for, which is
what the tracking allocator reports on a leak:

```c
void* p = oak_alloc(&allocator_storage, 64, OAK_HERE);
oak_free(&allocator_storage, p, OAK_HERE);
```

`OAK_HERE` is the current C file and line. Pass it when *this* call
site is who asked for the memory. A helper that allocates for a
caller should take an `oak_source_loc_t` and forward it, so a leak
names the caller rather than the helper:

```c
static Node* node_new(oak_allocator_t* a, oak_source_loc_t at)
{
  return oak_alloc(a, sizeof(Node), at);   /* not OAK_HERE */
}
```

The location's `file` is borrowed, never copied. A `__FILE__` literal
is fine. Anything else must live at least as long as the allocation.

If you write an `oak_allocator_t` by hand instead of using
`oak_allocator_init`, implement the three callbacks. They receive
that location:

```c
static void* my_alloc(oak_allocator_t* self, usize size, oak_source_loc_t at);
static void* my_realloc(oak_allocator_t* self, void* ptr, usize new_size,
                        oak_source_loc_t at);
static void  my_free(oak_allocator_t* self, void* ptr, oak_source_loc_t at);
```

### Calling Oak from C

`oak_vm_call()` calls an Oak function value from C. The VM needs a
chunk attached first: either run one with `oak_vm_run()`, or attach
one without executing it with `oak_vm_prepare()`.

`oak_vm_t::user_data` is an embedder pointer so native callbacks can
recover their host object without process globals.

## Native functions

Every native callable — global function, instance method, static
method — has the same C signature:

```c
/* The compile-time signature. OAK_BIND_*_INIT are brace initializers,
 * so the table can have static storage. The OAK_BIND_* function forms
 * cannot. */
static const oak_bind_type_ref_t add_params[] = {
  OAK_BIND_SCALAR_INIT(OAK_TYPE_NUMBER),
  OAK_BIND_SCALAR_INIT(OAK_TYPE_NUMBER),
};

static oak_fn_call_result_t add(oak_native_call_t* call,
                                const oak_value_t* args,
                                const usize argc,
                                oak_value_t* out)
{
  int a;
  int b;
  if (!oak_arg_i32(call, args, argc, 0, &a) ||
      !oak_arg_i32(call, args, argc, 1, &b))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_I32(a + b);
  return OAK_FN_CALL_OK;
}
```

`call` gives you the VM, the allocator, the `user_data` pointer from
the binding, the binding's name, and — for a method — its receiver type.

### Reading arguments

`oak_arg_*` reads one argument and checks it:

| Accessor | Yields |
|---|---|
| `oak_arg_i32` / `oak_arg_f32` | exactly that numeric representation |
| `oak_arg_number` | either, widened to `float` |
| `oak_arg_bool` | `int`, 0 or 1 |
| `oak_arg_cstring` | `const char*`, borrowed for the call |
| `oak_arg_string` | `const oak_obj_string_t*`, so the length travels with it |
| `oak_arg_native` | the C instance behind a native record of a given type |
| `oak_arg_self` | `oak_arg_native` against `args[0]` and the receiver type |

Each returns non-zero on success. On a missing or wrongly-typed
argument it raises a runtime error naming the binding, the parameter
position, and the expected versus actual type, then returns 0.

Do not re-check `argc`. The VM already matched it against the
binding's declared arity, including on `oak_vm_call()` from C.

`oak_arg_native` and `oak_arg_self` are the only *checked* way to
unwrap a native record. `oak_native_instance()` only asserts that the
value is *some* native record, then reinterprets whatever C struct is
behind it. A receiver of the wrong type is silently misread. That
path is reachable from C through `oak_vm_call()`, where no
compile-time check applies.

### Reporting failure

`oak_native_error(call, fmt, ...)` raises a runtime error and returns
`OAK_FN_CALL_RUNTIME_ERROR`, so failing is one line:

```c
if (!fp)
  return oak_native_error(call, "cannot open '%s'", path);
```

The message is prefixed with the binding's name and reaches you
through `oak_vm_last_error()`. Returning `OAK_FN_CALL_RUNTIME_ERROR`
without calling it still aborts the script, but the message is only
`native function '<name>' failed`.

### Registering a function

```c
oak_bind_fn_global(&opts,
                   &(oak_bind_global_fn_t){
                       .module_name = OAK_NULL,
                       .name = "add",
                       .impl = add,
                       .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                       .param_types = add_params,
                       .param_count = OAK_COUNT_OF(add_params),
                       .user_data = OAK_NULL,
                   });
```

Set `.param_types` and `.param_count` as separate fields. When types
are declared they must agree; set `.param_count` without an array
only for a binding with no declared parameter types. The array is
borrowed and must outlive `oak_compile_ex`, so it cannot be a local.

Set `module_name` to put the function in an importable module instead
of the global namespace — see [Module-scoped bindings](#module-scoped-bindings).
`param_types` is optional but recommended: with it, the compiler
type-checks call sites.

Every `oak_bind_*` call returns 0 or -1. A rejection is also recorded
and reported by `oak_compile_ex` as a diagnostic, so a mis-registered
binding fails the compile and names itself, rather than silently
going missing.

### Registering several at once

`oak_bind_fields`, `oak_bind_fns`, `oak_bind_fns_global`, and
`oak_bind_enum_variants` take an array. Each returns 0 only if every
entry registered:

```c
static const oak_bind_enum_variant_t modes[] = {
  { "Read", 0 }, { "Write", 1 }, { "Append", 2 },
};
oak_bind_enum_variants(mode, modes, (int)OAK_COUNT_OF(modes));
```

A table that references a descriptor returned by `oak_bind_type()`
cannot be `static`, since that pointer is only known at run time —
make it a local array. See
[`src/stdlib/oak_stdlib_file.c`](../src/stdlib/oak_stdlib_file.c)
for both shapes.

### Type references

`oak_bind_type_ref_t` describes parameter, return, and field types:

| Macro | Oak type |
|---|---|
| `OAK_BIND_SCALAR(OAK_TYPE_NUMBER)` | `number` (likewise `OAK_TYPE_STRING`, `OAK_TYPE_BOOL`, ...) |
| `OAK_BIND_ARRAY(OAK_TYPE_NUMBER)` | `number[]` |
| `OAK_BIND_MAP(OAK_TYPE_STRING, OAK_TYPE_NUMBER)` | `[string:number]` |
| `OAK_BIND_NATIVE(desc)` | a native type by its descriptor |
| `OAK_BIND_NATIVE_ARRAY(desc)` | array of a native type |
| `OAK_BIND_NATIVE_MAP(kdesc, vdesc)` | map with native key and/or value types |

`OAK_BIND_SCALAR_INIT`, `OAK_BIND_ARRAY_INIT`, and `OAK_BIND_MAP_INIT`
are brace initializers for the same three builtin forms. Use them for
tables with static storage — the macros above expand to a function
call, which C does not accept as a static initializer.

`OAK_BIND_ENUM(desc)` and `OAK_BIND_ENUM_ARRAY(desc)` name a native
enum registered with `oak_bind_enum()`. Prefer them over
`OAK_TYPE_NUMBER` for an enum-typed parameter: the compiler then
accepts `f(EnumName.Variant)` and rejects a bare integer. At run time
an enum value *is* its integer, so check it with `oak_arg_i32`.

`OAK_BIND_WEAK(ref)` wraps any of the forms above to make the
reference non-owning. Oak has no cycle collector, so a native field
that points back at something that can reach it must be declared
weak, exactly as it would be in Oak source.

## Native record types

`oak_bind_type()` registers a heap-allocated, reference-counted
record type. Fields are exposed through getter/setter callbacks over
the raw C instance:

```c
typedef struct Vec2
{
  float x;
  float y;
} Vec2;

static oak_value_t vec2_get_x(oak_value_t self, void* user_data)
{
  (void)user_data;
  Vec2* v = oak_native_instance(self);
  return OAK_VALUE_F32(v->x);
}

static void vec2_set_x(oak_value_t self,
                       oak_value_t value,
                       void* user_data)
{
  (void)user_data;
  Vec2* v = oak_native_instance(self);
  v->x = oak_as_f32(value);
}

static void vec2_free(void* instance)
{
  /* free whatever the instance owns; instance itself is the pointer
   * you passed to oak_vm_native_record_new */
  free(instance);
}

oak_bind_type_t* vec2 =
    oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "Vec2");
vec2->destructor = vec2_free;

/* Native records declare their Oak interfaces explicitly, and the Oak
   declaration of Vec2 must carry the same clause:

       record Vec2 implements IVector { x : number; y : number; }

   A disagreement is rejected, the same as a disagreement about the
   field list. If the program never declares IVector, the claim does
   nothing. */
oak_bind_type_implements(vec2, "IVector");

oak_bind_field(vec2,
               &(oak_bind_field_t){
                   .name = "x",
                   .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                   .getter = vec2_get_x,
                   .setter = vec2_set_x, /* NULL = read-only */
               });
```

Ownership at the field boundary runs in opposite directions:

- A **getter returns an owned reference**. Object values (strings,
  arrays, records) must already have their refcount incremented.
  Scalars need none.
- A **setter receives a borrowed value**. The VM releases it as soon
  as the setter returns, so a setter that stores an object value must
  `oak_value_incref` it first.

Create instances inside a native callback — typically a static method
acting as a constructor. `oak_native_self_new()` wraps the instance
as the receiver type, so the binding does not have to carry its own
descriptor:

```c
static oak_fn_call_result_t make_vec2(oak_native_call_t* call,
                                      const oak_value_t* args,
                                      const usize argc,
                                      oak_value_t* out)
{
  float x;
  float y;
  if (!oak_arg_number(call, args, argc, 0, &x) ||
      !oak_arg_number(call, args, argc, 1, &y))
    return OAK_FN_CALL_RUNTIME_ERROR;

  Vec2* v = oak_alloc(call->allocator, sizeof(Vec2), OAK_HERE);
  if (!v)
    return oak_native_error(call, "out of memory");
  v->x = x;
  v->y = y;
  *out = oak_native_self_new(call, v);
  return OAK_FN_CALL_OK;
}
```

A *global* function has no receiver, so it still needs the descriptor
through `user_data`:
`oak_vm_native_record_new(call->vm, call->user_data, v)`.

When the last reference drops, the registered `destructor` runs. It
receives the instance pointer and the type's `user_data` — teardown
has no VM or call in scope, so that is where an allocator has to
come from. If no destructor is registered the instance is never
freed, which is useful for values that point into host-owned memory.

Returning a value the callback did not just allocate needs care:
`*out` is *moved* to the VM, which then releases every argument.
Assigning an object argument straight through — `*out = args[0]` —
is a use-after-free. Incref it first, or return a fresh object.

## Methods

`oak_bind_fn()` attaches instance or static methods to a native type:

```c
oak_bind_fn(&opts,
            &(oak_bind_fn_t){
                .kind = OAK_BIND_FN_INSTANCE_METHOD,
                .receiver_type = vec2,
                .name = "length",
                .impl = vec2_length,
                .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                .param_count = 0, /* excludes the implicit self */
            });
```

For instance methods, `param_count` is only the user-visible
parameters. At run time the callback receives `self` as `args[0]`
(so `argc == param_count + 1`), and `oak_arg_self()` recovers the C
pointer after checking the type. Static methods
(`OAK_BIND_FN_STATIC_METHOD`) have no receiver and are called as
`TypeName.name(...)` from Oak.

Because `self` occupies `args[0]`, an explicit parameter *i* is at
index *i + 1*:

```c
static oak_fn_call_result_t vec2_scale(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       const usize argc,
                                       oak_value_t* out)
{
  Vec2* v;
  float factor;
  if (!oak_arg_self(call, args, argc, (void**)&v) ||
      !oak_arg_number(call, args, argc, 1, &factor))
    return OAK_FN_CALL_RUNTIME_ERROR;
  ...
}
```

## Inline value types

`OAK_BIND_TYPE_VALUE` registers a type whose instances live directly
inside the 8-byte `oak_value_t` — no heap wrapper, no refcount, no
destructor. The payload must fit in 61 bits (pointers do; arbitrary
u64 ids may not). Value types expose data through methods only; they
cannot declare fields.

Wrap and unwrap the opaque payload with `oak_native_value_new()` /
`oak_native_value()`. See
[`tests/suites/bind_type_test.c`](../tests/suites/bind_type_test.c).

## Enums

```c
oak_bind_enum_t* color = oak_bind_enum(&opts, "Color");
oak_bind_enum_variant(color, "Red", 0);
oak_bind_enum_variant(color, "Green", 1);
oak_bind_enum_variant(color, "Blue", 2);
```

Oak code then uses `Color.Green` with the same type checking as a
declared `enum`.

## Module-scoped bindings

By default bindings land in the global namespace. To expose a native
API as an importable module — the way the stdlib provides `io.File`
— two pieces work together.

**1. An Oak declaration module.** A `.oak` file that declares the
module's types, enums, and function signatures without bodies
([`stdlib/io.oak`](../stdlib/io.oak)):

```oak
export enum FileMode { Read, Write, Append }

export record File {
  export fn static open(path : string, mode : FileMode) -> File;
  export fn read() -> string;
  export fn mut write(value : string);
}
```

A native record's fields are bound from C, so its body usually
declares only methods. Each one must agree with its binding on
kind as well as name and arity: `fn static` has to match
`OAK_BIND_FN_STATIC_METHOD`, and a bare `fn` has to match
`OAK_BIND_FN_INSTANCE_METHOD`, or the module fails to load.

Bodyless signatures compile only when `allow_bodyless_fns` is set
on the compile options for that module.

**2. Native bindings** registered under the matching module name
with `oak_bind_type_in_module()`, `oak_bind_enum_in_module()`, and
the `module_name` field on function descriptors
([`src/stdlib/oak_stdlib_file.c`](../src/stdlib/oak_stdlib_file.c)):

```c
oak_bind_type_t* file =
    oak_bind_type_in_module(&opts, "io", OAK_BIND_TYPE_RECORD, "File");
```

Programs compiled through the module loader
(`oak_module_loader_load_program()`, declared in
[`include/oak_module_loader.h`](../include/oak_module_loader.h))
then import the module like any other:

```oak
import { File, FileMode } from io;
```

The loader resolves the declaration module from the stdlib search
path. A missing file is an error. A host with no filesystem (the
WebAssembly playground, an embedder shipping only bindings) can set
`allow_synthetic_native_modules` on the compile options — or pass
`--allow-synthetic-modules` to the CLI — to build the module from
the registered bindings alone.

The synthesized module carries only what the bindings describe, so
anything the stub adds on top — parameter types and mutability,
`fn mut` receivers, declarations with no matching binding — is
absent, and calls into it are checked more loosely.

## Attributes

`oak_bind_attr()` registers a named attribute with two optional hooks:

- `on_decl` fires at compile time for each declaration that bears the
  attribute. It gets parameter/field metadata and a live
  `oak_compile_options_t*`, so it can register more bindings.
- `on_call` fires before every call to a function that bears it, and
  can abort the call by returning `OAK_FN_CALL_RUNTIME_ERROR`.

## Values, VMs, and threads

Every Oak value is one 64-bit word. Heap objects belong either to a
specific VM or to the process-shared table.

Use the `oak_vm_*_new()` family when a heap value should belong to a
particular VM:

```c
oak_obj_string_t* message = oak_vm_string_new(&vm, "hello");
oak_obj_array_t* items = oak_vm_array_new(&vm);
```

The plain `oak_string_new()`, `oak_array_new()`, and related
constructors make process-shared values instead (compiled constants
and native definitions live here).

Rules that follow from that:

- One thread may operate on different VMs, and a VM may move between
  threads *between* calls.
- Neither a VM nor its values may be accessed concurrently.
- At most 63 VM tables can be live alongside the shared table.
- Heap values created by a VM stay in that VM. The runtime rejects
  putting a strong or weak reference from one VM into another VM's
  stack, array, map, record, native field, or call arguments —
  including in release builds.
- Scalar values and process-owned (table-0) objects may be used by
  every VM.
- To talk between workers, exchange host data or scalar Oak values
  and recreate arrays, maps, records, and strings in the destination
  VM. Do not pass an object `oak_value_t` across VMs.

When an object dies, its slot's generation number (nonce) is
incremented before the slot can be reused. A weak reference therefore
becomes `none` instead of silently pointing at whatever was allocated
next in that slot.

## The cycle invariant

Oak has no runtime cycle collector. The compiler rejects programs
that could form strong reference cycles, so reference counting alone
reclaims every object.

Native code must uphold the same rule: never create a strong
ownership loop from C (an Oak value stored in a native instance that
ultimately owns that instance). Hold weak values, or restructure
ownership so the graph stays acyclic.

A *declared* native field takes part in that analysis, so wrap its
type in `OAK_BIND_WEAK` when it points back at something that can
reach the owner:

```c
oak_bind_field(node,
               &(oak_bind_field_t){
                   .name = "parent",
                   .type = OAK_BIND_WEAK(OAK_BIND_NATIVE(node)),
                   .getter = node_get_parent,
               });
```

The compiler cannot see inside a native instance. Oak values that
the C struct holds without declaring them as fields — and anything a
native method stores there — are outside the analysis, and are yours
to keep acyclic.
