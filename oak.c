#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_mem.h"
#include "oak_parser.h"

int main(const int argc, const char* argv[])
{
  (void)argc;
  (void)argv;

  oak_mem_init();

  oak_lexer_result_t* lexer = oak_lexer_tokenize("let n = 24;\n"
                                                 "let i = 1;\n"
                                                 "while i < n {\n"
                                                 "  if n % i == 0 {\n"
                                                 "    print(msg = i);\n"
                                                 "  }\n"
                                                 "  i += 1;\n"
                                                 "}\n");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  oak_chunk_t* chunk = oak_compile(oak_parser_root(result));
  oak_chunk_free(chunk);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  oak_mem_shutdown();

  return 0;
}
