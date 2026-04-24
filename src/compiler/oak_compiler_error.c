#include "oak_compiler_internal.h"

#include <stdarg.h>
#include <stdio.h>

struct oak_code_loc_t oak_compiler_loc_from_token(const struct oak_token_t* t)
{
  return (struct oak_code_loc_t){
    .line = oak_token_line(t),
    .column = oak_token_column(t),
  };
}

void oak_compiler_error_at(struct oak_compiler_t* c,
                           const struct oak_token_t* token,
                           const char* fmt,
                           ...)
{
  if (c->result->error_count < OAK_MAX_DIAGNOSTICS)
  {
    struct oak_diagnostic_t* d = &c->result->errors[c->result->error_count++];
    d->line = token ? oak_token_line(token) : 0;
    d->column = token ? oak_token_column(token) : 0;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->message, sizeof(d->message), fmt, ap);
    va_end(ap);
  }
  c->has_error = 1;
}
