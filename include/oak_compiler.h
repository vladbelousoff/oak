#pragma once

#include "oak_chunk.h"
#include "oak_parser.h"

struct oak_chunk_t* oak_compile(const struct oak_ast_node_t* root);
