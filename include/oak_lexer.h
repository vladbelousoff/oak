#pragma once

#include "oak_export.h"
#include "oak_list.h"

typedef struct oak_allocator oak_allocator_t;
typedef struct oak_lexer_result oak_lexer_result_t;

OAK_API oak_lexer_result_t*
oak_lexer_tokenize(const char* input,
                   oak_allocator_t* allocator);
OAK_API const oak_list_entry_t*
oak_lexer_tokens(const oak_lexer_result_t* result);
/* Returns the number of errors encountered during tokenization. A non-zero
 * value means the token list may be incomplete or contain gaps. */
OAK_API int oak_lexer_error_count(const oak_lexer_result_t* result);
OAK_API void oak_lexer_free(oak_lexer_result_t* result);
