#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_mem.h"
#include "oak_parser.h"
#include "oak_vm.h"

#include <stdio.h>
#include <stdlib.h>

static char* read_file(const char* path)
{
  FILE* file = fopen(path, "rb");
  if (!file)
  {
    fprintf(stderr, "error: could not open file '%s'\n", path);
    return NULL;
  }

  fseek(file, 0, SEEK_END);
  const long size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char* buffer = oak_alloc(size + 1, OAK_SRC_LOC);
  if (!buffer)
  {
    fprintf(stderr, "error: not enough memory to read '%s'\n", path);
    fclose(file);
    return NULL;
  }

  const size_t bytes_read = fread(buffer, 1, size, file);
  buffer[bytes_read] = '\0';
  fclose(file);

  return buffer;
}

int main(const int argc, const char* argv[])
{
  if (argc < 2)
  {
    fprintf(stderr, "usage: oak <script>\n");
    return 1;
  }

  char* source = read_file(argv[1]);
  if (!source)
    return 1;

  oak_mem_init();

  oak_lexer_result_t* lexer = oak_lexer_tokenize(source);

  oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_KIND_PROGRAM);
  const oak_ast_node_t* root = oak_parser_root(result);
  if (!root)
  {
    oak_free(source, OAK_SRC_LOC);
    oak_parser_cleanup(result);
    oak_lexer_cleanup(lexer);
    oak_mem_shutdown();
    return 1;
  }

  oak_chunk_t* chunk = oak_compile(root);
  if (!chunk)
  {
    oak_free(source, OAK_SRC_LOC);
    oak_parser_cleanup(result);
    oak_lexer_cleanup(lexer);
    oak_mem_shutdown();
    return 1;
  }

  oak_chunk_disassemble(chunk);

  oak_vm_t vm;
  oak_vm_init(&vm);
  const oak_vm_result_t vm_result = oak_vm_run(&vm, chunk);
  oak_vm_free(&vm);
  oak_chunk_free(chunk);

  oak_free(source, OAK_SRC_LOC);
  oak_parser_cleanup(result);
  oak_lexer_cleanup(lexer);

  oak_mem_shutdown();

  return vm_result != OAK_VM_OK ? 1 : 0;
}
