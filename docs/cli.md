# CLI and Debugging

## Usage

```text
oak [options] <script> [script args...]
```

Options come before the script path. Everything after the script path is passed
to the script unchanged.

| Option | Effect |
|---|---|
| `--debug` | Start the localhost VS Code/DAP debugger server |
| `--debug-port <port>` | Debug server port; use `0` to choose a free port |
| `--disassemble` | Print compiled bytecode instead of running |
| `--no-debug-symbols` | Compile without source debug metadata |
| `--track-memory` | Fail if tracked runtime allocations leak |
| `--help` | Print usage |

```sh
oak examples/01_values/01_values.oak
oak --disassemble examples/06_modules/06_modules.oak
oak --debug --debug-port 4711 examples/04_functions/04_functions.oak
```

## VS Code

The extension is in [`editors/vscode`](../editors/vscode/README.md). It
provides `.oak` syntax highlighting and source debugging. The debugger assumes
`oak` is on the `PATH` inherited by VS Code.

```sh
code editors/vscode
```

Select **Run Extension** and press `F5`. To attach to a running script:

```sh
oak --debug --debug-port 4711 path/to/program.oak
```
