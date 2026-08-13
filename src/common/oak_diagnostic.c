#include "oak_diagnostic.h"

#include "oak_log.h"

void oak_diagnostics_print(const oak_diagnostic_t* diags, const int count)
{
  if (!diags)
    return;
  for (int i = 0; i < count; ++i)
  {
    const oak_diagnostic_t* d = &diags[i];
    if (d->line > 0)
      oak_log(OAK_LOG_ERROR, "%d:%d: %s", d->line, d->column, d->message);
    else
      oak_log(OAK_LOG_ERROR, "%s", d->message);
  }
}
