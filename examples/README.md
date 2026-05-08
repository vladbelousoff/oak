# Oak Language Tour

The examples are ordered as a tour. Start at `01_values.oak` and move down the
numbered files; each one introduces a larger piece of the language.

Run any top-level example with:

```sh
./build/oak examples/01_values.oak
```

CTest runs these examples as smoke tests. A `.expected_error` file marks an
example as intentionally failing; otherwise the example must exit successfully.
Multi-file examples live in numbered subdirectories and use a `_main.oak`
entry point.

## Tour

1. `01_values.oak` - numbers, strings, booleans, assignment, formatting
2. `02_control_flow.oak` - branches, loops, `break`, `continue`
3. `03_collections.oak` - arrays, maps, indexing, iteration
4. `04_functions.oak` - functions, recursion, mutual recursion
5. `05_records_enums.oak` - records, methods, mutation, enums
6. `06_modules/showcase_main.oak` - imports, aliases, exported types
7. `07_algorithms.oak` - a compact algorithm built from the earlier pieces
8. `08_file_io.oak` - native `File` bindings
9. `09_diagnostics/cycle_main.oak` - module-cycle diagnostics
