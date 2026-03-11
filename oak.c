#include "src/oak_lex.h"
#include "src/oak_mem.h"

int main(const int argc, const char* argv[])
{
  (void)argc;
  (void)argv;

  // oak_mem_init();

  oak_lex_t lex;
  oak_lex_tokenize("'Привет, мир!' 289.10\n1000", &lex);
  oak_lex_cleanup(&lex);

  oak_mem_shutdown();

  return 0;
}
