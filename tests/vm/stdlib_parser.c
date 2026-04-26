#include "oak_bind.h"
#include "oak_compiler.h"
#include "oak_count_of.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_stdlib.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <string.h>

OAK_TEST_DECL(StdlibParserTokenizeThenParse)
{
  const char* source =
      "let toks = OakLexer.tokenize('let x = 1;\\n');\n"
      "let r = OakParser.parse(toks);\n"
      "print(r.error_count);\n"
      "print(r.root.kind);\n"
      "print(r.root);\n"
      "r.dispose();\n";

  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t pr = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &pr);
  const struct oak_ast_node_t* root = oak_parser_root(&pr);
  OAK_CHECK(root != null);

  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);
  oak_stdlib_register(&opts);

  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, &opts, &cr);
  OAK_CHECK(cr.error_count == 0);
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
  return OAK_TEST_OK;
}

OAK_TEST_DECL(StdlibParserUnexpectedTokenErrors)
{
  const char* source =
      "let toks = OakLexer.tokenize('+');\n"
      "let r = OakParser.parse(toks);\n"
      "print(r.error_count);\n"
      "r.dispose();\n";

  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t pr = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &pr);
  const struct oak_ast_node_t* root = oak_parser_root(&pr);
  OAK_CHECK(root != null);

  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts);
  oak_stdlib_register(&opts);

  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(root, &opts, &cr);
  OAK_CHECK(cr.error_count == 0);
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
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(StdlibParserTokenizeThenParse),
    OAK_TEST_ENTRY(StdlibParserUnexpectedTokenErrors),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
