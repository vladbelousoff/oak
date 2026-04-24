#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <string.h>

static enum oak_test_status_t run_ok(const char* source)
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

static enum oak_test_status_t compile_fails(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK(root != null);

  struct oak_chunk_t* chunk = oak_compile(root);
  OAK_CHECK(chunk == null);

  oak_parser_free(result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(FnArgArray)
{
  /* Pass a number[] to a function that indexes into it. */
  const char* source =
      "fn first(arr: number[]) -> number { return arr[0]; }\n"
      "let a = [10, 20, 30];\n"
      "print(first(a));";
  OAK_CHECK(run_ok(source) == OAK_TEST_OK);

  /* Pass a mutable array and push into it from inside the function. */
  const char* source_mut =
      "fn append(mut arr: number[]) -> number {\n"
      "  arr.push(99);\n"
      "  return arr.size();\n"
      "}\n"
      "let mut b = [1, 2];\n"
      "print(append(b));";
  OAK_CHECK(run_ok(source_mut) == OAK_TEST_OK);

  /* Passing a plain number where number[] is expected must fail. */
  const char* bad =
      "fn first(arr: number[]) -> number { return arr[0]; }\n"
      "print(first(42));";
  OAK_CHECK(compile_fails(bad) == OAK_TEST_OK);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(FnArgArray)
