# Embedding Oak: C API

Link against `acorn` and include the public headers from
[`include/`](../include/). The binding API is descriptor-based: you describe
native types, functions, enums, and attributes on `oak_compile_options_t`,
then compile with `oak_compile_ex()`. Bound names participate in Oak's
compile-time type checks exactly like Oak-declared ones — a script that calls
a native function with the wrong argument type fails to compile.

The examples below are condensed. For a complete program that is compiled and
run by CI, see
[`tests/public_api/oak_embed_smoke.c`](../tests/public_api/oak_embed_smoke.c):
it registers a native function, a record type with a field and a method, an
enum and an attribute, compiles and runs a script, and tears everything down in
the documented order. It builds against `include/` alone with no `-DOAK_*`
flags, so it cannot drift from what an embedder actually gets.

Further usage lives in
[`tests/suites/bind_fn_test.c`](../tests/suites/bind_fn_test.c),
[`tests/suites/bind_type_test.c`](../tests/suites/bind_type_test.c), and
[`src/stdlib/`](../src/stdlib/) (the real registrations behind `io.File`).

Note that `sqrt`, `pow` and the string methods are *not* native bindings: the
compiler installs them directly as globals and methods, so there is no `math`
module to import and no way for an embedder to add to or remove from those
tables. `oak_stdlib_register()` registers `io.File` only.

## Compile and run

The pipeline is `source → lexer → parser → compiler → chunk → VM`.
`oak_program_t` drives all of it and owns the three intermediate results, so
they are released together and in the right order:

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

The allocator for every stage comes from `opts->allocator`, so there is one
place to set it. `oak_program_free` is safe to call on a failed or
already-freed program, so the teardown does not need to branch.

The stages can still be driven by hand — `oak_lexer_tokenize`, `oak_parse` and
`oak_compile_ex` are all public — if you need the token stream or AST for
tooling. Doing so means holding three results alive and freeing the parser
result before the lexer, since the AST arena borrows the lexer's tokens.

### Diagnostics

Compile-time and run-time errors are both reported as `oak_diagnostic_t`
(`line`, `column`, `message`), and `oak_diagnostics_print` writes an array of
them to stderr. Where to find them:

| Stage | Accessor |
|---|---|
| lex / parse / compile via `oak_program_t` | `oak_program_errors()` / `oak_program_error_count()` |
| parsing directly | `oak_parser_errors()` / `oak_parser_error_count()` |
| compiling directly | `oak_compile_result_t::errors` / `::error_count` |
| module loading | `oak_module_loader_result_t::errors` / `::error_count` |
| run time | `oak_vm_last_error()` |

`oak_vm_last_error` returns the most recent runtime error, or `NULL` if the
last `oak_vm_run` / `oak_vm_call` did not fail. Runtime errors are written to
stderr as well; this is how to get them as data instead.

To route Oak through another allocator, initialize it with any
`malloc`/`realloc`/`free`-compatible functions instead:

```c
oak_allocator_t allocator_storage;
oak_allocator_init(&allocator_storage, my_malloc, my_realloc, my_free);
```

`oak_vm_call()` calls an Oak function value from C. It needs a chunk attached
to the VM: either run one first with `oak_vm_run()`, or attach one without
executing it using `oak_vm_prepare()`. `oak_vm_t::user_data` carries an
embedder pointer so native callbacks can recover their host object without
process globals.

VM ownership is explicit rather than thread-local. Use the `oak_vm_*_new()`
family when a heap value should belong to a particular VM; for example:

```c
oak_obj_string_t* message = oak_vm_string_new(&vm, "hello");
oak_obj_array_t* items = oak_vm_array_new(&vm);
```

The plain `oak_string_new()`, `oak_array_new()`, and related constructors make
process-shared values instead. A thread may operate on different VMs, and a VM
may move between threads between calls, but a VM and its values must never be
used concurrently. The registry supports 63 live VM tables plus shared table
0.

## Native functions

Every native callable — global function, instance method, static method —
has the same C signature:

```c
/* The compile-time signature. OAK_BIND_*_INIT are brace initializers, so the
 * table can have static storage; the OAK_BIND_* function forms cannot. */
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

`call` provides the VM, the allocator, the `user_data` pointer from the binding
descriptor, the binding's name, and — for a method — its receiver type.

### Reading arguments

The `oak_arg_*` family reads one argument, checking it on the way:

| Accessor | Yields |
|---|---|
| `oak_arg_i32` / `oak_arg_f32` | exactly that numeric representation |
| `oak_arg_number` | either, widened to `float` |
| `oak_arg_bool` | `int`, 0 or 1 |
| `oak_arg_cstring` | `const char*`, borrowed for the call |
| `oak_arg_string` | `const oak_obj_string_t*`, so the length travels with it |
| `oak_arg_native` | the C instance behind a native record of a given type |
| `oak_arg_self` | `oak_arg_native` against `args[0]` and the receiver type |

Each returns non-zero on success. On a missing or wrongly-typed argument it
raises a runtime error naming the binding, the parameter position, and the
expected versus actual type, then returns 0 — so the guard is one line and the
diagnostic is written centrally rather than per callback.

Do not re-check `argc`: the VM matches it against the binding's declared arity
before dispatching, on every path including `oak_vm_call()` from C.

`oak_arg_native` and `oak_arg_self` are the only *checked* way to unwrap a
native record. `oak_native_instance()` asserts merely that the value is some
native record and then reinterprets whatever C struct is behind it, so a
receiver of the wrong bound type is silently misread — reachable from C through
`oak_vm_call()`, where no compile-time check applies.

### Reporting failure

`oak_native_error(call, fmt, ...)` raises a runtime error and returns
`OAK_FN_CALL_RUNTIME_ERROR`, so failing is one line:

```c
if (!fp)
  return oak_native_error(call, "cannot open '%s'", path);
