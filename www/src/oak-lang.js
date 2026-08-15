import { StreamLanguage } from '@codemirror/language';

const KEYWORDS = new Set([
  'let', 'mut', 'fn', 'return', 'if', 'else', 'for', 'while',
  'break', 'continue', 'record', 'enum', 'import', 'new', 'from', 'to', 'in', 'as',
  'self', 'static', 'interface', 'implements', 'weak',
]);
const TYPES    = new Set(['number', 'string', 'bool']);
const BUILTINS = new Set(['print']);

export const oak = StreamLanguage.define({
  startState() {
    return { inBlockComment: false };
  },

  token(stream, state) {
    if (state.inBlockComment) {
      while (!stream.eol()) {
        if (stream.match('*/')) {
          state.inBlockComment = false;
          break;
        }
        stream.next();
      }
      return 'comment';
    }

    if (stream.match('/*')) {
      state.inBlockComment = true;
      while (!stream.eol()) {
        if (stream.match('*/')) {
          state.inBlockComment = false;
          break;
        }
        stream.next();
      }
      return 'comment';
    }

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
      if (w === 'true' || w === 'false' || w === 'none') return 'atom';
      return null;
    }

    stream.next();
    return null;
  },
});
