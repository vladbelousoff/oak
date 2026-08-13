#pragma once

#include "oak_export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oak_compile_options oak_compile_options_t;

/* Registers all Oak standard library surface (file, lexer, ...). */
OAK_API void oak_stdlib_register(oak_compile_options_t* opts);

#ifdef __cplusplus
}
#endif
