/*
 * Debugger: the interactive command loop.
 *
 * Each test compiles a snippet with debug info, attaches the debugger, feeds
 * it a canned command script, and asserts on what the debugger printed --
 * stop locations, locals, error messages. Checking the final VM result alone
 * would not distinguish "stepped correctly" from "ran straight through".
 *
 * The debugger's streams are injected through dbg.in/dbg.out rather than by
 * reassigning stdin/stdout, which are not l-values under MSVC.
 */

#include "oak_test_support.h"

#include "oak_debugger.h"

#include <stdio.h>
#include <string.h>

OAK_TEST_SUITE(debugger);

#define DBG_OUT_MAX 8192

typedef struct dbg_session dbg_session_t;
struct dbg_session
{
  oak_vm_result_t result;
  char output[DBG_OUT_MAX];
};

static oak_compile_result_t compile_debug(oak_allocator_t* a, const char* src)
{
  oak_lexer_result_t* lex = oak_lexer_tokenize(src, a);
  oak_parser_result_t* pr;
  oak_compile_options_t opts;
  oak_compile_result_t cr = { 0 };

  pr = oak_parse(lex, OAK_NODE_PROGRAM, a);

  oak_compile_options_init(&opts, a);
  opts.emit_debug_info = 1;
  opts.source_name = "test.oak";
  oak_compile_ex(oak_parser_root(pr), &opts, &cr);

  oak_compile_options_free(&opts);
  oak_parser_free(pr);
  oak_lexer_free(lex);
  return cr;
}

/* Runs one debug session. bp_line == 0 means no breakpoint. */
static dbg_session_t run_session(oak_allocator_t* a,
                                 const char* src,
                                 const char* commands,
                                 const int bp_line)
{
  dbg_session_t s;
  oak_compile_result_t cr;
  oak_debugger_t dbg;
  oak_vm_t vm;
  oak_vm_debug_hook_t hook;
  oak_test_pipe_t in_pipe;
  oak_test_pipe_t out_pipe;

  memset(&s, 0, sizeof(s));
  cr = compile_debug(a, src);
  if (!cr.chunk)
  {
    s.result = OAK_VM_RUNTIME_ERROR;
    return s;
  }

  oak_debugger_init(&dbg, a);
  if (bp_line)
    oak_debugger_add_breakpoint(&dbg, bp_line, "test.oak");

  oak_vm_init(&vm, a);
  hook.fn = oak_debugger_hook;
  hook.ctx = &dbg;
  oak_vm_set_debug_hook(&vm, &hook);

  /* Both are constructed unconditionally: `||` would skip the second on a
   * first failure and leave it an indeterminate struct for the free below to
   * fclose(). */
  {
    const int in_ok = oak_test_pipe_new(&in_pipe);
    const int out_ok = oak_test_pipe_new(&out_pipe);
    if (!in_ok || !out_ok)
    {
      /* No pipe: report a halt so the assertions fail loudly rather than the
       * session silently doing nothing. */
      oak_test_pipe_free(&in_pipe);
      oak_test_pipe_free(&out_pipe);
      oak_debugger_free(&dbg);
      oak_vm_free(&vm);
      oak_compile_result_free(&cr);
      s.result = OAK_VM_RUNTIME_ERROR;
      return s;
    }
  }

  /* The whole command script is queued up front and the write end closed, so
   * the debugger reads it back to a real EOF -- that EOF is what ends the
   * session when a script runs out of commands before the program does. */
  fwrite(commands, 1, strlen(commands), in_pipe.write_end);
  fclose(in_pipe.write_end);
  in_pipe.write_end = OAK_NULL;

  dbg.in = in_pipe.read_end;
  dbg.out = out_pipe.write_end;

  s.result = oak_vm_run(&vm, cr.chunk);

  /* Detach before the streams close: the debugger must not outlive them. */
  dbg.in = stdin;
  dbg.out = stdout;
  oak_test_pipe_read(&out_pipe, s.output, sizeof(s.output));

  oak_test_pipe_free(&in_pipe);
  oak_test_pipe_free(&out_pipe);
  oak_debugger_free(&dbg);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
  return s;
}

/* Reports the whole session transcript on a miss -- debugger output is the
 * only evidence of what actually happened. */
#define EXPECT_SAW(session, needle)                                            \
  do                                                                           \
  {                                                                            \
    if (!oak_test_contains((session).output, (needle)))                        \
    {                                                                          \
      UTEST_PRINTF("  expected to see '%s' in:\n%s\n",                         \
                   (needle),                                                   \
                   (session).output);                                          \
      *utest_result = UTEST_TEST_FAILURE;                                      \
    }                                                                          \
  } while (0)

#define EXPECT_NOT_SAW(session, needle)                                        \
  do                                                                           \
  {                                                                            \
    if (oak_test_contains((session).output, (needle)))                         \
    {                                                                          \
      UTEST_PRINTF("  did not expect '%s' in:\n%s\n",                          \
                   (needle),                                                   \
                   (session).output);                                          \
      *utest_result = UTEST_TEST_FAILURE;                                      \
    }                                                                          \
  } while (0)

/*
 * A program with a call, so step/next/finish have somewhere to go:
 *   1: fn f(a : number) -> number {
 *   2:   let b = a + 1;
 *   3:   return b;
 *   4: }
 *   5: let x = f(41);
 *   6: let y = x + 1;
 * The initial stop lands on line 5, the first top-level statement.
 */
