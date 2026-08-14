#include "oak_test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#define oak_fileno _fileno
#define oak_fdopen _fdopen
#define oak_dup    _dup
#define oak_dup2   _dup2
#define oak_close  _close
#define oak_read   _read
#else
#include <fcntl.h>
#include <unistd.h>
#define oak_fileno fileno
#define oak_fdopen fdopen
#define oak_dup    dup
#define oak_dup2   dup2
#define oak_close  close
#define oak_read   read
#endif

/*
 * The tests capture output through anonymous pipes rather than scratch files,
 * so a test run touches no filesystem at all: nothing to name uniquely, nothing
 * to clean up, and nothing left behind when a run is killed.
 *
 * The one constraint a pipe brings is capacity. Nothing drains the pipe while
 * the program under test is writing to it -- the read happens afterwards -- so
 * a program that emitted more than the pipe holds would block forever. The
 * buffer is therefore requested large (1 MiB) against captures bounded at
 * OAK_TEST_OUTPUT_MAX, and on POSIX the write end is additionally marked
 * non-blocking so that overflowing it truncates the capture instead of hanging
 * the suite. A test that deliberately prints megabytes would need a reader
 * thread; none does, and one that did would be asserting on the wrong thing.
 */
#define OAK_TEST_PIPE_BYTES (1u << 20)

/* fds[0] read end, fds[1] write end. Returns 1 on success. */
static int oak_pipe_open(int fds[2])
{
#if defined(_WIN32)
  return _pipe(fds, OAK_TEST_PIPE_BYTES, _O_BINARY) == 0;
#else
  if (pipe(fds) != 0)
    return 0;
#if defined(F_SETPIPE_SZ)
  /* Best effort: the kernel caps this at /proc/sys/fs/pipe-max-size and the
   * 64 KiB default is already ample for these captures. */
  (void)fcntl(fds[1], F_SETPIPE_SZ, (int)OAK_TEST_PIPE_BYTES);
#endif
  (void)fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL, 0) | O_NONBLOCK);
  return 1;
#endif
}

/* Reads to EOF, which arrives once every write end is closed. */
static usize oak_pipe_drain(const int fd, char* out, const usize cap)
{
  usize n = 0;
  while (n + 1 < cap)
  {
    const int got = oak_read(fd, out + n, (unsigned)(cap - 1 - n));
    if (got <= 0)
      break;
    n += (usize)got;
  }
  out[n] = '\0';
  return n;
}

int oak_test_pipe_new(oak_test_pipe_t* p)
{
  int fds[2];

  p->read_end = null;
  p->write_end = null;

  if (!oak_pipe_open(fds))
    return 0;

  p->read_end = oak_fdopen(fds[0], "rb");
  p->write_end = oak_fdopen(fds[1], "wb");
  if (!p->read_end || !p->write_end)
  {
    oak_test_pipe_free(p);
    return 0;
  }
  return 1;
}

usize oak_test_pipe_read(oak_test_pipe_t* p, char* out, const usize cap)
{
  if (cap)
    out[0] = '\0';

  /* The reader only sees EOF once no write end is left open, so closing this
   * side first is what keeps the read below from blocking forever. */
  if (p->write_end)
  {
    fclose(p->write_end);
    p->write_end = null;
  }
  if (!p->read_end || !cap)
    return 0;

  return oak_pipe_drain(oak_fileno(p->read_end), out, cap);
}

void oak_test_pipe_free(oak_test_pipe_t* p)
{
  if (p->write_end)
    fclose(p->write_end);
  if (p->read_end)
    fclose(p->read_end);
  p->write_end = null;
  p->read_end = null;
}

/*
 * Everything print() emits goes to stdout, so asserting on program output means
 * pointing fd 1 at a pipe for the duration of the run and reading it back
 * afterwards. fd 2 is captured the same way, which picks up diagnostics from
 * every stage in the form a user actually sees them.
 *
 * A runtime error specifically no longer needs the pipe: oak_vm_last_error()
 * returns it as an oak_diagnostic_t. Prefer that when asserting on the message
 * or its source location; the capture stays because it also covers compile and
 * loader diagnostics, and because it verifies the text that reaches a terminal.
 */
void oak_capture_begin(oak_capture_t* c, FILE* stream)
{
  int fds[2];

  c->stream = stream;
  c->read_fd = -1;
  c->saved_fd = -1;

  if (!oak_pipe_open(fds))
    return;

  fflush(stream);
  c->saved_fd = oak_dup(oak_fileno(stream));
  if (c->saved_fd < 0 || oak_dup2(fds[1], oak_fileno(stream)) < 0)
  {
    if (c->saved_fd >= 0)
    {
      oak_close(c->saved_fd);
      c->saved_fd = -1;
    }
    oak_close(fds[0]);
    oak_close(fds[1]);
    return;
  }

  /* fd 1/2 is now a second handle on the write end, so drop this one: the
   * restore in oak_capture_end() then leaves no writer and the read sees EOF. */
  oak_close(fds[1]);
  c->read_fd = fds[0];
}

