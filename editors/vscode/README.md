# oak Language and Debugger for VS Code

Syntax highlighting and source-level debugging support for the oak
programming language. See the
[oak repository](https://github.com/vladbelousoff/oak) for the language
itself.

## Features

- Syntax highlighting for `.oak` files (keywords, types, strings, numbers,
  comments, attributes, operators)
- Bracket matching and auto-closing for `()`, `[]`, `{}`, and `'...'` strings
- An `oak` debug adapter with launch and localhost-attach sessions, source
  breakpoints, pause/continue/step in/step over/step out, stack frames,
  locals, local-name watches, and expandable collection values

## Installing / running the extension

The extension is purely declarative — there are no dependencies to install
and no build step. From a checkout of this repo:

1. Open `editors/vscode` as the workspace root in VS Code
   (`code editors/vscode`). The bundled `.vscode/launch.json` defines a
   **Run Extension** configuration, which only appears when this folder is
   the workspace root.
2. Open the Run and Debug panel (`Ctrl+Shift+D`), select **Run Extension**,
   and press `F5` to launch an Extension Development Host with the extension
   loaded.
3. Open or create a `.oak` file in that new window — it should be
   highlighted automatically based on its `.oak` extension.

To package and install it as a regular `.vsix` extension instead:

```sh
cd editors/vscode
npx @vscode/vsce package
code --install-extension vscode-oak-<version>.vsix
```

## Syntax highlighting

Highlighting is driven by the TextMate grammar at
`syntaxes/oak.tmLanguage.json` (scope `source.oak`) and
`language-configuration.json` (comments, brackets, auto-closing pairs).
No configuration is required — it activates for any file with the `.oak`
extension.

Notes on oak's lexical syntax, for anyone editing the grammar:

- Comments are block-only: `/* ... */`. oak has no `//` line comment —
  `//` is the floor-division operator.
- Strings are single-quoted only: `'like this'`, with `\n`, `\t`, `\r`,
  `\\`, `\'`, and `\"` escapes. Double-quoted strings are a lexer error.
- Built-in types are `number`, `string`, `bool`, `void`, and `fn`.
  Capitalized identifiers (records, enums, traits) are highlighted as
  types heuristically.
- Attributes use `@Name` syntax, e.g. `@Native`, placed before a
  declaration.

## Debugging

The `oak` debug type supports:

- launch and localhost attach sessions
- source breakpoints, pause, continue, step in, step over, and step out
- stack frames, locals, local-name watches, and expandable collection values

Launch sessions use the `oak` executable from `PATH` by default. Make sure
`oak --help` works in the environment used to start VS Code, then open any
`.oak` file, select **Debug Current oak File** in the Run and Debug panel, and
press `F5`.

The contributed starter configuration runs the active file from that file's
directory, so it works for standalone files as well as workspace files:

```json
{
  "type": "oak",
  "request": "launch",
  "name": "Debug Current oak File",
  "program": "${file}",
  "cwd": "${fileDirname}",
  "args": []
}
```

Add `oakExecutable` only when you want to override PATH lookup:

```json
"oakExecutable": "/absolute/path/to/oak"
```

To run without the debugger, use VS Code's integrated terminal:

```sh
oak path/to/program.oak
```

For attach sessions, start oak first:

```sh
oak --debug --debug-port 4711 path/to/program.oak
```

then attach with a configuration like:

```json
{
  "type": "oak",
  "request": "attach",
  "name": "Attach to oak",
  "port": 4711
}
```
