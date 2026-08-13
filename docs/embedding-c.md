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
/* Written once: given to the compiler as param_types, and reused as the
 * callback's own guard. OAK_BIND_*_INIT are brace initializers, so the table
 * can have static storage; the OAK_BIND_* function forms cannot. */
static const oak_bind_type_ref_t add_params[] = {
  OAK_BIND_SCALAR_INIT(OAK_TYPE_NUMBER),
  OAK_BIND_SCALAR_INIT(OAK_TYPE_NUMBER),
};

static oak_fn_call_result_t add(oak_native_call_t* call,
                                const oak_value_t* args,
                                int argc,
                                oak_value_t* out)
{
  (void)call;
  if (!oak_native_args_match(args, argc, add_params, 2))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_I32(oak_as_i32(args[0]) + oak_as_i32(args[1]));
  return OAK_FN_CALL_OK;
}
```

Returning `OAK_FN_CALL_RUNTIME_ERROR` aborts the script with a runtime error.
`call` provides the VM, the allocator, and the `user_data` pointer from the
binding descriptor.

Register it as a global (or module-scoped) function:

```c
oak_bind_fn_global(&opts,
                   &(oak_bind_global_fn_t){
                       .name = "add",
                       .impl = add,
                       .arity = 2,
                       .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                       .param_types = add_params,
                       .param_count = 2,
                   });
```

The `module_name` field scopes the function into an importable module instead
of the global namespace — see
[Module-scoped bindings](#module-scoped-bindings) for the full pattern.
`param_types` is optional but recommended: with it, the compiler type-checks
call sites; the array is borrowed and must outlive `oak_compile_ex`.

Keeping the guard as well is worthwhile even when `param_types` is set: the
compiler checks Oak call sites, but a native function can also be reached from
C through `oak_vm_call()` with arbitrary values.

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

Ownership rules for getters: object values (strings, arrays, records) must be
returned as a fresh reference with the refcount already incremented; scalar
values (numbers, bools) need no refcounting.

Instances are created inside a native callback (typically a factory function
or static method) with `oak_vm_native_record_new()`:

```c
static oak_fn_call_result_t make_vec2(oak_native_call_t* call,
                                           const oak_value_t* args,
                                           int argc,
                                           oak_value_t* out)
{
  Vec2* v = malloc(sizeof(Vec2));
  v->x = oak_as_f32(args[0]);
  v->y = oak_as_f32(args[1]);
  /* user_data carries the type descriptor (set it on the binding) */
  *out = oak_vm_native_record_new(call->vm, call->user_data, v);
  return OAK_FN_CALL_OK;
}
```

The wrapper participates in normal reference counting: when the last
reference drops, the registered `destructor` runs on the instance pointer. If
no destructor is registered the instance is never freed — lifetime stays with
the embedder (useful for values that point into host-owned memory).

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
`oak_native_instance(args[0])` recovers the C pointer. Static methods
(`OAK_BIND_FN_STATIC_METHOD`) have no receiver and are called as
`TypeName.name(...)` from Oak.

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
