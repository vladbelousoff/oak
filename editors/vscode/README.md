# Oak VS Code Debugger

Open `editors/vscode` in VS Code and run the extension, or package it with
`npx @vscode/vsce package`.

The `oak` debug type supports:

- launch and localhost attach sessions
- source breakpoints, pause, continue, step in, step over, and step out
- stack frames, locals, local-name watches, and expandable collection values

For attach sessions, start Oak first:

```sh
./build/oak --debug --debug-port 4711 path/to/program.oak
```
