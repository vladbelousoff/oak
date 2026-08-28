# Oak

Oak is a small, statically typed scripting language. You write `.oak` files
and run them with the `oak` command — or from C, using the `acorn` library.

There is no garbage collector. Values are reference-counted, and the
compiler refuses programs that could form a cycle. If a program compiles,
memory is reclaimed when the last reference goes away.

```oak
fn sum(values : number[]) -> number {
  let total = 0;
  for value in values {
    total += value;
  }
  return total;
}

print(sum([3, 5, 8]));
```

This repo is the language, the CLI, the C library, examples, tests, a
VS Code extension, and a WebAssembly playground.

## Quick start

```sh
meson setup build
meson compile -C build
./build/oak examples/01_values/01_values.oak
```

On Windows, run `.\build\oak.exe` instead of `./build/oak`.

See [Building](docs/building.md) for requirements, tests, install, and the
playground, and [the CLI](docs/cli.md) for command-line options.

## The language

The numbered examples in [`examples/`](examples/README.md) are the full
tour. This is a short look at how Oak feels.

### Bindings and values

`let` declares a binding and takes no qualifier. Whether you can write
*through* it is inherited from the initializer: a fresh value is yours to
change, and a reference reached from something read-only stays read-only. Only
functions say `mut` — see [Functions](#functions).
Strings are **single-quoted** — double quotes are not allowed.
`/` is always float division; `//` is integer division.
Comments are `/* ... */` only. There is no `//` comment: `//` is the
division operator.

```oak
let answer = 40 + 2;
let stock = 10;
stock += 5;

let ratio = 9 / 2;   /* 4.5 */
let half = 9 // 2;   /* 4 */

print('{} {}'.format(['Oak', 'scripts']));
```

### Control flow

`if` / `else`, `while`, counted `for i from 1 to 10` (the upper bound is
exclusive), and `for item in collection`. `break` and `continue` work as
you'd expect.

### Collections

```oak
let scores = new number[];
scores.push(10);
scores[0] = 12;

let inventory = new [string:number];
inventory['apples'] = 4;
print(inventory.has('apples'));
```

### Functions

Named functions use `fn`. Anonymous functions and function types share
the `(...) -> T` shape:

```oak
fn fib(n : number) -> number {
  if n < 2 {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}

let double = (x : number) -> number { return x * 2; };
print(double(fib(10)));
```

A parameter is read-only unless it says `mut`. This is the one place the
language asks you to declare write access, so a signature states in full what
a function may change:

```oak
fn insertion_sort(mut values : number[]) { ... }

let mixed = [9, 1, 5];
insertion_sort(mixed);   /* ok: `mixed` is a fresh array */

fn report(values : number[]) {
  insertion_sort(values);   /* error: `values` is read-only here */
}
```

### Records, enums, and interfaces

Create a record with `new Type { ... }`. Methods live in the record body:

- `fn name(...)` can read `self`
- `fn mut name(...)` can also write `self`
- `fn static name(...)` has no receiver and is called on the type

Interface names start with `I`. A record has to say `implements IFoo` —
having the methods is not enough on its own. If a method is missing or
mismatched, the error is on the record, not later at the call site.

```oak
enum Status { Planned, Active, Done }

interface IShape {
  fn area() -> number;
}

record Circle implements IShape {
  radius : number;

  fn area() -> number {
    return 3.14159 * self.radius * self.radius;
  }
}

let c = new Circle { radius : 5 };
print(c.area());
```

### Modules

A module name resolves relative to the file that imports it. Only names
marked `export` are visible. Import selected names, everything, or a namespace:

```oak
import { sum, average } from analytics.stats;
import * from domain.project;
import domain.project as project;
```

See [`examples/06_modules/`](examples/06_modules/) for a complete
multi-file program.

### Packages

A package is a directory with an `oak.json` in it. Declare a dependency
and its modules become importable by name from anywhere in your program,
with no files copied into your tree:

```json
{
  "name": "me/demo",
  "version": "1.0.0",
  "module": "app",
  "src": "src",
  "deps": {
    "greet": { "path": "vendor/greet" },
    "json":  "github:acme/oak-json@1.2.0"
  }
}
```

```oak
import { hello } from greet;
import { parse } from json.lexer;
```

The alias on the left is the first segment of the import, so renaming a
dependency is a manifest edit rather than a change to every import. The
rest of the dotted name resolves inside that package exactly the way the
stdlib already resolves `io.path` — `json.lexer` is `<package>/src/json/lexer.oak`.

`oak-pkg` resolves and fetches; `oak` reads the lockfile and runs.
Running a program never touches the network and never decides a version:

```sh
oak-pkg init --name me/demo
oak-pkg add github:acme/oak-json@1.2.0
oak src/app.oak
```

A dependency's own dependencies are not visible to you, so two packages
can depend on different things under the same name without colliding. A
script with no manifest above it is unaffected — imports resolve exactly
as they did before packages existed.

Packages ship source, or a prebuilt shared library per platform plus the
same bodyless `.oak` declarations the standard library uses.

[Packages](docs/packages.md) covers manifests, the lockfile, the shared
cache, and version selection; [Publishing](docs/publishing.md) covers
shipping one. [`examples/14_packages/`](examples/14_packages/) is a
complete working project.

### Memory

Oak has no garbage collector and no runtime cycle detector. The compiler
rejects programs that *could* form a strong reference cycle, so
reference counting is enough.

If two records can point at each other, mark one of those fields
`weak`. A weak reference does not keep its target alive and can be
compared against `none`:

```oak
record Node {
  name : string;
  links : Edge[];
}

record Edge {
  label : string;
  target : Node weak;
}
```

The same rule applies to C bindings: do not create a strong ownership
loop from native code.

### Text

Source files and strings are UTF-8. Strings are single-quoted, and
identifiers may be non-ASCII. `\u{...}` writes a codepoint by number:

```oak
let greeting = 'Привет, мир';
print(greeting.size());        /* 11 — characters, not bytes */
print(greeting.substring(0, 6));
print(chr(128512));             /* \u{1F600} */
```

`size`, `index_of`, and `substring` count characters, so a slice never
cuts one in half. `upper` and `lower` map ASCII only and leave everything
else untouched. Source that is not valid UTF-8 is a compile error.

`to_string` turns any value into text. `+` never coerces, so this is the
way to build a string out of something that is not one:

```oak
print('score: ' + to_string(42));   /* score: 42 */
print(to_string([1, 2]));           /* the array, one element per line */
```

A record prints its fields, and a field that holds another record prints
that record's fields too — except a `weak` field, which prints the
identity of its target rather than its contents. A weak reference is the
edge that points back into the graph already being printed, so following
it would repeat the same records over and over.

On a record it is a method rather than a global, and a record may define
its own — the built-in one is only the default:

```oak
record Point {
  x : number;
  y : number;
  export fn to_string() -> string { return '(' + to_string(self.x) + ')'; }
}
```

`format` fills placeholders in a template from an array:

```oak
print('{} squared is {}'.format([4, 16]));   /* 4 squared is 16 */
print('{1} then {0}'.format(['b', 'a']));    /* a then b */
print('{0} twice: {0}'.format(['x']));       /* x twice: x */
print('{{literal}}'.format(['x']));          /* {literal} */
```

`{}` takes the next argument in order, `{0}` names one by index (and may
repeat it), and `{{` and `}}` write a literal brace. A placeholder with no
argument to fill it is a runtime error; spare arguments are ignored.

Arrays are homogeneous, so a template mixing kinds takes an array of
strings — `to_string` is what gets them there:

```oak
print('{} scored {}'.format(['ann', to_string(5)]));
```

### Stdlib

Printing, numbers, strings, and collections need no import:

```oak
print('Hello, Oak'.upper());
print(to_int(pow(2.0, 10.0)));
print('{} squared is {}'.format([4, 16]));
```

File I/O lives in `io`:

```oak
import * from io;
let file = File.open('message.txt', FileMode.Read);
print(file.read_all());
```

The numbered examples cover the rest.

## Embedding

The C library is called **acorn**. You describe native functions and
types on `oak_compile_options_t`, then compile and run. Bindings go
through the same type checker as Oak code.

```c
oak_bind_fn_global(&opts,
                   &(oak_bind_global_fn_t){
                       .module_name = OAK_NULL,
                       .name = "add",
                       .impl = native_add,
                       .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                       .param_types = add_params,
                       .param_count = OAK_COUNT_OF(add_params),
                       .user_data = OAK_NULL,
                   });

oak_program_t prog;
if (oak_program_compile(&prog, "print(add(20, 22));\n", &opts))
{
  oak_vm_t vm;
  oak_vm_init(&vm, &allocator);
  oak_vm_run(&vm, oak_program_chunk(&prog));
  oak_vm_free(&vm);
}
oak_program_free(&prog);
```

The [C embedding guide](docs/embedding-c.md) has the full API.
[`tests/public_api/oak_embed_smoke.c`](tests/public_api/oak_embed_smoke.c)
is a complete program that CI compiles against the installed headers.

## Documentation

| I want to... | Read |
|---|---|
| Learn the language by running it | [`examples/`](examples/README.md) |
| Build, test, and install | [`docs/building.md`](docs/building.md) |
| Use the CLI or debugger | [`docs/cli.md`](docs/cli.md) |
| Depend on someone else's package | [`docs/packages.md`](docs/packages.md) |
| Publish a package of my own | [`docs/publishing.md`](docs/publishing.md) |
| Embed Oak in a C program | [`docs/embedding-c.md`](docs/embedding-c.md) |
| Edit Oak in VS Code | [`editors/vscode/`](editors/vscode/README.md) |
