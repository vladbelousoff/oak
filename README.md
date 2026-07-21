# Oak

Oak is a statically typed scripting language implemented in C17. It compiles
`.oak` source to bytecode and runs it on a stack-based virtual machine. Memory
is fully deterministic: values are reference-counted and the **compiler
rejects programs that could form strong reference cycles**, so there is no
garbage collector and no runtime cycle detection.

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

The repo includes the `oak` CLI, the `acorn` C/C++ embedding library, a VS
Code extension, examples, tests, and a WebAssembly playground.

## Quick Start

```sh
meson setup build
meson compile -C build
./build/oak examples/01_values/01_values.oak
```

On Windows, run `.\build\oak.exe` instead of `./build/oak`. See
[Building and Installing](docs/building.md) for requirements, tests, install,
and the web playground, and [CLI and Debugging](docs/cli.md) for the `oak`
command-line options.

## The Language

The snippets below cover the core of Oak; every one is a complete program.
The numbered examples in [`examples/`](examples/README.md) are the full
language tour and run as smoke tests in CI.

### Values and Bindings

Bindings are immutable by default; `let mut` opts into mutation. Strings are
**single-quoted** — double quotes are a lexer error. `/` always produces a
float, `//` is integer division. Comments are `/* ... */` blocks.

```oak
let answer = 40 + 2;
let ready = answer == 42;

let mut stock = 10;
stock += 5;
stock *= 2;

let ratio = 9 / 2;   /* 4.5  — `/` is float division */
let half = 9 // 2;   /* 4    — `//` is integer division */

let words = ['Oak', 'scripts'];
print('{} {}'.format(words));
print('{0}+{1}={2}'.format([2, 3, 5]));
```

### Control Flow

`if`/`else`, `while`, counted `for ... from ... to` (exclusive upper bound),
collection iteration, `break`, and `continue`:

```oak
for i from 1 to 20 {
  if i % 2 == 0 {
    continue;
  }
  if i > 9 {
    break;
  }
  print(i);
}

let mut n = 13;
let mut steps = 0;
while n != 1 {
  if n % 2 == 0 {
    n = n // 2;
  } else {
    n = n * 3 + 1;
  }
  steps += 1;
}
print(steps);
```

### Collections

Typed arrays and maps, with index/key iteration:

```oak
let mut scores = new number[];
scores.push(10);
scores.push(20);
scores[1] = 25;

for i, score in scores {
  print('{}: {}'.format([i, score]));
}

let mut inventory = new [string:number];
inventory['apples'] = 4;
inventory['pears'] = 6;
print(inventory.has('apples'));
print(inventory.size());

