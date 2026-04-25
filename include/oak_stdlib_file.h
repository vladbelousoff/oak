#pragma once

struct oak_compile_options_t;

/* Registers File (static open, instance read/read_all/write/eof/close). */
void oak_stdlib_register_file(struct oak_compile_options_t* opts);
