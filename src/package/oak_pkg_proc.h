#pragma once

/*
 * Running a child process, for the git driver.
 *
 * Oak shells out to `git` rather than linking a git library, and the argument
 * vector never becomes a string the shell will look at: a tag or URL from a
 * manifest is data, and the one way to guarantee it stays data is to hand the
 * OS an argv it does not re-parse.  On Windows, where the OS takes a single
 * command line, this file does the quoting itself against the same rules the C
 * runtime uses to take it apart again.
 */

#include "oak_allocator.h"
#include "oak_types.h"

typedef struct oak_pkg_proc_result oak_pkg_proc_result_t;
struct oak_pkg_proc_result
{
  /* The process's exit status, or -1 when it was killed by a signal. */
  int exit_code;
  /* Captured stdout with trailing newlines removed, or null.  Owned by the
   * caller; only set when `capture` was requested and the spawn succeeded. */
  char* output;
};

/* Run `argv` (NULL-terminated, argv[0] is the program) in `cwd`, or the current
 * directory when `cwd` is null.
 *
 * With `capture` non-zero, stdout is read into `out->output` and stderr is left
 * attached to this process, so git's progress and error text still reaches the
 * user while the value being asked for is captured.  Returns 0 when the child
 * ran -- check `out->exit_code` for what it decided -- or -1 when it could not
 * be started, with a reason in `err`. */
int oak_pkg_proc_run(oak_allocator_t* a,
                     const char* const* argv,
                     const char* cwd,
                     int capture,
                     oak_pkg_proc_result_t* out,
                     char* err,
                     usize err_cap);

void oak_pkg_proc_result_free(oak_allocator_t* a, oak_pkg_proc_result_t* r);

/* Quote one argument the way the Windows C runtime expects to find it, for the
 * command line CreateProcess takes.  Exposed for testing: getting this wrong is
 * how a path with a space becomes two arguments.  Appends to `buf`, returning
 * the length used, or -1 if it does not fit.  Present on every platform so the
 * test suite covers it everywhere. */
int oak_pkg_proc_quote_win32(const char* arg, char* buf, usize cap);
