#pragma once

#include "oak_export.h"

typedef struct oak_compile_options oak_compile_options_t;

/* Registers File (static open, instance read/read_all/write/eof/close). */
OAK_API void oak_stdlib_register_file(oak_compile_options_t* opts);
