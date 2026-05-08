#include <emscripten/emscripten.h>

#include "oak_bind.h"
#include "oak_log.h"
#include "oak_mem.h"
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
  oak_mem_init();

  struct oak_compile_options_t compile_opts;
  oak_compile_options_init(&compile_opts);
  compile_opts.source_name = path;
  compile_opts.emit_debug_info = 1;
  oak_stdlib_register(&compile_opts);

  struct oak_module_registry_t registry;
  oak_module_registry_init(&registry);
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
    oak_vm_init(&vm);
    oak_vm_set_module_registry(&vm, &registry);
    exit_code = oak_vm_run(&vm, lr.entry->chunk) != OAK_VM_OK;
    oak_vm_free(&vm);
  }

  oak_module_registry_free(&registry);
  oak_compile_options_free(&compile_opts);
  oak_mem_shutdown();
  return exit_code;
}
