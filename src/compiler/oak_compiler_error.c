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
  static _Thread_local char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (token)
    oak_log(OAK_LOG_ERROR,
            "%d:%d: error: %s",
            oak_token_line(token),
            oak_token_column(token),
            buf);
  else
    oak_log(OAK_LOG_ERROR, "error: %s", buf);
  c->has_error = 1;
}
