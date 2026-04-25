#include "oak_bind.h"
#include "oak_compiler.h"
#include "oak_count_of.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_stdlib_file.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <stdio.h>
#include <string.h>

OAK_TEST_DECL(StdlibFileWriteReadRoundTrip)
{
  const char* source =
      "let w = File.open('oak_vm_file_test.tmp', 'w');\n"
      "w.write('hello line\\nline two\\n');\n"
      "w.close();\n"
      "let r = File.open('oak_vm_file_test.tmp', 'r');\n"
      "let t = r.read_all();\n"
      "let e = r.eof();\n"
      "r.close();\n"
      "print(t);\n";

  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t pr = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &pr);
  const struct oak_ast_node_t* root = oak_parser_root(&pr);
  OAK_CHECK(root != null);

  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);
  oak_stdlib_register_file(&opts);

  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, &opts, &cr);
  OAK_CHECK(cr.chunk != null);

  struct oak_vm_t vm;
  oak_vm_init(&vm);
  const enum oak_vm_result_t run = oak_vm_run(&vm, cr.chunk);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
  oak_compile_options_free(&opts);
  oak_parser_free(&pr);
  oak_lexer_free(lexer);

  OAK_CHECK(run == OAK_VM_OK);
  remove("oak_vm_file_test.tmp");
  return OAK_TEST_OK;
}

OAK_TEST_DECL(StdlibFileReadlineAndEof)
{
  const char* source =
      "let w = File.open('oak_vm_file_rl.tmp', 'w');\n"
      "w.write('A\\nB\\n');\n"
      "w.close();\n"
      "let r = File.open('oak_vm_file_rl.tmp', 'r');\n"
      "let a = r.read();\n"
      "let b = r.read();\n"
      "let c = r.eof();\n"
      "r.close();\n"
      "print(a);\n"
      "print(b);\n";

  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t pr = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &pr);
  const struct oak_ast_node_t* root = oak_parser_root(&pr);
  OAK_CHECK(root != null);

  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);
  oak_stdlib_register_file(&opts);

  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, &opts, &cr);
  OAK_CHECK(cr.chunk != null);

  struct oak_vm_t vm;
  oak_vm_init(&vm);
  const enum oak_vm_result_t run = oak_vm_run(&vm, cr.chunk);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);
  oak_compile_options_free(&opts);
  oak_parser_free(&pr);
  oak_lexer_free(lexer);

  OAK_CHECK(run == OAK_VM_OK);
  remove("oak_vm_file_rl.tmp");
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(StdlibFileWriteReadRoundTrip),
    OAK_TEST_ENTRY(StdlibFileReadlineAndEof),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
