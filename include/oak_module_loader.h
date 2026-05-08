#pragma once

#include "oak_bind.h"
#include "oak_diagnostic.h"
#include "oak_module.h"

struct oak_module_loader_result_t
{
  /* Resolved entry module. May be null on failure. */
  struct oak_module_t* entry;
  /* Aggregated diagnostics from any module's parse/compile. */
  struct oak_diagnostic_t errors[OAK_MAX_DIAGNOSTICS];
  int error_count;
};

/* Loads `entry_path` and recursively loads every module reachable through
 * `import a.b.c;` declarations.  All modules are stored in `out_reg` and
 * compiled to per-module bytecode chunks.  `opts` provides shared compilation
 * settings (native bindings, debug info flag).  Returns 0 on success and
 * leaves the entry module pointer in `out->entry`; returns -1 on any error
 * (file I/O, cycle, parse, or compile) with diagnostics in `out->errors`. */
int oak_module_loader_load_program(const char* entry_path,
                                   struct oak_compile_options_t* opts,
                                   struct oak_module_registry_t* out_reg,
                                   struct oak_module_loader_result_t* out);
