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
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = { 0 };
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

OAK_TEST_DECL(ArrayBasic)
{
  /* Push elements, read them back via indexing, mutate via index assignment,
   * and call .size() / .push() as array methods. */
  const char* source = "let mut arr = [] as number[];\n"
                       "arr.push(10);\n"
                       "arr.push(20);\n"
                       "arr.push(30);\n"
                       "print(arr.size());\n"
                       "print(arr[0]);\n"
                       "print(arr[1]);\n"
                       "print(arr[2]);\n"
                       "arr[1] = 99;\n"
                       "print(arr[1]);\n";
  return run_program(source);
}

OAK_TEST_MAIN(ArrayBasic)