void oak_capture_end(oak_capture_t* c, char* out, const usize cap)
{
  if (cap)
    out[0] = '\0';
  if (c->read_fd < 0)
  {
    /* Redirection never got set up, so the program's output went to the real
     * stream and there is nothing to read back. Say so, rather than leaving an
     * empty buffer that reads as "the program printed nothing" and turns into
     * a misleading assertion failure somewhere else. */
    snprintf(out, cap, "%s", OAK_TEST_CAPTURE_FAILED);
    return;
  }

  fflush(c->stream);
  if (c->saved_fd >= 0)
  {
    oak_dup2(c->saved_fd, oak_fileno(c->stream));
    oak_close(c->saved_fd);
    c->saved_fd = -1;
  }

  oak_pipe_drain(c->read_fd, out, cap);
  oak_close(c->read_fd);
  c->read_fd = -1;
}

oak_run_result_t oak_test_source_opts(oak_allocator_t* a,
                                      const char* src,
                                      oak_compile_options_t* opts)
{
  oak_run_result_t r;
  oak_lexer_result_t* lexer;
  oak_parser_result_t* parsed = null;
  const oak_ast_node_t* root;
  oak_compile_result_t compiled = { 0 };

  memset(&r, 0, sizeof(r));
  r.run = OAK_VM_OK;

  lexer = oak_lexer_tokenize(src, a);
  parsed = oak_parse(lexer, OAK_NODE_PROGRAM, a);
  root = oak_parser_root(parsed);
  r.parsed = root != null;

  if (root)
  {
    oak_compile_ex(root, opts, &compiled);
    r.compiled = compiled.chunk != null;
    r.error_count = compiled.error_count;

    /* Join every diagnostic, not just the first. A single mistake often
     * produces a cascade (an unresolved name, then the void-typed expression
     * that depends on it), and which one lands at index 0 is an artifact of
     * pass ordering. Matching against all of them asks the question the test
     * actually cares about: was this rejected for the stated reason? */
    {
      int i;
      usize used = 0;
      for (i = 0; i < compiled.error_count; ++i)
      {
        const usize space = sizeof(r.diag) - used;
        const int written = snprintf(r.diag + used,
                                     space,
                                     "%s%s",
                                     used ? "\n" : "",
                                     compiled.errors[i].message);
        if (written < 0)
          break;
        /* snprintf reports what it *would* have written. Letting that run past
         * the buffer would underflow `space` on the next pass, so stop here and
         * say the text is incomplete -- otherwise a test that matched against a
         * dropped diagnostic reports "wrong diagnostic" for a message the
         * compiler really did emit. */
        if ((usize)written >= space)
        {
          const char* const mark = "\n[diagnostics truncated]";
          const usize mark_len = strlen(mark);
          if (sizeof(r.diag) > mark_len)
            memcpy(r.diag + sizeof(r.diag) - mark_len - 1, mark, mark_len + 1);
          break;
        }
        used += (usize)written;
      }
    }
  }

  if (compiled.chunk)
  {
    oak_capture_t cap_out;
    oak_capture_t cap_err;
    oak_vm_t vm;

    oak_capture_begin(&cap_out, stdout);
    oak_capture_begin(&cap_err, stderr);
    oak_vm_init(&vm, a);
    if (opts && opts->module_registry)
      oak_vm_set_module_registry(&vm, opts->module_registry);
    r.run = oak_vm_run(&vm, compiled.chunk);
    oak_vm_free(&vm);
    oak_capture_end(&cap_err, r.err, sizeof(r.err));
    oak_capture_end(&cap_out, r.out, sizeof(r.out));
  }

  oak_compile_result_free(&compiled);
  oak_parser_free(parsed);
  oak_lexer_free(lexer);
  return r;
}

oak_run_result_t oak_test_source(oak_allocator_t* a, const char* src)
{
  oak_compile_options_t opts;
  oak_run_result_t r;

  oak_compile_options_init(&opts, a);
  r = oak_test_source_opts(a, src, &opts);
  oak_compile_options_free(&opts);
  return r;
}

const oak_ast_node_t* oak_test_lhs(const oak_ast_node_t* node)
{
  return node ? node->lhs : null;
}

const oak_ast_node_t* oak_test_rhs(const oak_ast_node_t* node)
{
  return node ? node->rhs : null;
}

oak_parse_fixture_t oak_test_parse(oak_allocator_t* a, const char* src)
{
  oak_parse_fixture_t fx;
  memset(&fx, 0, sizeof(fx));
  fx.lexer = oak_lexer_tokenize(src, a);
  fx.parsed = oak_parse(fx.lexer, OAK_NODE_PROGRAM, a);
  fx.root = oak_parser_root(fx.parsed);
  return fx;
}

void oak_test_parse_free(oak_parse_fixture_t* fx)
{
  oak_parser_free(fx->parsed);
  fx->parsed = null;
  oak_lexer_free(fx->lexer);
  fx->lexer = null;
  fx->root = null;
}

int oak_test_contains(const char* haystack, const char* needle)
{
  if (!needle || !needle[0])
    return 1;
  if (!haystack)
    return 0;
  return strstr(haystack, needle) != null;
}

