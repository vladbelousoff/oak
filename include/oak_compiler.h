#pragma once

#include "oak_chunk.h"
#include "oak_parser.h"

oak_chunk_t* oak_compile(const oak_ast_node_t* root);
