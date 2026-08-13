#pragma once

#include "oak_export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OAK_MAX_DIAGNOSTICS 64

typedef struct oak_diagnostic oak_diagnostic_t;
struct oak_diagnostic
{
  int line; /* 0 = no source location */
  int column;
  char message[512];
};

/*
 * Where to find the diagnostics for each stage:
 *
 *   parsing      oak_parser_errors() / oak_parser_error_count()
 *   compiling    oak_compile_result_t::errors / ::error_count
 *   loading      oak_module_loader_result_t::errors / ::error_count
 *
 * The parser hands its diagnostics out through accessors because the result
 * owns an arena; the other two are plain structs with the array stored inline
 * (no allocation, capped at OAK_MAX_DIAGNOSTICS), so read them directly.
 */

/* Print each diagnostic to stderr as "line:column: message", or just the
 * message when line is 0. Safe with count == 0 or diags == NULL. */
OAK_API void oak_diagnostics_print(const oak_diagnostic_t* diags, int count);

#ifdef __cplusplus
}
#endif
