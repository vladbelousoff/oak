#pragma once

#include "oak_list.h"

typedef struct oak_lexer_result_t oak_lexer_result_t;

oak_lexer_result_t* oak_lexer_tokenize(const char* input);
const oak_list_head_t* oak_lexer_tokens(const oak_lexer_result_t* result);
void oak_lexer_cleanup(oak_lexer_result_t* result);
