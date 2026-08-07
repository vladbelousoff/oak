#include <emscripten/emscripten.h>

#include "oak_allocator.h"
#include "oak_bind.h"
#include "oak_log.h"
#include "oak_module.h"
#include "oak_module_loader.h"
#include "oak_stdlib.h"
#include "oak_vm.h"

int oak_run(const char* code);

EMSCRIPTEN_KEEPALIVE
int oak_run_wrapper(const char* code)
{
  return oak_run(code);
}

EMSCRIPTEN_KEEPALIVE
int oak_run_file_wrapper(const char* path)
{
  struct oak_allocator_t allocator;
  oak_tracking_allocator_init(&allocator);

  struct oak_compile_options_t compile_opts;
  oak_compile_options_init(&compile_opts, &allocator);
  compile_opts.source_name = path;
  compile_opts.emit_debug_info = 1;
  /* The playground's virtual filesystem holds only the example sources, so
   * stdlib modules like `io` have no stub to load and must be synthesized
   * from the registered native bindings. */
  compile_opts.allow_synthetic_native_modules = 1;
  oak_stdlib_register(&compile_opts);

  struct oak_module_registry_t registry;
  oak_module_registry_init(&registry, &allocator);
  struct oak_module_loader_result_t lr = { 0 };

  int exit_code = 1;
  const int load_rc =
      oak_module_loader_load_program(path, &compile_opts, &registry, &lr);

  for (int i = 0; i < lr.error_count; i++)
  {
    const struct oak_diagnostic_t* d = &lr.errors[i];
    if (d->line > 0)
      oak_log(OAK_LOG_ERROR, "%d:%d: %s", d->line, d->column, d->message);
    else
      oak_log(OAK_LOG_ERROR, "%s", d->message);
  }

  if (load_rc == 0 && lr.entry && lr.entry->chunk)
  {
    struct oak_vm_t vm;
    oak_vm_init(&vm, &allocator);
    oak_vm_set_module_registry(&vm, &registry);
    exit_code = oak_vm_run(&vm, lr.entry->chunk) != OAK_VM_OK;
    oak_vm_free(&vm);
  }

  oak_module_registry_free(&registry);
  oak_compile_options_free(&compile_opts);
  if (allocator.shutdown(&allocator) > 0 && exit_code == 0)
    exit_code = 2;
  return exit_code;
}
