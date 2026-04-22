#include "oak_cli.h"
#include "oak_compiler.h"
#include "oak_file_map.h"
#include "oak_lexer.h"
#include "oak_mem.h"
#include "oak_parser.h"
#include "oak_vm.h"

#include <stdio.h>

int main(const int argc, const char* argv[])
{
  struct oak_cli_args_t cli;
  if (oak_cli_parse(argc, argv, &cli) != 0)
  {
    if (cli.error)
      fprintf(stderr, "oak: %s\n", cli.error);
    oak_cli_usage(stderr);
    return 1;
  }

  if (cli.help)
  {
    oak_cli_usage(stdout);
    return 0;
  }

  oak_mem_init();

  struct oak_file_map_t source_map;
  struct oak_lexer_result_t* lexer = null;
  struct oak_parser_result_t* result = null;
  struct oak_chunk_t* chunk = null;
  int exit_code = 1;

  if (oak_file_map(cli.script_path, &source_map) != 0)
  {
    oak_mem_shutdown();
    return 1;
  }

  lexer = oak_lexer_tokenize(source_map.data, source_map.size);
  result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* const root = oak_parser_root(result);
  if (root)
  {
    chunk = oak_compile(root);
    if (chunk)
    {
      exit_code = 0;
      if (cli.disassemble)
        oak_chunk_disassemble(chunk);
      else
      {
        struct oak_vm_t vm;
        oak_vm_init(&vm);
        exit_code = oak_vm_run(&vm, chunk) != OAK_VM_OK;
        oak_vm_free(&vm);
      }
    }
  }

  if (chunk)
    oak_chunk_free(chunk);
  oak_file_unmap(&source_map);
  oak_parser_free(result);
  oak_lexer_free(lexer);
  oak_mem_shutdown();
  return exit_code;
}
