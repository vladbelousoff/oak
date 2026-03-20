#pragma once

#include "oak_arena.h"
#include "oak_list.h"

typedef struct _oak_lex_t
{
  oak_list_head_t tokens;
  oak_arena_t arena;
} oak_lex_t;

void oak_lex_tokenize(const char* input, oak_lex_t* output);
void oak_lex_cleanup(oak_lex_t* lex);