```

The message is prefixed with the binding's name and reaches the embedder
through `oak_vm_last_error()`. Returning `OAK_FN_CALL_RUNTIME_ERROR` without
calling it still aborts the script, but reports only
`native function '<name>' failed`, which cannot distinguish a missing file from
a bad argument.

### Registering it

```c
oak_bind_fn_global(&opts,
                   &(oak_bind_global_fn_t){
                       .name = "add",
                       .impl = add,
                       OAK_BIND_PARAMS(add_params),
                       .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                   });
```

`OAK_BIND_PARAMS` fills `arity`, `param_types` and `param_count` from the one
array so they cannot disagree; set them individually only for a binding with no
declared parameter types. The array is borrowed and must outlive
`oak_compile_ex`, so it cannot be a local.

The `module_name` field scopes the function into an importable module instead
of the global namespace — see
[Module-scoped bindings](#module-scoped-bindings) for the full pattern.
`param_types` is optional but recommended: with it, the compiler type-checks
call sites.

Every `oak_bind_*` call returns 0 or -1, and a rejection is *also* recorded and
reported by `oak_compile_ex` as a diagnostic — a mis-registered binding fails
the compile and names itself, rather than silently going missing and surfacing
later as an "unknown name" error at the call site.

### Registering several at once

`oak_bind_fields`, `oak_bind_fns`, `oak_bind_fns_global` and
`oak_bind_enum_variants` take an array, so a binding can be a table rather than
a run of near-identical calls. Each returns 0 only if every entry registered:

```c
static const oak_bind_enum_variant_t modes[] = {
  { "Read", 0 }, { "Write", 1 }, { "Append", 2 },
};
oak_bind_enum_variants(mode, modes, (int)oak_count_of(modes));
```

A table that references a descriptor returned by `oak_bind_type()` cannot be
`static`, since that pointer is only known at run time — make it a local array.
See [`src/stdlib/oak_stdlib_file.c`](../src/stdlib/oak_stdlib_file.c) for both
shapes.

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

`OAK_BIND_SCALAR_INIT`, `OAK_BIND_ARRAY_INIT` and `OAK_BIND_MAP_INIT` are brace
initializers for the same three builtin forms, for tables with static storage
duration — the macros above expand to a function call, which C does not accept
as a static initializer.

`OAK_BIND_ENUM(desc)` and `OAK_BIND_ENUM_ARRAY(desc)` name a native enum
registered with `oak_bind_enum()`. Prefer them over `OAK_TYPE_NUMBER` for an
enum-typed parameter: the compiler then accepts `f(EnumName.Variant)` and
rejects a bare integer. At run time an enum value *is* its integer, so a
number check inside the callback is the right one — `oak_arg_i32` rather than
anything enum-aware.

`OAK_BIND_WEAK(ref)` wraps any of the forms above to make the reference
non-owning. Oak has no cycle collector, so a native field that points back at
something that can reach it must be declared weak, exactly as it would be in
Oak source; a weak field takes part in the same compile-time acyclicity
analysis as a declared one.

## Native record types

`oak_bind_type()` registers a heap-allocated, reference-counted record type;
fields are exposed through getter/setter callbacks over the raw C instance:

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
  /* free whatever the instance owns; instance itself is the pointer you
   * passed to oak_vm_native_record_new */
  free(instance);
}

oak_bind_type_t* vec2 =
    oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "Vec2");
vec2->destructor = vec2_free;

oak_bind_field(vec2,
               &(oak_bind_field_t){
                   .name = "x",
                   .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                   .getter = vec2_get_x,
                   .setter = vec2_set_x, /* NULL = read-only */
               });
```

Ownership at the field boundary runs in opposite directions, so both are worth
stating: a **getter returns an owned reference** — object values (strings,
arrays, records) must carry a refcount already incremented; scalars need none.
A **setter receives a borrowed value**, which the VM releases as soon as the
setter returns, so a setter that stores an object value must
`oak_value_incref` it first.

