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
const EXAMPLES = {

  hello:
`print('Hello, World!');
print(42);
print(3.14);
print(true);
print(false);`,

  variables:
`let x = 42;
let pi = 3.14;
let flag = true;

let mut n = 10;
n = 20;
n += 5;
n -= 3;
n *= 2;
n /= 4;
n %= 7;
print(n);

print(x > 0 && flag);
print(x == 0 || n != 0);
print(!false);
print(-x + 100);`,

  strings:
`let greeting = 'Hello';
let name = 'Oak';
print(greeting + ', ' + name + '!');

let escaped = 'line one\nline two';
print(escaped);

let vals = [3, 4, 7];
print('{0} + {1} = {2}'.format(vals));

let parts = ['foo', 'bar', 'baz'];
print('{}{}{}'.format(parts));

print('{{literal braces}}'.format([] as string[]));

let word = 'hello';
print(word.size());`,

  control_flow:
`let x = 7;

if x > 10 {
  print('big');
} else {
  if x > 4 {
    print('medium');
  } else {
    print('small');
  }
}

for i from 0 to 5 {
  print(i);
}

let mut sum = 0;
for i from 1 to 101 {
  sum += i;
}
print(sum);

let mut i = 0;
while i < 5 {
  print(i);
  i += 1;
}`,

  break_continue:
`for i from 0 to 10 {
  if i == 5 {
    break;
  }
  print(i);
}

let mut odds = 0;
for i from 0 to 20 {
  if i % 2 == 0 {
    continue;
  }
  odds += 1;
}
print(odds);

let mut n = 0;
while true {
  if n == 7 {
    break;
  }
  n += 1;
}
print(n);`,

  functions:
`fn add(a : number, b : number) -> number {
  return a + b;
}

fn max(a : number, b : number) -> number {
  if a > b {
    return a;
  }
  return b;
}

fn greet(name : string) -> string {
  return 'Hello, ' + name + '!';
}

fn abs(x : number) -> number {
  if x < 0 {
    return -x;
  }
  return x;
}

print(add(3, 4));
print(max(10, 20));
print(greet('Oak'));
print(abs(-42));`,

  recursion:
`fn fact(n : number) -> number {
  if n <= 1 {
    return 1;
  }
  return n * fact(n - 1);
}

fn power(base : number, exp : number) -> number {
  if exp == 0 {
    return 1;
  }
  return base * power(base, exp - 1);
}

for i from 0 to 8 {
  print(fact(i));
}

print(power(2, 10));`,

  mutual_recursion:
`fn is_even(n : number) -> number {
  if n == 0 {
    return 1;
  }
  return is_odd(n - 1);
}

fn is_odd(n : number) -> number {
  if n == 0 {
    return 0;
  }
  return is_even(n - 1);
}

for i from 0 to 8 {
  print(is_even(i));
}`,

  arrays:
`let mut nums = [] as number[];
nums.push(10);
nums.push(20);
nums.push(30);
nums.push(40);

print(nums.size());

nums[0] = 99;
print(nums[0]);
print(nums[nums.size() - 1]);

let mut total = 0;
for v in nums {
  total += v;
}
print(total);

for i, v in nums {
  print(i);
  print(v);
}

let words = ['oak', 'is', 'fun'];
for w in words {
  print(w);
}`,

  maps:
`let mut scores = [:] as [string:number];
scores['alice'] = 95;
scores['bob']   = 87;
scores['carol'] = 92;

print(scores.size());
print(scores['alice']);

print(scores.has('bob'));
print(scores.has('dave'));

scores.delete('bob');
print(scores.size());

let mut total = 0;
for k, v in scores {
  print(k);
  total += v;
}
print(total);

let by_id = [1: 'alpha', 2: 'beta', 3: 'gamma'];
print(by_id[2]);
print(by_id.size());`,

  records:
`record Point {
  x : number;
  y : number;

  fn dist_sq(self, other : Point) -> number {
    let dx = self.x - other.x;
    let dy = self.y - other.y;
    return dx * dx + dy * dy;
  }

  fn translate(mut self, dx : number, dy : number) {
    self.x = self.x + dx;
    self.y = self.y + dy;
  }

  fn origin() -> Point {
    return new Point { x : 0, y : 0 };
  }
}

record Person {
  name : string;
  age  : number;

  fn greet(self) -> string {
    return 'Hi, I am ' + self.name;
  }
}

let a = new Point { x : 3, y : 4 };
let b = Point.origin();
print(a.dist_sq(b));

let mut p = new Point { x : 1, y : 1 };
p.translate(2, 3);
print(p.x);
print(p.y);

let alice = new Person { name : 'alice', age : 30 };
print(alice.greet());
print(alice.age);`,

  fizzbuzz:
`for i from 1 to 31 {
  if i % 15 == 0 {
    print('fizzbuzz');
  } else {
    if i % 3 == 0 {
      print('fizz');
    } else {
      if i % 5 == 0 {
        print('buzz');
      } else {
        print(i);
      }
    }
  }
}`,

  fibonacci:
`let mut a = 0;
let mut b = 1;

for i from 0 to 20 {
  print(a);
  let mut tmp = a + b;
  a = b;
  b = tmp;
}`,

  primes:
`for n from 2 to 50 {
  let mut is_prime = 1;
  for i from 2 to n {
    if n % i == 0 {
      is_prime = 0;
    }
  }
  if is_prime == 1 {
    print(n);
  }
}`,

  collatz:
`let mut n = 27;
let mut steps = 0;

while n != 1 {
  print(n);
  if n % 2 == 0 {
    n /= 2;
  } else {
    n *= 3;
    n += 1;
  }
  steps += 1;
}

print(n);
print(steps);`,

  bubble_sort:
`fn swap(mut arr: number[], i: number, j: number) {
  let tmp = arr[i];
  arr[i] = arr[j];
  arr[j] = tmp;
}

fn bubble_sort(mut arr: number[]) -> number {
  let n = arr.size();
  let mut i = 0;
  while i < n {
    let mut j = 0;
    while j < n - i - 1 {
      if arr[j] > arr[j + 1] {
        swap(arr, j, j + 1);
      }
      j += 1;
    }
    i += 1;
  }
  return n;
}

let mut data = [5, 2, 9, 1, 7, 3, 8, 4, 6, 0];
bubble_sort(data);
for v in data {
  print(v);
}`,

};

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
