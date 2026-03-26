#include "oak_lexer.h"
#include "oak_mem.h"
#include "oak_parser.h"

int main(const int argc, const char* argv[])
{
  (void)argc;
  (void)argv;

  oak_mem_init();

  oak_lexer_result_t* lexer = oak_lexer_tokenize(
      "type SampleStruct struct { field1 : number; field2 : string; }\n"
      "type SampleEnum enum { Field1 Field2 }\n"
      "let x = -1000 + (10 - 7) * 10;\n");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  (void)oak_parser_root(result);

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  oak_mem_shutdown();

  return 0;
}
