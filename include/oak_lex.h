#pragma once

#include "oak_list.h"

typedef struct oak_lex_result_t oak_lex_result_t;

oak_lex_result_t* oak_lex_tokenize(const char* input);
const oak_list_head_t* oak_lex_tokens(const oak_lex_result_t* result);
void oak_lex_cleanup(oak_lex_result_t* result);
