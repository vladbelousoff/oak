# CLI and debugging

## Run a script

```sh
oak path/to/program.oak
oak examples/01_values/01_values.oak
```

Flags go before the script path. Everything after the script path is
passed through to the script unchanged.

```text
oak [options] <script> [script args...]
```

| Option | What it does |
|---|---|
| `--debug` | Start a localhost debugger that VS Code can attach to |
| `--debug-port <port>` | Port for that debugger. `0` picks a free port |
| `--disassemble` | Print compiled bytecode instead of running the script |
| `--no-debug-symbols` | Compile without source debug metadata |
| `--allow-synthetic-modules` | If a native module has no `.oak` stub file, build the module from its C bindings instead of failing |
| `--track-memory` | Fail the process if tracked runtime allocations leak |
| `--unbuffered` | Flush every `print` immediately (useful when piping a long-running script) |
| `--help` | Print usage |

```sh
oak --disassemble examples/06_modules/06_modules.oak
oak --debug --debug-port 4711 examples/04_functions/04_functions.oak
oak --unbuffered long_running.oak
```

## VS Code

The extension in [`editors/vscode`](../editors/vscode/README.md) adds
syntax highlighting and source debugging for `.oak` files.

To try it from this repo:

```sh
code editors/vscode
```

Select **Run Extension** and press `F5`. The debugger looks for `oak`
on the `PATH` that VS Code inherited, so `oak --help` should work in
that environment.

To attach to a script that is already running:

```sh
oak --debug --debug-port 4711 path/to/program.oak
```

Then attach from VS Code on port `4711`.
