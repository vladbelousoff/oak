#pragma once

#include "oak_bind.h"
#include "oak_diagnostic.h"
#include "oak_export.h"
#include "oak_module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oak_module_loader_result oak_module_loader_result_t;
struct oak_module_loader_result
{
  /* Resolved entry module. May be null on failure. */
  oak_module_t* entry;
  /* Aggregated diagnostics from any module's parse/compile. */
  oak_diagnostic_t errors[OAK_MAX_DIAGNOSTICS];
  int error_count;
};

/* Loads `entry_path` and recursively loads every module reachable through
 * `import a.b.c;` declarations.  All modules are stored in `out_reg` and
 * compiled to per-module bytecode chunks.  `opts` provides shared compilation
 * settings (native bindings, debug info flag).  Returns 0 on success and
 * leaves the entry module pointer in `out->entry`; returns -1 on any error
 * (file I/O, cycle, parse, or compile) with diagnostics in `out->errors`. */
OAK_API int
oak_module_loader_load_program(const char* entry_path,
                               oak_compile_options_t* opts,
                               oak_module_registry_t* out_reg,
                               oak_module_loader_result_t* out);

/* Point the first segment of an import at a directory of your choosing.
 *
 * `import <ns>.b.c` from a module under `scope_root` then resolves to
 * `<root_dir>/<ns>/b/c.oak` instead of a path relative to the importing file,
 * and `import <ns>` to `<root_dir>/<ns>.oak` -- the rule the stdlib already
 * follows, applied to a directory you name.  This is how a package manager
 * makes a downloaded library importable without copying it into the project.
 *
 * `scope_root` limits the mount to modules under that directory, so one
 * package's dependencies stay invisible to another's; pass null to apply it
 * program-wide.  Where several mounts claim the same namespace, the one with
 * the most specific scope wins.
 *
 * A miss against a mount is an error, never a fall-through to a
 * module-relative file: once a namespace belongs to a package, a local
 * directory of the same name cannot quietly take over.  For the same reason,
 * mounting over a built-in native module (`io`) is rejected.
 *
 * `label` names the mount's origin in that error -- a package name such as
 * "acme/json" reads better than the cache directory the files happen to sit in.
 * It may be null.
 *
 * Call before oak_module_loader_load_program.  Every string is copied and the
 * directories are canonicalized.  Returns 0, or -1 with the reason recorded on
 * `opts` and reported by the compile, as with the oak_bind_* functions. */
OAK_API int oak_module_loader_mount(oak_compile_options_t* opts,
                                    const char* scope_root,
                                    const char* ns,
                                    const char* root_dir,
                                    const char* label);

#ifdef __cplusplus
}
#endif
