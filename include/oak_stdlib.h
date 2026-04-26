#pragma once

struct oak_compile_options_t;

/* Registers all Oak standard library surface (file, lexer, ...). */
void oak_stdlib_register(struct oak_compile_options_t* opts);
