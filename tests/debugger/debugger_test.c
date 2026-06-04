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

static FILE* redirect_stdin(const char* commands)
{
  FILE* f = tmpfile();
  if (!f)
    return null;
  fwrite(commands, 1, strlen(commands), f);
  rewind(f);
  stdin = f;
  return f;
}

static void restore_stdin(FILE* saved, FILE* fake)
{
  if (fake)
    fclose(fake);
  stdin = saved;
}

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

static void attach_debugger(struct oak_vm_t* vm,
                            struct oak_debugger_t* dbg,
                            struct oak_vm_debug_hook_t* hook)
{
  hook->fn = oak_debugger_hook;
  hook->ctx = dbg;
  oak_vm_set_debug_hook(vm, hook);
}

/* ── tests ─────────────────────────────────────────────────────────── */

OAK_TEST_DECL(DebuggerQuitImmediately)
{
  struct oak_compile_result_t cr = compile_debug("let x = 42;");
  OAK_CHECK(cr.chunk != null);

  struct oak_allocator_t* a = oak_test_allocator();
  struct oak_debugger_t dbg;
  oak_debugger_init(&dbg, a);

  struct oak_vm_t vm;
  oak_vm_init(&vm, a);
  struct oak_vm_debug_hook_t hook;
  attach_debugger(&vm, &dbg, &hook);

  FILE* saved = stdin;
  FILE* fake = redirect_stdin("quit\n");
  OAK_CHECK(fake != null);

  const enum oak_vm_result_t r = oak_vm_run(&vm, cr.chunk);
  restore_stdin(saved, fake);

  OAK_CHECK(r == OAK_VM_DEBUG_HALT);

  oak_debugger_free(&dbg);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerContinueToEnd)
{
  struct oak_compile_result_t cr = compile_debug("let x = 1; let y = 2;");
  OAK_CHECK(cr.chunk != null);

  struct oak_allocator_t* a = oak_test_allocator();
  struct oak_debugger_t dbg;
  oak_debugger_init(&dbg, a);

  struct oak_vm_t vm;
  oak_vm_init(&vm, a);
  struct oak_vm_debug_hook_t hook;
  attach_debugger(&vm, &dbg, &hook);

  FILE* saved = stdin;
  FILE* fake = redirect_stdin("continue\n");
  OAK_CHECK(fake != null);

  const enum oak_vm_result_t r = oak_vm_run(&vm, cr.chunk);
  restore_stdin(saved, fake);

  OAK_CHECK(r == OAK_VM_OK);

  oak_debugger_free(&dbg);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerBreakpointHit)
{
  const char* src = "let a = 10;\nlet b = 20;\nlet c = 30;";
  struct oak_compile_result_t cr = compile_debug(src);
  OAK_CHECK(cr.chunk != null);

  struct oak_allocator_t* a = oak_test_allocator();
  struct oak_debugger_t dbg;
  oak_debugger_init(&dbg, a);
  oak_debugger_add_breakpoint(&dbg, 2, "test.oak");

  struct oak_vm_t vm;
  oak_vm_init(&vm, a);
  struct oak_vm_debug_hook_t hook;
  attach_debugger(&vm, &dbg, &hook);

  FILE* saved = stdin;
  FILE* fake = redirect_stdin("continue\ncontinue\n");
  OAK_CHECK(fake != null);

  const enum oak_vm_result_t r = oak_vm_run(&vm, cr.chunk);
  restore_stdin(saved, fake);

  OAK_CHECK(r == OAK_VM_OK);

  oak_debugger_free(&dbg);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerStepCommand)
{
  const char* src = "let a = 1;\nlet b = 2;\nlet c = 3;";
  struct oak_compile_result_t cr = compile_debug(src);
  OAK_CHECK(cr.chunk != null);

  struct oak_allocator_t* a = oak_test_allocator();
  struct oak_debugger_t dbg;
  oak_debugger_init(&dbg, a);

  struct oak_vm_t vm;
  oak_vm_init(&vm, a);
  struct oak_vm_debug_hook_t hook;
  attach_debugger(&vm, &dbg, &hook);

  FILE* saved = stdin;
  FILE* fake = redirect_stdin("step\nstep\nstep\ncontinue\n");
  OAK_CHECK(fake != null);

  const enum oak_vm_result_t r = oak_vm_run(&vm, cr.chunk);
  restore_stdin(saved, fake);

  OAK_CHECK(r == OAK_VM_OK);

  oak_debugger_free(&dbg);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(DebuggerLocalsCommand)
{
  const char* src = "let x = 42;\nlet y = 99;";
  struct oak_compile_result_t cr = compile_debug(src);
  OAK_CHECK(cr.chunk != null);

  struct oak_allocator_t* a = oak_test_allocator();
  struct oak_debugger_t dbg;
  oak_debugger_init(&dbg, a);
  oak_debugger_add_breakpoint(&dbg, 2, "test.oak");

  struct oak_vm_t vm;
  oak_vm_init(&vm, a);
  struct oak_vm_debug_hook_t hook;
  attach_debugger(&vm, &dbg, &hook);

  FILE* saved = stdin;
  FILE* fake = redirect_stdin("continue\nlocals\ncontinue\n");
  OAK_CHECK(fake != null);

  const enum oak_vm_result_t r = oak_vm_run(&vm, cr.chunk);
  restore_stdin(saved, fake);

  OAK_CHECK(r == OAK_VM_OK);

  oak_debugger_free(&dbg);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
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
    OAK_TEST_ENTRY(DebuggerStepCommand),
    OAK_TEST_ENTRY(DebuggerLocalsCommand),
    OAK_TEST_ENTRY(DebuggerNoHookRunsNormally),
  };
  return oak_test_run(tests, sizeof(tests) / sizeof(tests[0]));
}
