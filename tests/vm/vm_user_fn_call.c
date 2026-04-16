#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <string.h>

OAK_TEST_DECL(UserFnCall)
{
  const char* source =
      "fn add(a : number, b : number) -> number { return a + b; }\n"
      "print(add(1, 2));";

  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK(root != null);

  struct oak_chunk_t* chunk = oak_compile(root);
  OAK_CHECK(chunk != null);

  struct oak_vm_t vm;
  oak_vm_init(&vm);
  const enum oak_vm_result_t r = oak_vm_run(&vm, chunk);
  oak_vm_free(&vm);
  oak_chunk_free(chunk);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  OAK_CHECK(r == OAK_VM_OK);
  return OAK_TEST_OK;
}

OAK_TEST_MAIN(UserFnCall)
