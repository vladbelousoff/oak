#pragma once

struct yyjson_mut_doc;

/* Pretty-print a mutable yyjson document with two spaces per indent.
 * The document must have its root set. Returns a heap-allocated string (malloc);
 * free with free(). */
char* oak_json_pretty2_write(struct yyjson_mut_doc* doc);
