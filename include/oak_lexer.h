#pragma once

#include "oak_list.h"

struct oak_lexer_result_t;

struct oak_lexer_result_t* oak_lexer_tokenize(const char* input, usize len);
const struct oak_list_entry_t*
oak_lexer_tokens(const struct oak_lexer_result_t* result);
/* Returns the number of errors encountered during tokenization. A non-zero
 * value means the token list may be incomplete or contain gaps. */
int oak_lexer_error_count(const struct oak_lexer_result_t* result);
void oak_lexer_free(struct oak_lexer_result_t* result);
