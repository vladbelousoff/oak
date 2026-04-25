#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"

#include <string.h>

static struct oak_chunk_t* try_compile(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  if (!root)
  {
    oak_parser_free(&result);
    oak_lexer_free(lexer);
    return null;
  }
  struct oak_compile_result_t cr = { 0 };
  oak_compile(root, &cr);
  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return cr.chunk;
}

OAK_TEST_DECL(MapMustBeTyped)
{
  /* Bare empty map literal is rejected. */
  struct oak_chunk_t* chunk = try_compile("let mut m = [:];");
  OAK_CHECK(chunk == null);

  /* Wrong key type is rejected at compile time. */
  chunk = try_compile("let mut m = [:] as [string:number];\n"
                      "m[1] = 2;\n");
  OAK_CHECK(chunk == null);

  /* Wrong value type is rejected at compile time. */
  chunk = try_compile("let mut m = [:] as [string:number];\n"
                      "m['x'] = 'oops';\n");
  OAK_CHECK(chunk == null);

  /* `push` is not available on maps. */
  chunk = try_compile("let mut m = [:] as [string:number];\n"
                      "m.push(1);\n");
  OAK_CHECK(chunk == null);

  /* Well-typed program compiles. */
  chunk = try_compile("let mut m = [:] as [string:number];\n"
                      "m['x'] = 1;\n"
                      "m['y'] = 2;\n"
                      "print(m.size());\n"
                      "print(m['x']);\n");
  OAK_CHECK(chunk != null);
  oak_chunk_free(chunk);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(MapMustBeTyped)
