# Oak language tour

These examples are the language, in order. Start at
`01_values/01_values.oak` and walk down the numbered folders. Each one
adds a piece.

```sh
./build/oak examples/01_values/01_values.oak
```

On Windows, run `.\build\oak.exe` instead.

CI runs these as smoke tests. An example with a `.expected_error` file
is supposed to fail; everything else must exit successfully. Each
runnable example lives in a folder with the same name as its entry file.

## Tour

1. `01_values` — numbers, strings, booleans, `let`, formatting
2. `02_control_flow` — `if`, loops, `break`, `continue`
3. `03_collections` — arrays, maps, indexing, iteration
4. `04_functions` — named functions, recursion
5. `05_records_enums` — records, methods, mutation, enums
6. `06_modules` — imports, aliases, exported types
7. `07_algorithms` — a small program built from the earlier pieces
8. `08_file_io` — `io.File`
9. `09_diagnostics` — what a module-cycle error looks like
10. `10_interfaces` — interfaces and virtual dispatch
11. `11_weak_refs` — weak references in records and parameters
12. `12_strings` — string methods and conversion built-ins
13. `13_anonymous_functions` — function values, lambdas, higher-order functions