static const char* const FN_SRC =
    "fn f(a : number) -> number {\n"
    "  let b = a + 1;\n"
    "  return b;\n"
    "}\n"
    "let x = f(41);\n"
    "let y = x + 1;\n";

static const char* const FLAT_SRC =
    "let a = 10;\nlet b = 20;\nlet c = 30;\n";

/* `quit` stops the program rather than letting it finish. */
UTEST_F(debugger, quit_halts_the_program)
{
  const dbg_session_t s = run_session(OAK_A, FLAT_SRC, "quit\n", 0);

  OAK_EXPECT_ENUM(OAK_VM_DEBUG_HALT, s.result);
  EXPECT_SAW(s, "stopped at test.oak:1");
}

UTEST_F(debugger, continue_runs_to_completion)
{
  const dbg_session_t s = run_session(OAK_A, FLAT_SRC, "continue\n", 0);

  OAK_EXPECT_ENUM(OAK_VM_OK, s.result);
  EXPECT_SAW(s, "stopped at test.oak:1");
}

UTEST_F(debugger, a_breakpoint_stops_the_second_time)
{
  const dbg_session_t s =
      run_session(OAK_A, FLAT_SRC, "continue\ncontinue\n", 2);

  OAK_EXPECT_ENUM(OAK_VM_OK, s.result);
  EXPECT_SAW(s, "stopped at test.oak:1"); /* initial stop */
  EXPECT_SAW(s, "stopped at test.oak:2"); /* breakpoint */
}

/* Broken inside f(), both the parameter and the local are live. */
UTEST_F(debugger, locals_lists_parameters_and_locals)
{
  const dbg_session_t s =
      run_session(OAK_A, FN_SRC, "continue\nlocals\ncontinue\n", 3);

  OAK_EXPECT_ENUM(OAK_VM_OK, s.result);
  EXPECT_SAW(s, "stopped at test.oak:3");
  EXPECT_SAW(s, "a = 41");
  EXPECT_SAW(s, "b = 42");
}

UTEST_F(debugger, print_reports_a_local_or_says_it_is_absent)
{
  const dbg_session_t s = run_session(
      OAK_A, FN_SRC, "continue\nprint b\nprint missing\ncontinue\n", 3);

  OAK_EXPECT_ENUM(OAK_VM_OK, s.result);
  EXPECT_SAW(s, "b = 42");
  EXPECT_SAW(s, "no local named 'missing' in scope");
}

/* From line 5, `next` runs f() to completion and lands on line 6 -- it must
 * not stop anywhere inside the callee. */
UTEST_F(debugger, next_steps_over_a_call)
{
  const dbg_session_t s = run_session(OAK_A, FN_SRC, "next\nquit\n", 0);

  EXPECT_SAW(s, "stopped at test.oak:5");
  EXPECT_SAW(s, "stopped at test.oak:6");
  EXPECT_NOT_SAW(s, "stopped at test.oak:2");
}

/* `step` from the same place descends into f(). */
UTEST_F(debugger, step_enters_a_call)
{
  const dbg_session_t s = run_session(OAK_A, FN_SRC, "step\nquit\n", 0);

  EXPECT_SAW(s, "stopped at test.oak:5");
  EXPECT_SAW(s, "stopped at test.oak:2");
}

/* `finish` runs out of the current frame and stops back in the caller. */
UTEST_F(debugger, finish_returns_to_the_caller)
{
  const dbg_session_t s =
      run_session(OAK_A, FN_SRC, "continue\nfinish\nquit\n", 2);

  EXPECT_SAW(s, "stopped at test.oak:2");
  EXPECT_SAW(s, "stopped at test.oak:6");
}

/* Numeric arguments are parsed with strtol, not atoi: "12xyz" must be
 * rejected outright rather than silently read as 12. */
UTEST_F(debugger, malformed_break_arguments_create_no_breakpoint)
{
  const dbg_session_t s =
      run_session(OAK_A, FLAT_SRC, "break 12xyz\nbreak 0\ncontinue\n", 0);

  OAK_EXPECT_ENUM(OAK_VM_OK, s.result);
  EXPECT_SAW(s, "usage: break <line>");
  EXPECT_NOT_SAW(s, "breakpoint #");
}

UTEST_F(debugger, malformed_delete_arguments_are_reported)
{
  const dbg_session_t s =
      run_session(OAK_A, FLAT_SRC, "delete abc\ndelete 99\ncontinue\n", 0);

  OAK_EXPECT_ENUM(OAK_VM_OK, s.result);
  EXPECT_SAW(s, "usage: delete <id>");
  EXPECT_SAW(s, "no breakpoint #99");
}

/* Debug info on the chunk must not change execution when no hook is set --
 * the debugger is entirely opt-in at run time. */
UTEST_F(debugger, a_chunk_with_debug_info_runs_normally_without_a_hook)
{
  oak_compile_result_t cr = compile_debug(OAK_A, "let x = 1; let y = 2;");
  oak_vm_t vm;

  ASSERT_TRUE(cr.chunk != OAK_NULL);

  oak_vm_init(&vm, OAK_A);
  OAK_EXPECT_ENUM(OAK_VM_OK, oak_vm_run(&vm, cr.chunk));
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
}
