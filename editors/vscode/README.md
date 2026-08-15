# Oak for VS Code

Syntax highlighting and source debugging for `.oak` files. The
language itself lives in the [Oak repository](https://github.com/vladbelousoff/oak).

## What you get

- Highlighting for keywords, types, strings, numbers, comments,
  attributes, and operators
- Bracket matching and auto-closing for `()`, `[]`, `{}`, and
  `'...'` strings
- A debugger: launch or attach, breakpoints, step in / over / out,
  stack frames, locals, watches, and expandable collections

## Try it from this repo

There is nothing to install and no build step.

1. Open `editors/vscode` as the workspace root
   (`code editors/vscode`). The **Run Extension** configuration only
   appears when this folder is the workspace root.
2. Open the Run and Debug panel (`Ctrl+Shift+D`), select
   **Run Extension**, and press `F5`.
3. In the new window, open any `.oak` file. Highlighting should
   turn on from the extension.

To package it as a regular `.vsix` instead:

```sh
cd editors/vscode
npx @vscode/vsce package
code --install-extension vscode-oak-<version>.vsix
```

## Debugging

Launch sessions look for `oak` on the `PATH` that VS Code inherited.
If `oak --help` works in a terminal started the same way VS Code was,
you are set.

Open a `.oak` file, select **Debug Current Oak File**, and press `F5`.
The starter configuration runs the active file from that file's
directory:

```json
{
  "type": "oak",
  "request": "launch",
  "name": "Debug Current Oak File",
  "program": "${file}",
  "cwd": "${fileDirname}",
  "args": []
}
```

Set `oakExecutable` only if you need a specific binary:

```json
"oakExecutable": "/absolute/path/to/oak"
```

To run without the debugger, use the integrated terminal:

```sh
oak path/to/program.oak
```

To attach to a script that is already running, start Oak first:

```sh
oak --debug --debug-port 4711 path/to/program.oak
```

then attach:

```json
{
  "type": "oak",
  "request": "attach",
  "name": "Attach to Oak",
  "port": 4711
}
```

## Editing the grammar

Highlighting comes from `syntaxes/oak.tmLanguage.json` (scope
`source.oak`) and `language-configuration.json`. It activates for
any `.oak` file; no settings are required.

Notes if you are changing the grammar:

- Comments are block-only: `/* ... */`. Oak has no `//` line comment
  — `//` is integer division.
- Strings are single-quoted only: `'like this'`, with `\n`, `\t`,
  `\r`, `\\`, `\'`, and `\"` escapes. Double-quoted strings are a
  lexer error.
- Built-in types are `number`, `string`, `bool`, `void`, and `fn`.
  Capitalized identifiers (records, enums, interfaces) are
  highlighted as types heuristically.
- Attributes use `@Name` syntax, placed before a declaration.
