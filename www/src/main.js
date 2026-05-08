import {
  EditorView,
  keymap,
  lineNumbers,
  highlightActiveLine,
  highlightActiveLineGutter,
  placeholder,
} from '@codemirror/view';
import { EditorState } from '@codemirror/state';
import {
  defaultKeymap,
  indentWithTab,
  history,
  historyKeymap,
} from '@codemirror/commands';
import {
  syntaxHighlighting,
  HighlightStyle,
  indentUnit,
  bracketMatching,
} from '@codemirror/language';
import { tags } from '@lezer/highlight';
import { oak } from './oak-lang.js';

// ── Catppuccin Mocha theme ────────────────────────────────────────────────────
const theme = EditorView.theme({
  '&': {
    height: '100%',
    background: '#1e1e2e',
    color: '#cdd6f4',
  },
  '.cm-scroller': {
    fontFamily: "'Cascadia Code', 'Fira Code', 'JetBrains Mono', monospace",
    fontSize: '14px',
    lineHeight: '1.6',
    overflow: 'auto',
  },
  '.cm-content': {
    padding: '16px',
    caretColor: '#cdd6f4',
  },
  '.cm-gutters': {
    background: '#181825',
    color: '#45475a',
    border: 'none',
    borderRight: '1px solid #313244',
  },
  '.cm-lineNumbers .cm-gutterElement': {
    padding: '0 10px 0 12px',
    minWidth: '48px',
    textAlign: 'right',
  },
  '.cm-activeLineGutter': { background: 'rgba(49,50,68,0.5)' },
  '.cm-activeLine':       { background: 'rgba(49,50,68,0.3)' },
  '&.cm-focused':                              { outline: 'none' },
  '&.cm-focused .cm-cursor':                  { borderLeftColor: '#cdd6f4' },
  '&.cm-focused .cm-selectionBackground, .cm-selectionBackground': {
    background: '#313244',
  },
  '.cm-matchingBracket': { background: '#45475a', outline: 'none', color: 'inherit' },
  '.cm-placeholder':     { color: '#45475a', fontStyle: 'italic' },
}, { dark: true });

const highlightStyle = HighlightStyle.define([
  { tag: tags.keyword,             color: '#cba6f7' },
  { tag: tags.typeName,            color: '#89b4fa' },
  { tag: tags.standard(tags.name), color: '#89dceb' },
  { tag: tags.string,              color: '#a6e3a1' },
  { tag: tags.number,              color: '#fab387' },
  { tag: tags.atom,                color: '#f38ba8' },
  { tag: tags.comment,             color: '#6c7086', fontStyle: 'italic' },
]);

// ── DOM ───────────────────────────────────────────────────────────────────────
const output  = document.getElementById('output');
const runBtn  = document.getElementById('run-btn');
const status  = document.getElementById('status');
const examplesTree = document.getElementById('examples-tree');
const WASM_ASSET_VERSION = 'examples-tree-fs-v2';

// ── WASM ──────────────────────────────────────────────────────────────────────
let oakRunFile = null;
let wasmFS = null;
let captured = [];
let activeEntryPath = '/playground/main.oak';

function clearOutput() {
  output.innerHTML = '';
}

function appendOutput(text, isErr) {
  const line = document.createElement('span');
  line.className = isErr ? 'err-line' : 'out-line';
  line.textContent = text + '\n';
  output.appendChild(line);
  output.scrollTop = output.scrollHeight;
}

function run() {
  if (!oakRunFile || !wasmFS) return;
  clearOutput();
  captured = [];
  const code = view.state.doc.toString();
  let exitCode;
  try {
    wasmFS.writeFile(activeEntryPath, code);
    exitCode = oakRunFile(activeEntryPath);
  } catch (e) {
    appendOutput('Runtime exception: ' + e, true);
    return;
  }
  for (const { text, err } of captured) appendOutput(text, err);
  if (output.children.length === 0) {
    const ph = document.createElement('span');
    ph.className = 'placeholder';
    ph.textContent = exitCode === 0 ? 'Finished with no output.' : `Exited with code ${exitCode}.`;
    output.appendChild(ph);
  }
}

window.OakModule({
  locateFile: (path, prefix) => {
    if (path.endsWith('.wasm')) {
      return `${prefix}${path}?v=${WASM_ASSET_VERSION}`;
    }
    return prefix + path;
  },
  print:    (text) => { if (oakRunFile) captured.push({ text, err: false }); },
  printErr: (text) => { if (oakRunFile) captured.push({ text, err: true }); },
}).then(module => {
  wasmFS = module.FS;
  if (!wasmFS) {
    throw new Error('WASM filesystem API is unavailable; refresh the page to load the latest runtime.');
  }
  mountExamples(wasmFS);
  ensureDir(wasmFS, '/playground');
  oakRunFile = module.cwrap('oak_run_file_wrapper', 'number', ['string']);
  runBtn.disabled = false;
  status.textContent = 'Ready';
}).catch(err => {
  status.textContent = 'Failed to load';
  clearOutput();
  appendOutput('Failed to load WASM module: ' + err, true);
});

