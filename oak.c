#include "oak_compiler.h"
#include "oak_file_map.h"
#include "oak_lexer.h"
#include "oak_mem.h"
#include "oak_parser.h"
#include "oak_vm.h"

#include <stdio.h>

int main(const int argc, const char* argv[])
{
  if (argc < 2)
  {
    fprintf(stderr, "usage: oak <script>\n");
    return 1;
  }

  oak_mem_init();

  struct oak_file_map_t source_map;
  if (oak_file_map(argv[1], &source_map) != 0)
  {
    oak_mem_shutdown();
    return 1;
  }

  struct oak_lexer_result_t* lexer =
      oak_lexer_tokenize(source_map.data, source_map.size);
  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  if (!root)
  {
    oak_file_unmap(&source_map);
    oak_parser_cleanup(result);
    oak_lexer_cleanup(lexer);
    oak_mem_shutdown();
    return 1;
  }

  struct oak_chunk_t* chunk = oak_compile(root);
  if (!chunk)
  {
    oak_file_unmap(&source_map);
    oak_parser_cleanup(result);
    oak_lexer_cleanup(lexer);
    oak_mem_shutdown();
    return 1;
  }

  oak_chunk_disassemble(chunk);

  struct oak_vm_t vm;
  oak_vm_init(&vm);
  const enum oak_vm_result_t vm_result = oak_vm_run(&vm, chunk);
  oak_vm_free(&vm);
  oak_chunk_free(chunk);

  oak_file_unmap(&source_map);
  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  oak_mem_shutdown();

  return vm_result != OAK_VM_OK ? 1 : 0;
}
