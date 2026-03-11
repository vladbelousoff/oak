#pragma once

#include "oak_list.h"

typedef struct
{
  oak_list_head_t tokens;
} oak_lex_t;

void oak_lex_tokenize(const char* input, oak_lex_t* output);
void oak_lex_cleanup(const oak_lex_t* lex);
