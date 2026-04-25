#pragma once

#include "oak_chunk.h"
#include "oak_diagnostic.h"
#include "oak_parser.h"

struct oak_compile_result_t
{
  struct oak_chunk_t* chunk; /* NULL on failure */
  struct oak_diagnostic_t errors[OAK_MAX_DIAGNOSTICS];
  int error_count;
};

void oak_compile(const struct oak_ast_node_t* root,
                 struct oak_compile_result_t* out);
void oak_compile_result_free(struct oak_compile_result_t* result);

/* oak_compile_ex is declared in oak_bind.h (requires oak_compile_options_t). */
