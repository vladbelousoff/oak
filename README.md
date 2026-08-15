# Oak

Oak is a small, statically typed scripting language. You write `.oak` files
and run them with the `oak` command — or from C, using the `acorn` library.

There is no garbage collector. Values are reference-counted, and the
compiler refuses programs that could form a cycle. If a program compiles,
memory is reclaimed when the last reference goes away.

```oak
fn sum(values : number[]) -> number {
  let mut total = 0;
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

`let` is immutable. Use `let mut` when you need to change a binding.
Strings are **single-quoted** — double quotes are not allowed.
`/` is always float division; `//` is integer division.
Comments are `/* ... */` only. There is no `//` comment: `//` is the
division operator.

```oak
let answer = 40 + 2;
let mut stock = 10;
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
let mut scores = new number[];
scores.push(10);
scores[0] = 12;

let mut inventory = new [string:number];
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

Modules are resolved relative to the entry script. Only names marked
`export` are visible. Import selected names, everything, or a namespace:

```oak
import { sum, average } from analytics.stats;
import * from domain.project;
import domain.project as project;
```

See [`examples/06_modules/`](examples/06_modules/) for a complete
multi-file program.

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
                       .module_name = null,
                       .name = "add",
                       .impl = native_add,
                       .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                       .param_types = add_params,
                       .param_count = OAK_COUNT_OF(add_params),
                       .user_data = null,
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
| Embed Oak in a C program | [`docs/embedding-c.md`](docs/embedding-c.md) |
| Edit Oak in VS Code | [`editors/vscode/`](editors/vscode/README.md) |
