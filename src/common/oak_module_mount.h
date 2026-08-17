#pragma once

/*
 * Module mounts: the mechanism a package system uses to make `import json.lexer`
 * resolve somewhere other than next to the importing file.
 *
 * A mount claims one first dotted segment (`ns`) and points it at a directory
 * (`root_dir`).  The rest of the dotted name resolves under that directory
 * exactly as it always has -- `path_resolve_dotted`, unchanged -- so `json` is
 * `<root>/json.oak` and `json.lexer` is `<root>/json/lexer.oak`, the same rule
 * the stdlib has always followed.
 *
 * `scope_root` is what keeps one package's dependencies out of another's.  A
 * mount with a null scope applies program-wide; a mount scoped to a directory
 * applies only to modules under it.  When several mounts claim the same
 * namespace, the one with the longest matching scope wins, so a package's own
 * dependency list shadows its parent's rather than merging with it.
 *
 * The layout lives here rather than in a public header because nothing outside
 * the loader reads it: an embedder adds mounts through
 * oak_module_loader_mount() and never sees this struct.
 */

#include "oak_allocator.h"
#include "oak_container.h"
#include "oak_types.h"

typedef struct oak_compile_options oak_compile_options_t;

typedef struct oak_module_mount oak_module_mount_t;
struct oak_module_mount
{
  /* Canonical directory whose modules may use this mount, or null for
   * program-wide.  Owned. */
  char* scope_root;
  /* The first dotted segment this mount claims, e.g. "json".  Owned. */
  char* ns;
  /* Canonical directory that dotted names resolve against.  Owned. */
  char* root_dir;
  /* Package name for diagnostics, e.g. "acme/json", or null.  Owned. */
  char* package;
};

/* Root directory for the namespace `ns` (the first `ns_len` bytes, so a caller
 * can pass the leading segment of a dotted name without copying it) as seen
 * from a module in `importer_dir`, or null when no mount claims it.
 * `importer_dir` may be null, which matches only program-wide mounts.
 * `out_package` receives the owning package name (or null) when non-null.  The
 * returned string is borrowed from the mount. */
const char* oak_module_mount_find(const oak_container_t* mounts,
                                  const char* importer_dir,
                                  const char* ns,
                                  usize ns_len,
                                  const char** out_package);

/* Free every mount's strings and the vector itself. */
void oak_module_mounts_free(oak_allocator_t* a, oak_container_t* mounts);
