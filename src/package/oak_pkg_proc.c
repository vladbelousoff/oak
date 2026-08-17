#include "oak_pkg_proc.h"

#include "internal/oak_pkg_util.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* Grow-on-demand byte buffer for captured output. */
typedef struct sink sink_t;
struct sink
{
  oak_allocator_t* a;
  char* data;
  usize len;
  usize cap;
  int failed;
};

static void sink_push(sink_t* s, const char* bytes, const usize n)
{
  if (s->failed || n == 0u)
    return;
  if (s->len + n + 1u > s->cap)
  {
    usize cap = s->cap ? s->cap : 256u;
    while (cap < s->len + n + 1u)
      cap *= 2u;
    char* grown = oak_realloc(s->a, s->data, cap, OAK_HERE);
    if (!grown)
    {
      s->failed = 1;
      return;
    }
    s->data = grown;
    s->cap = cap;
  }
  memcpy(s->data + s->len, bytes, n);
  s->len += n;
  s->data[s->len] = 0;
}

/* Trailing newlines are never wanted: every caller is reading a single value
 * like a commit hash out of a line-oriented command. */
static void sink_trim(sink_t* s)
{
  while (s->data && s->len != 0u &&
         (s->data[s->len - 1u] == '\n' || s->data[s->len - 1u] == '\r'))
    s->data[--s->len] = 0;
}

int oak_pkg_proc_quote_win32(const char* arg, char* buf, const usize cap)
{
  if (!arg || !buf)
    return -1;

  int needs_quotes = (arg[0] == 0);
  for (const char* p = arg; *p && !needs_quotes; ++p)
    if (*p == ' ' || *p == '\t' || *p == '"' || *p == '\n' || *p == '\v')
      needs_quotes = 1;

  usize n = 0;
#define PUT(c)                                                                 \
  do                                                                           \
  {                                                                            \
    if (n + 1u >= cap)                                                         \
      return -1;                                                               \
    buf[n++] = (c);                                                            \
  } while (0)

  if (!needs_quotes)
  {
    for (const char* p = arg; *p; ++p)
      PUT(*p);
    buf[n] = 0;
    return (int)n;
  }

  PUT('"');
  for (const char* p = arg;; ++p)
  {
    /* A run of backslashes is literal unless it precedes a quote -- the one
     * the argument contains, or the one that closes it -- in which case each
     * backslash must be doubled so the runtime does not read the pair as an
     * escaped quote. */
    usize slashes = 0;
    while (*p == '\\')
    {
      ++slashes;
      ++p;
    }
    if (*p == 0)
    {
      for (usize i = 0; i < slashes * 2u; ++i)
        PUT('\\');
      break;
    }
    if (*p == '"')
    {
      for (usize i = 0; i < slashes * 2u + 1u; ++i)
        PUT('\\');
      PUT('"');
      continue;
    }
    for (usize i = 0; i < slashes; ++i)
      PUT('\\');
    PUT(*p);
  }
  PUT('"');
#undef PUT
  buf[n] = 0;
  return (int)n;
}

void oak_pkg_proc_result_free(oak_allocator_t* a, oak_pkg_proc_result_t* r)
{
  if (!a || !r)
    return;
  oak_free(a, r->output, OAK_HERE);
  r->output = OAK_NULL;
}

#if defined(_WIN32)

static char* build_command_line(oak_allocator_t* a, const char* const* argv)
{
  usize cap = 1u;
  for (usize i = 0; argv[i]; ++i)
    cap += strlen(argv[i]) * 2u + 4u;

  char* line = oak_alloc(a, cap, OAK_HERE);
  if (!line)
    return OAK_NULL;

  usize n = 0;
  for (usize i = 0; argv[i]; ++i)
  {
    if (i != 0u)
      line[n++] = ' ';
    const int written = oak_pkg_proc_quote_win32(argv[i], line + n, cap - n);
    if (written < 0)
    {
      oak_free(a, line, OAK_HERE);
      return OAK_NULL;
    }
    n += (usize)written;
  }
  line[n] = 0;
  return line;
}

