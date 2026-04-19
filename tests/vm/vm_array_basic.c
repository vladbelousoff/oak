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

OAK_TEST_DECL(ArrayBasic)
{
  /* Push elements, read them back via indexing, mutate via index assignment,
   * and call .len() / .push() as array methods. */
  const char* source = "let mut arr = [] as number[];\n"
                       "arr.push(10);\n"
                       "arr.push(20);\n"
                       "arr.push(30);\n"
                       "print(arr.len());\n"
                       "print(arr[0]);\n"
                       "print(arr[1]);\n"
                       "print(arr[2]);\n"
                       "arr[1] = 99;\n"
                       "print(arr[1]);\n";
  return run_program(source);
}

OAK_TEST_MAIN(ArrayBasic)