/* Returns 0 and fills `msg` on the first field that differs. */
static int token_matches(const oak_token_t* tok,
                         const oak_expect_token_t* want,
                         const usize index,
                         char* msg,
                         const usize cap)
{
  const oak_token_kind_t kind = oak_token_kind(tok);

  if (kind != want->kind)
  {
    snprintf(msg,
             cap,
             "token[%u]: kind %s, want %s",
             (unsigned)index,
             oak_token_name(kind),
             oak_token_name(want->kind));
    return 0;
  }

#define OAK_TOKEN_FIELD(getter, field, label)                                  \
  if (getter(tok) != want->field)                                              \
  {                                                                            \
    snprintf(msg,                                                              \
             cap,                                                              \
             "token[%u] (%s): " label " %d, want %d",                          \
             (unsigned)index,                                                  \
             oak_token_name(kind),                                             \
             getter(tok),                                                      \
             want->field);                                                     \
    return 0;                                                                  \
  }

  OAK_TOKEN_FIELD(oak_token_line, line, "line")
  OAK_TOKEN_FIELD(oak_token_column, column, "column")
  OAK_TOKEN_FIELD(oak_token_offset, offset, "offset")
#undef OAK_TOKEN_FIELD

  if (kind == OAK_TOKEN_INT && oak_token_as_i32(tok) != want->integer)
  {
    snprintf(msg,
             cap,
             "token[%u]: int %d, want %d",
             (unsigned)index,
             oak_token_as_i32(tok),
             want->integer);
    return 0;
  }

  if (kind == OAK_TOKEN_FLOAT)
  {
    const float got = oak_token_as_f32(tok);
    const float diff = got > want->floating ? got - want->floating
                                            : want->floating - got;
    if (diff > 0.0001f)
    {
      snprintf(msg,
               cap,
               "token[%u]: float %f, want %f",
               (unsigned)index,
               (double)got,
               (double)want->floating);
      return 0;
    }
  }

  if (want->text && (kind == OAK_TOKEN_STRING || kind == OAK_TOKEN_IDENT) &&
      strcmp(oak_token_text(tok), want->text) != 0)
  {
    snprintf(msg,
             cap,
             "token[%u]: text '%s', want '%s'",
             (unsigned)index,
             oak_token_text(tok),
             want->text);
    return 0;
  }

  return 1;
}

int oak_test_tokens_match(const oak_lexer_result_t* lexer,
                          const oak_expect_token_t* expected,
                          const usize count,
                          char* msg,
                          const usize msg_cap)
{
  usize index = 0;
  oak_list_entry_t* entry;

  msg[0] = '\0';

  oak_list_for_each_indexed(index, entry, oak_lexer_tokens(lexer))
  {
    const oak_token_t* tok = oak_container_of(entry, oak_token_t, link);

    if (index >= count)
    {
      snprintf(msg,
               msg_cap,
               "unexpected extra token[%u] (%s); expected exactly %u",
               (unsigned)index,
               oak_token_name(oak_token_kind(tok)),
               (unsigned)count);
      return 0;
    }

    if (!token_matches(tok, &expected[index], index, msg, msg_cap))
      return 0;
  }

  if (index < count)
  {
    snprintf(msg,
             msg_cap,
             "token stream ended at %u; expected %u tokens",
             (unsigned)index,
             (unsigned)count);
    return 0;
  }

  return 1;
}

/* Advance past a CR so CRLF and LF compare equal, then skip trailing blanks. */
int oak_test_output_equals(const char* got, const char* want)
{
  usize gi = 0;
  usize wi = 0;

  if (!want)
    return 1;
  if (!got)
    return 0;

  for (;;)
  {
    while (got[gi] == '\r')
      gi++;
    while (want[wi] == '\r')
      wi++;

    /* Trailing whitespace on either side is not significant. */
    if (got[gi] == '\0' || want[wi] == '\0')
    {
      while (got[gi] == '\n' || got[gi] == ' ' || got[gi] == '\t' ||
             got[gi] == '\r')
        gi++;
      while (want[wi] == '\n' || want[wi] == ' ' || want[wi] == '\t' ||
             want[wi] == '\r')
        wi++;
      return got[gi] == '\0' && want[wi] == '\0';
    }

    if (got[gi] != want[wi])
      return 0;
    gi++;
    wi++;
  }
}

void oak_test_explain(const oak_run_result_t* r, const char* src)
{
  printf("    source: %s\n", src ? src : "(null)");
  if (!r->parsed)
  {
    printf("    stage:  parse failed\n");
    return;
  }
  if (!r->compiled)
  {
    printf("    stage:  compile failed (%d diagnostic%s)\n",
           r->error_count,
           r->error_count == 1 ? "" : "s");
    printf("    diag:   %s\n", r->diag[0] ? r->diag : "(none)");
    return;
  }
  printf("    stage:  ran, vm result %d\n", (int)r->run);
  if (r->out[0])
    printf("    stdout: %s\n", r->out);
  if (r->err[0])
    printf("    stderr: %s\n", r->err);
  fflush(stdout);
}
