import { StreamLanguage } from '@codemirror/language';

const KEYWORDS = new Set([
  'let', 'mut', 'fn', 'return', 'if', 'else', 'for', 'while',
  'break', 'continue', 'record', 'import', 'new', 'from', 'to', 'in', 'as',
  'self', 'trait',
]);
const TYPES    = new Set(['number', 'string', 'bool']);
const BUILTINS = new Set(['print']);

export const oak = StreamLanguage.define({
  token(stream) {
    if (stream.match('//')) { stream.skipToEnd(); return 'comment'; }

    if (stream.eat("'")) {
      while (!stream.eol()) {
        if (stream.eat('\\')) { stream.next(); continue; }
        if (stream.eat("'")) break;
        stream.next();
      }
      return 'string';
    }

    if (stream.match(/^[0-9]+(\.[0-9]+)?/)) return 'number';

    if (stream.match(/^[a-zA-Z_]\w*/)) {
      const w = stream.current();
      if (KEYWORDS.has(w)) return 'keyword';
      if (TYPES.has(w))    return 'type';
      if (BUILTINS.has(w)) return 'builtin';
      if (w === 'true' || w === 'false') return 'atom';
      return null;
    }

    stream.next();
    return null;
  },
});
