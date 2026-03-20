#include "oak_lex.h"
#include "oak_mem.h"
#include "oak_parser.h"

int main(const int argc, const char* argv[])
{
  (void)argc;
  (void)argv;

  oak_mem_init();

  oak_lex_t lex;
  oak_lex_tokenize("type SampleType { field1 : number; field2 : string; }\n"
                   "-1000 + (10 - 7) * 10;\n",
                   &lex);

  oak_parser_result_t* result = oak_parse(&lex, OAK_NODE_KIND_PROGRAM);
  (void)oak_parser_root(result);

  oak_parser_cleanup(result);
  oak_lex_cleanup(&lex);

  oak_mem_shutdown();

  return 0;
}