int oak_pkg_proc_run(oak_allocator_t* a,
                     const char* const* argv,
                     const char* cwd,
                     const int capture,
                     oak_pkg_proc_result_t* out,
                     char* err,
                     const usize err_cap)
{
  if (!a || !argv || !argv[0] || !out)
    return -1;
  memset(out, 0, sizeof *out);

  char* line = build_command_line(a, argv);
  if (!line)
    return oak_pkg_fail(err, err_cap, "cannot build a command line for '%s'",
                        argv[0]);

  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof sa);
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;

  HANDLE read_end = INVALID_HANDLE_VALUE;
  HANDLE write_end = INVALID_HANDLE_VALUE;
  if (capture && !CreatePipe(&read_end, &write_end, &sa, 0))
  {
    oak_free(a, line, OAK_HERE);
    return oak_pkg_fail(err, err_cap, "cannot create a pipe for '%s'", argv[0]);
  }
  /* The read end must not reach the child, or the pipe never reports EOF. */
  if (capture)
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si;
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  if (capture)
  {
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  }

  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof pi);

  const BOOL started = CreateProcessA(OAK_NULL, line, OAK_NULL, OAK_NULL,
                                      capture ? TRUE : FALSE, 0, OAK_NULL, cwd,
                                      &si, &pi);
  oak_free(a, line, OAK_HERE);
  if (capture)
    CloseHandle(write_end);

  if (!started)
  {
    if (capture)
      CloseHandle(read_end);
    return oak_pkg_fail(err, err_cap, "cannot run '%s' (is it installed?)",
                        argv[0]);
  }

  sink_t sink;
  memset(&sink, 0, sizeof sink);
  sink.a = a;
  if (capture)
  {
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(read_end, buf, sizeof buf, &got, OAK_NULL) && got != 0u)
      sink_push(&sink, buf, (usize)got);
    CloseHandle(read_end);
  }

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  if (sink.failed)
  {
    oak_free(a, sink.data, OAK_HERE);
    return oak_pkg_fail(err, err_cap, "out of memory reading '%s' output",
                        argv[0]);
  }
  sink_trim(&sink);
  out->exit_code = (int)code;
  out->output = sink.data;
  return 0;
}

#else

int oak_pkg_proc_run(oak_allocator_t* a,
                     const char* const* argv,
                     const char* cwd,
                     const int capture,
                     oak_pkg_proc_result_t* out,
                     char* err,
                     const usize err_cap)
{
  if (!a || !argv || !argv[0] || !out)
    return -1;
  memset(out, 0, sizeof *out);

  int fds[2] = { -1, -1 };
  if (capture && pipe(fds) != 0)
    return oak_pkg_fail(err, err_cap, "cannot create a pipe for '%s'", argv[0]);

  const pid_t pid = fork();
  if (pid < 0)
  {
    if (capture)
    {
      close(fds[0]);
      close(fds[1]);
    }
    return oak_pkg_fail(err, err_cap, "cannot fork to run '%s'", argv[0]);
  }

  if (pid == 0)
  {
    if (capture)
    {
      close(fds[0]);
      if (dup2(fds[1], STDOUT_FILENO) < 0)
        _exit(127);
      close(fds[1]);
    }
    if (cwd && chdir(cwd) != 0)
      _exit(127);
    /* execvp takes a mutable array only for historical reasons; it does not
     * write through it. */
    execvp(argv[0], (char* const*)(const void*)argv);
    _exit(127);
  }

  sink_t sink;
  memset(&sink, 0, sizeof sink);
  sink.a = a;
  if (capture)
  {
    close(fds[1]);
    char buf[4096];
    for (;;)
    {
      const isize got = read(fds[0], buf, sizeof buf);
      if (got > 0)
        sink_push(&sink, buf, (usize)got);
      else if (got == 0 || errno != EINTR)
        break;
    }
    close(fds[0]);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
    ;

  if (sink.failed)
  {
    oak_free(a, sink.data, OAK_HERE);
    return oak_pkg_fail(err, err_cap, "out of memory reading '%s' output",
                        argv[0]);
  }
  sink_trim(&sink);

  /* 127 is what the child reports when exec itself failed, which is a
   * different kind of problem from a command that ran and disagreed. */
  if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
  {
    oak_free(a, sink.data, OAK_HERE);
    return oak_pkg_fail(err, err_cap, "cannot run '%s' (is it installed?)",
                        argv[0]);
  }

  out->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  out->output = sink.data;
  return 0;
}

#endif
