# oak Benchmark Suite

Cross-language benchmarks comparing oak against peer scripting languages:

| runtime | class |
|---|---|
| oak (`build/oak`) | bytecode interpreter |
| Lua 5.4 | bytecode interpreter |
| Python 3 (CPython) | bytecode interpreter |
| Ruby 3 | bytecode interpreter |
| LuaJIT | tracing JIT (reference point) |
| Node.js (V8) | optimizing JIT (reference point) |
| C# (mono) | JIT; AOT `mcs` compile excluded from timing |

## Running

```sh
./setup.sh        # sudo apt install lua5.4 ruby mono-mcs mono-runtime hyperfine
./run.py          # verify checksums, time everything, write results/RESULTS.md
```

oak must be built **optimized** — debug builds (`buildtype=debug`, meson's
default) are -O0 with memory tracking compiled in, run 4–8x slower, and are
refused by the runner:

```sh
meson setup build-release -Dbuildtype=release
meson compile -C build-release
```

The runner accepts `../build-release/oak` or `../build/oak`, whichever has a
confirmed non-debug meson buildtype.
Missing runtimes are skipped with a note; you can benchmark any subset:

```sh
./run.py --list                       # show benchmarks and languages
./run.py --bench fib,nsieve           # subset of benchmarks
./run.py --lang oak,python3,luajit    # subset of languages
./run.py --warmup 5 --min-runs 10     # more stable statistics
```

## Benchmarks

Each benchmark exists once per language (`<bench>/<bench>.<ext>`); the `.lua`
source is shared by `lua5.4` and `luajit`. Every program prints a small
deterministic checksum which `run.py` verifies against `<bench>/expected.txt`
**before** timing — a runtime that produces a wrong answer is excluded and
reported, never silently timed.

| benchmark | measures | workload | checksum |
|---|---|---|---|
| `fib` | function-call / recursion cost | naive recursive `fib(30)` | `832040` |
| `nsieve` | loops, int arithmetic, array access | sieve of Eratosthenes to 500 000 | `41538` primes |
| `mandelbrot` | float arithmetic | 256×256 grid, 64 iterations max | `25726` interior points |
| `hashmap` | string-keyed map insert/lookup | 300 000 ops over 20 011 keys | size + lookup sum |
| `strcat` | string building / allocation | 1 000 000 short concat+format strings | total length |

Workloads are sized so the slowest runtime takes roughly 1–5 s, which keeps
process startup (tens of ms for node/mono) in the noise.

## Methodology and caveats

- **Whole-process timing.** oak has no clock builtin, so hyperfine measures
  complete process wall time — including interpreter startup and, for oak,
  the compile-to-bytecode phase. This is the honest number for a scripting
  language but differs from in-process loop timing.
- **oak computes in i32/f32.** Other languages use 64-bit ints (or f64
  doubles). All integer checksums are constructed to stay below 2³¹.
  In `mandelbrot`, oak's f32 can flip a few boundary pixels, so `run.py`
  allows ±0.5 % relative deviation on that checksum (all other benchmarks
  must match exactly). Grid coordinates are exact binary fractions to keep
  f32/f64 divergence minimal.
- **Interpreters vs JITs.** LuaJIT, Node, and mono compile hot code to
  machine code; comparing them to interpreters is an upper-bound reference,
  not a like-for-like comparison.
- **oak runs with `--no-debug-symbols`** (its release configuration — skips
  debug-info emission during compilation), symmetric to `mcs -optimize+`.
- **Comparable, not micro-tuned.** Implementations use the same algorithm
  and equivalent data structures in each language (e.g. `while` loops in
  Ruby instead of iterators where that mirrors the other sources), without
  language-specific tricks.
- `strcat` deliberately builds many short strings instead of one growing
  accumulator: oak has no StringBuilder, and naive `s = s + chunk` is O(n²)
  in some languages but not others, which would make the comparison
  meaningless.

## Results

`run.py` writes per-benchmark hyperfine exports (`results/<bench>.json`,
`results/<bench>.md`), an aggregated `results/RESULTS.md` with a relative
summary matrix, environment versions, and any skipped/failed runtimes, and a
machine-readable `results/summary.json`.
`results/` is gitignored — numbers are machine-specific; regenerate locally.

The `Benchmarks` GitHub Actions workflow (`.github/workflows/benchmark.yml`)
runs the full suite on every push to `main` that touches interpreter code and
injects the summary matrix into the top-level `README.md` via
`update_readme.py` (between the `<!-- benchmark:start/end -->` markers). Full
hyperfine output lands in the job summary and a `benchmark-results` artifact.

## Adding a benchmark

1. Create `newbench/` with `newbench.<ext>` for each language and an
   `expected.txt` holding the canonical output.
2. Add `"newbench"` to `BENCHMARKS` in `run.py` (and a tolerance entry in
   `TOLERANCES` if the checksum is float-sensitive).
3. Size the workload so the slowest runtime lands in 1–5 s and integer
   results stay below 2³¹.