for name, count in inventory {
  print(name);
}
```

### Functions

Functions are typed, first-class values; anonymous functions use the same
`fn` syntax as function types:

```oak
fn fib(n : number) -> number {
  if n < 2 {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}

let double = fn(x : number) -> number { return x * 2; };

fn apply(f : fn(number) -> number, x : number) -> number {
  return f(x);
}

print(apply(double, fib(10)));
```

### Records and Enums

Records are created with `new Type { ... }`; methods are declared as
`fn Type.method(self, ...)`, with `mut self` for mutation. Enum variants get
enum-aware type checking:

```oak
enum Status { Planned, Active, Done }

record Point {
  x : number;
  y : number;
}

fn Point.move_by(mut self, dx : number, dy : number) {
  self.x = self.x + dx;
  self.y = self.y + dy;
}

record Job {
  title : string;
  status : Status;
  location : Point;
}

fn Job.label(self) -> string {
  return self.title;
}

let mut p = new Point { x : 3, y : 4 };
p.move_by(10, -2);
print(p.x);

let job = new Job { title : 'release', status : Status.Planned, location : p };
print(job.label());
print(job.status == Status.Planned);
```

### Traits

Traits declare method signatures and dispatch virtually over the record
methods that implement them:

```oak
trait Shape {
  fn area(self) -> number;
}

record Circle {
  radius : number;
}

fn Circle.area(self) -> number {
  return 3.14159 * self.radius * self.radius;
}

record Rect {
  w : number;
  h : number;
}

fn Rect.area(self) -> number {
  return self.w * self.h;
}

let mut c = new Circle { radius : 5 };
let mut r = new Rect { w : 3, h : 4 };

let mut shapes = new Shape[];
shapes.push(c);
shapes.push(r);

let mut total = 0;
for s in shapes {
  total += s.area();
}

print(total);
```

### Modules

Modules are resolved relative to the entry script; import selected names or
everything a module exports:

```oak
import { sum, average } from analytics.stats;
import * from domain.project;

print(sum([3, 5, 8, 13]));
```

See [`examples/06_modules/`](examples/06_modules/) for a complete multi-file
program.

### Memory: Reference Counting Without Cycles

Oak has no garbage collector and no runtime cycle detector. Reference
counting alone reclaims every object because the compiler **rejects programs
that could form strong reference cycles**: fields that could close a cycle
must be write-once or declared `weak`. A weak reference does not keep its
target alive and can be compared against `none`:

```oak
record Node {
  name : string;
  links : Edge[];
}

record Edge {
  label : string;
  target : Node weak;  /* weak: does not keep the target alive */
}

let mut a = new Node { name : 'a', links : new Edge[] };
let mut b = new Node { name : 'b', links : new Edge[] };

a.links.push(new Edge { label : 'a->b', target : b });
b.links.push(new Edge { label : 'b->a', target : a });

print(a.links[0].target.name);
```

The same invariant applies to native bindings — host code must never create a
strong ownership loop from C or C++.

### Strings and the Stdlib

Everyday text and number work needs no imports:

```oak
let greeting = 'Hello, Oak';
print(greeting.upper());
print(greeting.contains('Oak'));
print(greeting.replace('Hello', 'Hey'));
print('HelloWorld'.to_snake_case());

let width = parse_number('  16 ');
print(to_int(pow(2.0, 10.0)));
print('{} squared is {}'.format([width, width * width]));
```

The stdlib ships printing and conversion builtins (`to_int`, `parse_number`,
`ord`, `chr`), math (`sqrt`, `pow`, `floor`, `ceil`, `round`, `log`, `exp`,
`sign`, `min`/`max`, trig, ...), string methods (`upper`, `lower`, `trim`,
`contains`, `starts_with`, `ends_with`, `index_of`, `replace`, `repeat`,
`substring`, `to_snake_case`, `to_camel_case`, `format`), collection methods,
and file I/O via `io.File` (see
[`examples/08_file_io/`](examples/08_file_io/)).

## Documentation

| Topic | Where |
|---|---|
| Language tour — runnable, numbered examples | [`examples/`](examples/README.md) |
| Building, installing, web playground | [`docs/building.md`](docs/building.md) |
| CLI options and debugging | [`docs/cli.md`](docs/cli.md) |
| Embedding: C API | [`docs/embedding-c.md`](docs/embedding-c.md) |
| Embedding: C++ API | [`docs/embedding-cpp.md`](docs/embedding-cpp.md) |
| VS Code extension | [`editors/vscode/`](editors/vscode/README.md) |
| Benchmark suite and methodology | [`benchmark/`](benchmark/README.md) |

## Embedding

Link against `acorn` and register native types, functions, enums, and
attributes before compiling; bindings participate in Oak's compile-time type
checks. The C API is descriptor-based, and the C++20 wrapper
([`include/oak.hpp`](include/oak.hpp)) adds RAII and typed callable binding:

```cpp
oak::Allocator alloc;
oak::CompileOptions opts(alloc);

opts.bind_fn("add", [](int a, int b) { return a + b; });
opts.bind_enum("Color", {{"Red", 0}, {"Green", 1}, {"Blue", 2}});

auto result = oak::compile(
    "let n = add(20, 22);\n"
    "let color = Color.Green;\n",
    opts);

oak::VM vm(alloc);
vm.run(result);
```

See the [C embedding guide](docs/embedding-c.md) and the
[C++ embedding guide](docs/embedding-cpp.md).

## Benchmarks

Cross-language benchmarks against peer bytecode interpreters (no JITs) run in
CI whenever interpreter code changes and the table below is updated
automatically. Workloads, methodology, and caveats are described in
[`benchmark/`](benchmark/README.md).

<!-- benchmark:start -->
| runtime | fib | nsieve | mandelbrot | hashmap | strcat |
|---|---|---|---|---|---|
| **oak** | 2.49× (5.47 s) | 4.97× (6.47 s) | 4.44× (7.85 s) | 2.84× (6.66 s) | 4.58× (8.25 s) |
| lua5.4 | 1.00× (2.20 s) | 1.00× (1.30 s) | 1.00× (1.77 s) | 1.67× (3.91 s) | 2.60× (4.68 s) |
| python3 | 1.94× (4.27 s) | 3.03× (3.94 s) | 9.97× (17.63 s) | 2.95× (6.91 s) | 4.23× (7.64 s) |
| ruby | 1.98× (4.35 s) | 2.02× (2.64 s) | 2.49× (4.40 s) | 2.17× (5.09 s) | 3.00× (5.41 s) |
| perl | 7.61× (16.75 s) | 4.74× (6.17 s) | 5.62× (9.93 s) | 1.30× (3.05 s) | 1.00× (1.80 s) |
| php | 7.70× (16.95 s) | 3.68× (4.79 s) | 3.44× (6.08 s) | 1.00× (2.34 s) | 1.41× (2.55 s) |

_Relative to the fastest runtime per benchmark, lower is better; median wall time in parentheses. Measured on a GitHub-hosted `ubuntu-latest` runner at `5634d9ef1` on 2026-07-21. All runtimes are bytecode interpreters (no JIT)._
<!-- benchmark:end -->

## Layout

| Path | Contents |
|---|---|
| `oak.c`, `oak_cli.c` | native CLI |
| `include/` | public C API and C++ wrapper |
| `src/` | compiler, runtime, VM, and stdlib C code |
| `stdlib/` | Oak stdlib modules |
| `docs/` | build, CLI, and embedding guides |
| `tests/` | C/C++ tests |
| `examples/` | runnable language tour |
| `editors/vscode/` | VS Code extension |
| `wasm/`, `www/` | WebAssembly playground |
