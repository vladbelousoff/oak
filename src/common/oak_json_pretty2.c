#include "oak_json_pretty2.h"

#include "oak_types.h"
#include "yyjson.h"

char* oak_json_pretty2_write(struct yyjson_mut_doc* const doc)
{
  if (!doc)
    return null;
  size_t len;
  return yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, &len);
}
