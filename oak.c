#include "oak_cli.h"
#include "oak_compiler.h"
#include "oak_file_map.h"
#include "oak_lexer.h"
#include "oak_log.h"
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
  struct oak_parser_result_t result = {0};
  struct oak_compile_result_t cr = {0};
  int exit_code = 1;

  if (oak_file_map(cli.script_path, &source_map) != 0)
  {
    oak_mem_shutdown();
    return 1;
  }

  lexer = oak_lexer_tokenize(source_map.data, source_map.size);
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);

  for (int i = 0; i < oak_parser_error_count(&result); i++)
  {
    const struct oak_diagnostic_t* d = &oak_parser_errors(&result)[i];
    if (d->line > 0)
      oak_log(OAK_LOG_ERROR, "%d:%d: %s", d->line, d->column, d->message);
    else
      oak_log(OAK_LOG_ERROR, "%s", d->message);
  }

  const struct oak_ast_node_t* const root = oak_parser_root(&result);
  if (root)
  {
    oak_compile(root, &cr);

    for (int i = 0; i < cr.error_count; i++)
    {
      const struct oak_diagnostic_t* d = &cr.errors[i];
      if (d->line > 0)
        oak_log(OAK_LOG_ERROR, "%d:%d: %s", d->line, d->column, d->message);
      else
        oak_log(OAK_LOG_ERROR, "%s", d->message);
    }

    if (cr.chunk)
    {
      exit_code = 0;
      if (cli.disassemble)
        oak_chunk_disassemble(cr.chunk);
      else
      {
        struct oak_vm_t vm;
        oak_vm_init(&vm);
        exit_code = oak_vm_run(&vm, cr.chunk) != OAK_VM_OK;
        oak_vm_free(&vm);
      }
    }
  }

  oak_compile_result_free(&cr);
  oak_file_unmap(&source_map);
  oak_parser_free(&result);
  oak_lexer_free(lexer);
  oak_mem_shutdown();
  return exit_code;
}
