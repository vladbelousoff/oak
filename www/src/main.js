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

// ── WASM ──────────────────────────────────────────────────────────────────────
let oakRun   = null;
let captured = [];

function appendOutput(text, isErr) {
  const line = document.createElement('span');
  line.className = isErr ? 'err-line' : 'out-line';
  line.textContent = text + '\n';
  output.appendChild(line);
  output.scrollTop = output.scrollHeight;
}

function run() {
  if (!oakRun) return;
  output.innerHTML = '';
  captured = [];
  const code = view.state.doc.toString();
  let exitCode;
  try {
    exitCode = oakRun(code);
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
  print:    (text) => { if (oakRun) captured.push({ text, err: false }); },
  printErr: (text) => { if (oakRun) captured.push({ text, err: true }); },
}).then(module => {
  oakRun = module.cwrap('oak_run_wrapper', 'number', ['string']);
  runBtn.disabled = false;
  status.textContent = 'Ready';
}).catch(err => {
  status.textContent = 'Failed to load';
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
const rawExamples = import.meta.glob('../examples/*.oak', { eager: true, query: '?raw', import: 'default' });

const EXAMPLES = Object.fromEntries(
  Object.entries(rawExamples).map(([path, src]) => {
    const key = path.replace(/^.*\/([^/]+)\.oak$/, '$1');
    return [key, src.trimEnd()];
  })
);

const examplesSelect = document.getElementById('examples-select');
examplesSelect.addEventListener('change', () => {
  const key = examplesSelect.value;
  if (key && EXAMPLES[key]) {
    view.dispatch({
      changes: { from: 0, to: view.state.doc.length, insert: EXAMPLES[key] },
    });
    view.focus();
  }
  examplesSelect.value = '';
});
