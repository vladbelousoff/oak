#include "oak_stdlib.h"
#include "oak_stdlib_file.h"
#include "oak_stdlib_lexer.h"
#include "oak_stdlib_parser.h"

void oak_stdlib_register(struct oak_compile_options_t* opts)
{
  oak_stdlib_register_file(opts);
  oak_stdlib_register_lexer(opts);
  oak_stdlib_register_parser(opts);
}
