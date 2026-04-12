#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"

#include <string.h>

OAK_TEST_DECL(FnCallArgTypeMismatchFailsCompile)
{
  const char* source = "fn echo(s : string) -> number { return 1; }\n"
                       "print(echo(42));";

  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK(root != NULL);

  struct oak_chunk_t* chunk = oak_compile(root);
  OAK_CHECK(chunk == NULL);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(FnCallArgTypeMismatchFailsCompile)
