#include "oak_bind.h"
#include "oak_compiler.h"
#include "oak_debugger.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <stdio.h>
#include <string.h>

/* ── session driver ────────────────────────────────────────────────────
 * Each test compiles a snippet with debug info, attaches the debugger,
 * feeds a canned command script on stdin, and captures the debugger's
 * stdout so assertions can check the observed output (stop lines, locals,
 * usage messages) rather than only the final VM result. */

#define DBG_OUT_MAX 8192

struct dbg_session_t
{
  enum oak_vm_result_t result;
  char output[DBG_OUT_MAX];
};

static struct oak_compile_result_t compile_debug(const char* source)
{
  struct oak_allocator_t* a = oak_test_allocator();
  struct oak_lexer_result_t* lex =
      oak_lexer_tokenize(source, strlen(source), a);
  struct oak_parser_result_t pr = { 0 };
  oak_parse(lex, OAK_NODE_PROGRAM, &pr, a);
  const struct oak_ast_node_t* root = oak_parser_root(&pr);

  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, a);
  opts.emit_debug_info = 1;
  opts.source_name = "test.oak";

  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, &opts, &cr);

  oak_compile_options_free(&opts);
  oak_parser_free(&pr);
  oak_lexer_free(lex);
  return cr;
}

/* Run a debug session. bp_line == 0 means no breakpoint. */
static struct dbg_session_t run_session(const char* source,
                                        const char* commands,
                                        int bp_line)
{
  struct dbg_session_t s = { 0 };
  struct oak_allocator_t* a = oak_test_allocator();

  struct oak_compile_result_t cr = compile_debug(source);
  if (!cr.chunk)
  {
    s.result = OAK_VM_RUNTIME_ERROR;
    return s;
  }

  struct oak_debugger_t dbg;
  oak_debugger_init(&dbg, a);
  if (bp_line)
    oak_debugger_add_breakpoint(&dbg, bp_line, "test.oak");

  struct oak_vm_t vm;
  oak_vm_init(&vm, a);
  struct oak_vm_debug_hook_t hook;
  hook.fn = oak_debugger_hook;
  hook.ctx = &dbg;
  oak_vm_set_debug_hook(&vm, &hook);

  FILE* fake_in = tmpfile();
  fwrite(commands, 1, strlen(commands), fake_in);
  rewind(fake_in);
  FILE* fake_out = tmpfile();

  FILE* saved_in = stdin;
  FILE* saved_out = stdout;
  stdin = fake_in;
  stdout = fake_out;

  s.result = oak_vm_run(&vm, cr.chunk);

  fflush(fake_out);
  rewind(fake_out);
  const usize n = fread(s.output, 1, sizeof(s.output) - 1, fake_out);
  s.output[n] = '\0';

  stdin = saved_in;
  stdout = saved_out;
  fclose(fake_in);
  fclose(fake_out);

  oak_debugger_free(&dbg);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
  return s;
}

static int has(const struct dbg_session_t* s, const char* needle)
{
  return strstr(s->output, needle) != null;
}

/* Program with a function call so step/next/finish can be exercised.
 *   1: fn f(a : number) -> number {
 *   2:   let b = a + 1;
 *   3:   return b;
 *   4: }
 *   5: let x = f(41);
 *   6: let y = x + 1;
 * Initial stop lands on line 5 (first top-level statement). */
static const char* FN_SRC =
    "fn f(a : number) -> number {\n"
    "  let b = a + 1;\n"
    "  return b;\n"
    "}\n"
    "let x = f(41);\n"
    "let y = x + 1;\n";

static const char* FLAT_SRC = "let a = 10;\nlet b = 20;\nlet c = 30;\n";

/* ── tests ─────────────────────────────────────────────────────────── */

