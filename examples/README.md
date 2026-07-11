# Oak Language Tour

The examples are ordered as a tour. Start at `01_values/01_values.oak`
and move down the numbered folders; each one introduces a larger piece of
the language.

Run any example entry point with:

```sh
./build/oak examples/01_values/01_values.oak
```

Meson runs these examples as smoke tests. A `.expected_error` file marks an
example as intentionally failing; otherwise the example must exit successfully.
Each runnable example lives in a folder with the same name as its entry file.

## Tour

1. `01_values/01_values.oak` - numbers, strings, booleans, assignment, formatting
2. `02_control_flow/02_control_flow.oak` - branches, loops, `break`, `continue`
3. `03_collections/03_collections.oak` - arrays, maps, indexing, iteration
4. `04_functions/04_functions.oak` - functions, recursion, mutual recursion
5. `05_records_enums/05_records_enums.oak` - records, methods, mutation, enums
6. `06_modules/06_modules.oak` - imports, aliases, exported types
7. `07_algorithms/07_algorithms.oak` - a compact algorithm built from the earlier pieces
8. `08_file_io/08_file_io.oak` - native `File` bindings
9. `09_diagnostics/09_diagnostics.oak` - module-cycle diagnostics
10. `10_traits/10_traits.oak` - traits and virtual dispatch
11. `11_weak_refs/11_weak_refs.oak` - weak references in records and function parameters
12. `12_strings/12_strings.oak` - string methods and conversion built-ins
