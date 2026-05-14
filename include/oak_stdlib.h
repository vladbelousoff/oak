#pragma once

#include "oak_export.h"

struct oak_compile_options_t;

/* Registers all Oak standard library surface (file, lexer, ...). */
OAK_API void oak_stdlib_register(struct oak_compile_options_t* opts);
