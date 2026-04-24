#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <string.h>

static enum oak_test_status_t run_program(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t result = {0};
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = {0};
  oak_compile(root, &cr);
  OAK_CHECK(cr.chunk != null);

  struct oak_vm_t vm;
  oak_vm_init(&vm);
  const enum oak_vm_result_t r = oak_vm_run(&vm, cr.chunk);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  OAK_CHECK(r == OAK_VM_OK);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(MapBasic)
{
  /* Construct a typed map, insert and update entries, look them up by key,
   * and call .size() as a method on the map receiver. */
  const char* source = "let mut a = [:] as [string:number];\n"
                       "a['one'] = 1;\n"
                       "a['two'] = 2;\n"
                       "a['three'] = 3;\n"
                       "print(a.size());\n"
                       "print(a['one']);\n"
                       "print(a['two']);\n"
                       "print(a['three']);\n"
                       "a['two'] = 22;\n"
                       "print(a['two']);\n";
  return run_program(source);
}

OAK_TEST_MAIN(MapBasic)
