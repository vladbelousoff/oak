#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_mem.h"
#include "oak_parser.h"
#include "oak_vm.h"

int main(const int argc, const char* argv[])
{
  (void)argc;
  (void)argv;

  oak_mem_init();

  oak_lexer_result_t* lexer = oak_lexer_tokenize("for n from 2 to 1000 {\n"
                                                 "  let is_prime = 1;\n"
                                                 "  for i from 2 to n - 1 {\n"
                                                 "    if n % i == 0 {\n"
                                                 "      is_prime = 0;\n"
                                                 "    }\n"
                                                 "  }\n"
                                                 "  if is_prime == 1 {\n"
                                                 "    print(msg = n);\n"
                                                 "  }\n"
                                                 "}\n");

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  oak_chunk_t* chunk = oak_compile(root);

  if (chunk)
  {
    oak_vm_t vm;
    oak_vm_init(&vm);
    oak_vm_run(&vm, chunk);
    oak_vm_free(&vm);
    oak_chunk_free(chunk);
  }

  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  oak_mem_shutdown();

  return 0;
}