runBtn.addEventListener('click', run);

// ── Editor ────────────────────────────────────────────────────────────────────
const view = new EditorView({
  state: EditorState.create({
    doc: '',
    extensions: [
      lineNumbers(),
      highlightActiveLine(),
      highlightActiveLineGutter(),
      bracketMatching(),
      history(),
      keymap.of([
        { key: 'Ctrl-Enter', mac: 'Cmd-Enter', run: () => { run(); return true; } },
        ...defaultKeymap,
        ...historyKeymap,
        indentWithTab,
      ]),
      indentUnit.of('  '),
      oak,
      syntaxHighlighting(highlightStyle),
      theme,
      placeholder('Write Oak code here…'),
    ],
  }),
  parent: document.getElementById('editor'),
});

// ── Examples ──────────────────────────────────────────────────────────────────
const rawExampleFiles = import.meta.glob('@examples/**/*', {
  eager: true,
  query: '?raw',
  import: 'default',
});

const EXAMPLES = new Map();

const EXAMPLE_FILE_ENTRIES = Object.entries(rawExampleFiles).map(([path, src]) => [
  exampleRelativePath(path),
  src,
]);

const EXAMPLE_FILES = EXAMPLE_FILE_ENTRIES
  .filter(([relativePath]) => shouldShowExampleFile(relativePath))
  .map(([relativePath, src]) => {
    if (isRunnableExample(relativePath)) EXAMPLES.set(relativePath, src.trimEnd());
    return ['/examples/' + relativePath, src];
  });

function exampleRelativePath(path) {
  return path
    .replace(/^@examples\//, '')
    .replace(/^.*\/examples\//, '');
}

function isRunnableExample(path) {
  return path.endsWith('.oak');
}

function shouldShowExampleFile(path) {
  return path !== 'README.md' && !path.endsWith('.expected_error');
}

function ensureDir(fs, path) {
  const parts = path.split('/').filter(Boolean);
  let current = '';
  for (const part of parts) {
    current += '/' + part;
    if (!fs.analyzePath(current).exists) {
      fs.mkdir(current);
    }
  }
}

function mountExamples(fs) {
  ensureDir(fs, '/examples');
  for (const [path, src] of EXAMPLE_FILES) {
    ensureDir(fs, path.replace(/\/[^/]+$/, ''));
    fs.writeFile(path, src);
  }
}

function buildTree(paths) {
  const root = { dirs: new Map(), files: [] };
  for (const path of paths) {
    const parts = path.split('/');
    const fileName = parts.pop();
    let node = root;
    for (const part of parts) {
      if (!node.dirs.has(part)) {
        node.dirs.set(part, { dirs: new Map(), files: [] });
      }
      node = node.dirs.get(part);
    }
    node.files.push({ name: fileName, path });
  }
  return root;
}

function sortNames(a, b) {
  return a.localeCompare(b, undefined, { numeric: true });
}

function loadExample(path) {
  const source = EXAMPLES.get(path);
  if (!source) return;
  activeEntryPath = '/examples/' + path;
  view.dispatch({
    changes: { from: 0, to: view.state.doc.length, insert: source },
  });
  for (const item of examplesTree.querySelectorAll('.tree-file button')) {
    item.classList.toggle('active', item.dataset.path === path);
  }
  view.focus();
}

function renderTreeNode(node, container) {
  const list = document.createElement('ul');
  list.className = 'tree-list';

  for (const [name, child] of [...node.dirs.entries()].sort(([a], [b]) => sortNames(a, b))) {
    const item = document.createElement('li');
    const details = document.createElement('details');
    details.className = 'tree-dir';
    details.open = true;

    const summary = document.createElement('summary');
    summary.textContent = name;
    details.appendChild(summary);
    renderTreeNode(child, details);

    item.appendChild(details);
    list.appendChild(item);
  }

  for (const file of node.files.sort((a, b) => sortNames(a.name, b.name))) {
    const item = document.createElement('li');
    item.className = 'tree-file';
    if (EXAMPLES.has(file.path)) {
      const button = document.createElement('button');
      button.type = 'button';
      button.dataset.path = file.path;
      button.textContent = file.name;
      button.title = file.path;
      button.addEventListener('click', () => loadExample(file.path));
      item.appendChild(button);
    } else {
      const label = document.createElement('span');
      label.textContent = file.name;
      label.title = file.path;
      item.appendChild(label);
    }
    list.appendChild(item);
  }

  container.appendChild(list);
}

const exampleTreeData = buildTree(EXAMPLE_FILES.map(([path]) => path.replace('/examples/', '')));
renderTreeNode(exampleTreeData, examplesTree);
