#include "oak_lex.h"
#include "oak_mem.h"
#include "oak_parser.h"

int main(const int argc, const char* argv[])
{
  (void)argc;
  (void)argv;

  oak_mem_init();

  oak_lex_t lex;
  oak_lex_tokenize("type SampleType { field1 : number; field2 : string; }",
                   &lex);

  const oak_ast_node_t* program_node = oak_parse(&lex, OAK_PARSER_RULE_PROGRAM);
  oak_lex_cleanup(&lex);

  oak_mem_shutdown();

  return 0;
}
