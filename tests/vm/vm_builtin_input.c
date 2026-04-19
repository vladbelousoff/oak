#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

static enum oak_test_status_t run_program(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK(root != null);

  struct oak_chunk_t* chunk = oak_compile(root);
  OAK_CHECK(chunk != null);

  struct oak_vm_t vm;
  oak_vm_init(&vm);
  const enum oak_vm_result_t r = oak_vm_run(&vm, chunk);
  oak_vm_free(&vm);
  oak_chunk_free(chunk);

  oak_parser_free(result);
  oak_lexer_free(lexer);

  OAK_CHECK(r == OAK_VM_OK);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(BuiltinInput)
{
#if defined(_WIN32)
  (void)run_program;
  return OAK_TEST_OK;
#else
  char tmpl[] = "/tmp/oak_input_testXXXXXX";
  const int fd = mkstemp(tmpl);
  OAK_CHECK(fd >= 0);
  const char line[] = "oak_line\n";
  OAK_CHECK(write(fd, line, sizeof(line) - 1u) == (ssize_t)(sizeof(line) - 1u));
  OAK_CHECK(close(fd) == 0);
  OAK_CHECK(freopen(tmpl, "r", stdin) != null);
  (void)unlink(tmpl);

  return run_program("print(input());");
#endif
}

OAK_TEST_MAIN(BuiltinInput)
