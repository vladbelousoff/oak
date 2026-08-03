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

### Interfaces

Interfaces declare method signatures and dispatch virtually over the record
methods that implement them. Interface names must start with `I`:

```oak
interface IShape {
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

let mut shapes = new IShape[];
shapes.push(c);
shapes.push(r);

let mut total = 0;
for s in shapes {
  total += s.area();
}

print(total);
```

### Modules

Modules are resolved relative to the entry script. A module only exposes
declarations marked with `export`; import selected names, everything exported,
or a namespace alias:

```oak
import { sum, average } from analytics.stats;
import * from domain.project;
import domain.project as project;

print(sum([3, 5, 8, 13]));
print(project.summary(project.make_task('ship', 8, project.Priority.High)));
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

### Object IDs, VM ownership, and threads

Every Oak value occupies one 64-bit word. Object and weak-object values use
the low 3 bits as their tag and form their ID from three fields:

| Bits | Field | Purpose |
|---|---|---|
| `3..31` | 29-bit slot | Index in an object table |
| `32..37` | 6-bit table | Owning VM's table (`0` means process-shared; 64 tables total) |
| `38..39` | reserved | Currently zero |
| `40..63` | 24-bit nonce | Slot generation when the ID was created |

Ownership is explicit and does not use thread-local state. VM bytecode routes
its allocations through its `oak_vm_t`, and native code can select an owner
with `oak_vm_string_new()`, `oak_vm_array_new()`, `oak_vm_map_new()`,
`oak_vm_record_new()`, and `oak_vm_native_record_new()`. The plain
`oak_*_new()` functions create process-shared table-0 values. One thread may
operate on different VMs, and a VM may move between threads between calls;
neither a VM nor its values may be accessed concurrently. At most 63 VM
tables can be live alongside the shared table.

When an object dies, its slot's nonce is incremented before the slot can be
reused. A weak ID therefore resolves to `none` when its saved nonce no longer
matches, rather than accidentally resolving to the next object allocated in
the same slot. The nonce floor is also advanced when an entire VM table is
recycled.

Heap values created by a VM are VM-confined. The runtime rejects
putting either a strong or weak reference from one VM into another VM's stack,
array, map, record, native field, or call arguments; release builds enforce the
same checks as debug builds. Scalar values and process-owned table-0 objects
(such as compiled constants and native definitions) may be used by every VM.
To communicate between workers, exchange host data or scalar Oak values and
recreate arrays, maps, records, and strings in the destination VM instead of
passing an `oak_value_t` object from the source VM.

## Benchmarks

Cross-language benchmarks against peer bytecode interpreters (no JITs) run in
CI whenever interpreter code changes and the table below is updated
automatically. Workloads, methodology, and caveats are described in
[`benchmark/`](benchmark/README.md).

<!-- benchmark:start -->
| runtime | fib | nsieve | mandelbrot | hashmap | strcat |
|---|---|---|---|---|---|
| **oak** | 2.93× (6.40 s) | 6.14× (8.13 s) | 5.12× (9.18 s) | 3.16× (7.44 s) | 4.26× (7.85 s) |
| lua5.4 | 1.00× (2.18 s) | 1.00× (1.32 s) | 1.00× (1.79 s) | 1.70× (4.00 s) | 2.55× (4.70 s) |
| python3 | 1.87× (4.09 s) | 2.94× (3.89 s) | 10.10× (18.11 s) | 2.94× (6.92 s) | 4.40× (8.10 s) |
| ruby | 1.68× (3.66 s) | 2.02× (2.67 s) | 2.45× (4.40 s) | 2.25× (5.29 s) | 3.10× (5.71 s) |
| perl | 7.71× (16.81 s) | 4.79× (6.34 s) | 5.48× (9.83 s) | 1.25× (2.94 s) | 1.00× (1.84 s) |
| php | 7.78× (16.95 s) | 3.63× (4.80 s) | 3.41× (6.12 s) | 1.00× (2.35 s) | 1.40× (2.57 s) |

_Relative to the fastest runtime per benchmark, lower is better; median wall time in parentheses. Measured on a GitHub-hosted `ubuntu-latest` runner at `41ba0815a` on 2026-08-03. All runtimes are bytecode interpreters (no JIT)._
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
