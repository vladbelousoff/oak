# Oak Language and Debugger for VS Code

Syntax highlighting and source-level debugging support for the
[Oak](https://github.com/) programming language.

## Features

- Syntax highlighting for `.oak` files (keywords, types, strings, numbers,
  comments, attributes, operators)
- Bracket matching and auto-closing for `()`, `[]`, `{}`, and `'...'` strings
- An `oak` debug adapter with launch and localhost-attach sessions, source
  breakpoints, pause/continue/step in/step over/step out, stack frames,
  locals, local-name watches, and expandable collection values

## Installing / running the extension

From a checkout of this repo:

1. Open `editors/vscode` as the workspace root in VS Code
   (`code editors/vscode`).
2. Run `npm install` if the extension has any dependencies to fetch
   (currently none are required for syntax highlighting alone).
3. Press `F5` (or use Run and Debug → "Run Extension") to launch an
   Extension Development Host with the extension loaded.
4. Open or create a `.oak` file in that new window — it should be
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

Notes on Oak's lexical syntax, for anyone editing the grammar:

- Comments are block-only: `/* ... */`. Oak has no `//` line comment —
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

For launch sessions, point `oakExecutable` at your built `oak` CLI
(defaults to `${workspaceFolder}/build/oak`) and `program` at the `.oak`
file to run. A starter launch configuration is contributed automatically
(`Debug Oak Program`); add or adjust it in your project's
`.vscode/launch.json` as needed.

For attach sessions, start Oak first:

```sh
./build/oak --debug --debug-port 4711 path/to/program.oak
```

then attach with a configuration like:

```json
{
  "type": "oak",
  "request": "attach",
  "name": "Attach to Oak",
  "port": 4711
}
```