OAK_TEST_DECL(DebuggerQuitImmediately)
{
  struct dbg_session_t s = run_session(FLAT_SRC, "quit\n", 0);
  OAK_CHECK(s.result == OAK_VM_DEBUG_HALT);
  OAK_CHECK(has(&s, "stopped at test.oak:1"));
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerContinueToEnd)
{
  struct dbg_session_t s = run_session(FLAT_SRC, "continue\n", 0);
  OAK_CHECK(s.result == OAK_VM_OK);
  OAK_CHECK(has(&s, "stopped at test.oak:1"));
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerBreakpointHit)
{
  struct dbg_session_t s = run_session(FLAT_SRC, "continue\ncontinue\n", 2);
  OAK_CHECK(s.result == OAK_VM_OK);
  OAK_CHECK(has(&s, "stopped at test.oak:1")); /* initial stop */
  OAK_CHECK(has(&s, "stopped at test.oak:2")); /* breakpoint */
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerLocalsCommand)
{
  /* Break inside f() where both the parameter and a local are live. */
  struct dbg_session_t s =
      run_session(FN_SRC, "continue\nlocals\ncontinue\n", 3);
  OAK_CHECK(s.result == OAK_VM_OK);
  OAK_CHECK(has(&s, "stopped at test.oak:3"));
  OAK_CHECK(has(&s, "a = 41"));
  OAK_CHECK(has(&s, "b = 42"));
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerPrintCommand)
{
  struct dbg_session_t s =
      run_session(FN_SRC, "continue\nprint b\nprint missing\ncontinue\n", 3);
  OAK_CHECK(s.result == OAK_VM_OK);
  OAK_CHECK(has(&s, "b = 42"));
  OAK_CHECK(has(&s, "no local named 'missing' in scope"));
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerInvalidBreak)
{
  /* atoi would have accepted "12xyz" as 12; strtol must reject it. */
  struct dbg_session_t s =
      run_session(FLAT_SRC, "break 12xyz\nbreak 0\ncontinue\n", 0);
  OAK_CHECK(s.result == OAK_VM_OK);
  OAK_CHECK(has(&s, "usage: break <line>"));
  /* No breakpoint should have been created. */
  OAK_CHECK(!has(&s, "breakpoint #"));
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerInvalidDelete)
{
  struct dbg_session_t s =
      run_session(FLAT_SRC, "delete abc\ndelete 99\ncontinue\n", 0);
  OAK_CHECK(s.result == OAK_VM_OK);
  OAK_CHECK(has(&s, "usage: delete <id>"));
  OAK_CHECK(has(&s, "no breakpoint #99"));
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerNextStepsOverCall)
{
  /* From line 5 (let x = f(41)), `next` must land on line 6 without
   * stopping inside f(). */
  struct dbg_session_t s = run_session(FN_SRC, "next\nquit\n", 0);
  OAK_CHECK(has(&s, "stopped at test.oak:5"));
  OAK_CHECK(has(&s, "stopped at test.oak:6"));
  OAK_CHECK(!has(&s, "stopped at test.oak:2"));
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerStepEntersCall)
{
  /* From line 5, `step` must descend into f() (line 2). */
  struct dbg_session_t s = run_session(FN_SRC, "step\nquit\n", 0);
  OAK_CHECK(has(&s, "stopped at test.oak:5"));
  OAK_CHECK(has(&s, "stopped at test.oak:2"));
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerFinishReturnsToCaller)
{
  /* Break inside f() at line 2, then `finish` runs until f() returns,
   * landing back at the caller (line 6). */
  struct dbg_session_t s =
      run_session(FN_SRC, "continue\nfinish\nquit\n", 2);
  OAK_CHECK(has(&s, "stopped at test.oak:2"));
  OAK_CHECK(has(&s, "stopped at test.oak:6"));
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerNoHookRunsNormally)
{
  struct oak_compile_result_t cr = compile_debug("let x = 1; let y = 2;");
  OAK_CHECK(cr.chunk != null);

  struct oak_allocator_t* a = oak_test_allocator();
  struct oak_vm_t vm;
  oak_vm_init(&vm, a);

  const enum oak_vm_result_t r = oak_vm_run(&vm, cr.chunk);
  OAK_CHECK(r == OAK_VM_OK);

  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(DebuggerQuitImmediately),
    OAK_TEST_ENTRY(DebuggerContinueToEnd),
    OAK_TEST_ENTRY(DebuggerBreakpointHit),
    OAK_TEST_ENTRY(DebuggerLocalsCommand),
    OAK_TEST_ENTRY(DebuggerPrintCommand),
    OAK_TEST_ENTRY(DebuggerInvalidBreak),
    OAK_TEST_ENTRY(DebuggerInvalidDelete),
    OAK_TEST_ENTRY(DebuggerNextStepsOverCall),
    OAK_TEST_ENTRY(DebuggerStepEntersCall),
    OAK_TEST_ENTRY(DebuggerFinishReturnsToCaller),
    OAK_TEST_ENTRY(DebuggerNoHookRunsNormally),
  };
  return oak_test_run(tests, sizeof(tests) / sizeof(tests[0]));
}
