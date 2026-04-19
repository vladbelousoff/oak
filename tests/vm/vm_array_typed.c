#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"

#include <string.h>

static struct oak_chunk_t* try_compile(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  if (!root)
  {
    oak_parser_free(result);
    oak_lexer_free(lexer);
    return null;
  }
  struct oak_chunk_t* chunk = oak_compile(root);
  oak_parser_free(result);
  oak_lexer_free(lexer);
  return chunk;
}

OAK_TEST_DECL(ArrayMustBeTyped)
{
  /* Bare empty array literal is rejected. */
  struct oak_chunk_t* chunk = try_compile("let mut arr = [];");
  OAK_CHECK(chunk == null);

  /* Pushing a wrong-typed value via `.push()` is rejected at compile time. */
  chunk = try_compile("let mut arr = [] as number[];\n"
                      "arr.push('oops');\n");
  OAK_CHECK(chunk == null);

  /* Wrong-typed indexed assignment is rejected at compile time. */
  chunk = try_compile("let mut arr = [] as number[];\n"
                      "arr.push(1);\n"
                      "arr[0] = 'oops';\n");
  OAK_CHECK(chunk == null);

  /* `push` and `len` are not visible as global functions anymore. */
  chunk = try_compile("let mut arr = [] as number[];\n"
                      "push(arr, 1);\n");
  OAK_CHECK(chunk == null);

  chunk = try_compile("let mut arr = [] as number[];\n"
                      "print(len(arr));\n");
  OAK_CHECK(chunk == null);

  /* The well-typed program using methods must compile. */
  chunk = try_compile("let mut arr = [] as number[];\n"
                      "arr.push(1);\n"
                      "arr[0] = 42;\n"
                      "print(arr.len());\n");
  OAK_CHECK(chunk != null);
  oak_chunk_free(chunk);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ArrayMustBeTyped)