Instances are created inside a native callback — typically a static method
acting as a constructor, where `oak_native_self_new()` wraps the instance as
the receiver type without the binding having to carry its own descriptor:

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

  Vec2* v = OAK_ALLOC(call->allocator, sizeof(Vec2));
  if (!v)
    return oak_native_error(call, "out of memory");
  v->x = x;
  v->y = y;
  *out = oak_native_self_new(call, v);
  return OAK_FN_CALL_OK;
}
```

A *global* function has no receiver, so it still needs the descriptor through
`user_data`: `oak_vm_native_record_new(call->vm, call->user_data, v)`.

The wrapper participates in normal reference counting: when the last reference
drops, the registered `destructor` runs, receiving the instance pointer and the
type's `user_data` (teardown has no VM or call in scope, so that is where an
allocator has to come from). If no destructor is registered the instance is
never freed — lifetime stays with the embedder, which is useful for values that
point into host-owned memory.

Returning a value the callback did not just allocate needs care: `*out` is
*moved* to the VM, which then releases every argument. Assigning an object
argument straight through — `*out = args[0]` — is therefore a use-after-free.
Incref it first, or return a fresh object.

## Methods

`oak_bind_fn()` attaches instance or static methods to a native type:

```c
oak_bind_fn(&opts,
            &(oak_bind_fn_t){
                .kind = OAK_BIND_FN_INSTANCE_METHOD,
                .receiver_type = vec2,
                .name = "length",
                .impl = vec2_length,
                .arity = 0, /* excludes the implicit self */
                .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
            });
```

For instance methods, `arity` counts only the user-visible parameters; at run
time the callback receives `self` as `args[0]` (so `argc == arity + 1`), and
`oak_arg_self()` recovers the C pointer, having first checked that the receiver
really is this type. Static methods (`OAK_BIND_FN_STATIC_METHOD`) have no
receiver and are called as `TypeName.name(...)` from Oak.

Because `self` occupies `args[0]`, an explicit parameter *i* is at index
*i + 1*:

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

`OAK_BIND_TYPE_VALUE` registers a type whose instances live directly inside
the 8-byte `oak_value_t` — no heap wrapper, no refcount, no destructor.
The payload must fit in 61 bits (pointers do; arbitrary u64 ids may not).
Value types expose data through methods only (they cannot declare fields).
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

Oak code then uses `Color.Green` with the same enum-aware type checking as a
declared `enum`.

## Module-scoped bindings

By default bindings land in the global namespace. To expose a native API as
an importable module — the way the stdlib provides `io.File` — two pieces
work together:

1. An Oak **declaration module**: a `.oak` file declaring the module's types,
   enums, and function signatures without bodies
   ([`stdlib/io.oak`](../stdlib/io.oak)):

   ```oak
   export enum FileMode { Read, Write, Append }

   export record File;

   export fn File.open(path : string, mode : FileMode) -> File;
   export fn File.read(self) -> string;
   ```

   Bodyless signatures compile only when `allow_bodyless_fns` is set on the
   compile options for that module.

2. Native bindings registered under the matching module name with
   `oak_bind_type_in_module()`, `oak_bind_enum_in_module()`, and the
   `module_name` field on function descriptors
   ([`src/stdlib/oak_stdlib_file.c`](../src/stdlib/oak_stdlib_file.c)):

   ```c
   oak_bind_type_t* file =
       oak_bind_type_in_module(&opts, "io", OAK_BIND_TYPE_RECORD, "File");
   ```

Programs compiled through the module loader
(`oak_module_loader_load_program()`, declared in
[`include/oak_module_loader.h`](../include/oak_module_loader.h)) then import
the module like any other:

```oak
import { File, FileMode } from io;
```

The loader resolves the declaration module from the stdlib search path, and a
missing file is an error. A host with no filesystem to load it from (the
WebAssembly playground, an embedder shipping only bindings) can set
`allow_synthetic_native_modules` on the compile options — or pass
`--allow-synthetic-modules` to the CLI — to build the module from the
registered bindings alone. The synthesized module carries only what the
bindings describe, so anything the stub adds on top — parameter types and
mutability, `mut self` receivers, declarations with no matching binding — is
absent, and calls into it are checked more loosely.

## Attributes

`oak_bind_attr()` registers a named attribute with two optional hooks:
`on_decl` fires at compile time for each declaration bearing the attribute
(with parameter/field metadata, and a live `oak_compile_options_t*` so it can
register additional bindings); `on_call` fires before every call to a
function bearing it and can abort the call by returning
`OAK_FN_CALL_RUNTIME_ERROR`.

## The cycle invariant

Oak has no runtime cycle collector: the compiler rejects programs that could
form strong reference cycles, so reference counting alone reclaims every
object. Native code must uphold the same invariant — never create a strong
ownership loop from C (an Oak value stored in a native instance that
ultimately owns that instance). Hold weak values or restructure ownership so
the graph stays acyclic.

A *declared* native field takes part in that analysis, so wrap its type in
`OAK_BIND_WEAK` when it points back at something that can reach the owner:

```c
oak_bind_field(node,
               &(oak_bind_field_t){
                   .name = "parent",
                   .type = OAK_BIND_WEAK(OAK_BIND_NATIVE(node)),
                   .getter = node_get_parent,
               });
```

The compiler cannot see inside a native instance, though. Oak values that the
C struct holds without declaring them as fields — and anything a native method
stores there — are outside the analysis entirely, and are the embedder's to
keep acyclic.
