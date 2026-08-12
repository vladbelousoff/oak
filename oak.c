#include "oak_allocator.h"
#include "oak_bind.h"
#include "oak_cli.h"
#include "oak_compiler.h"
#include "oak_debugger.h"
#include "oak_dap.h"
#include "oak_log.h"
#include "oak_module.h"
#include "oak_module_loader.h"
#include "oak_stdlib.h"
#include "oak_vm.h"

#include <mimalloc.h>
#include <stdio.h>

int main(const int argc, const char* argv[])
{
  oak_cli_args_t cli;
  if (oak_cli_parse(argc, argv, &cli) != 0)
  {
    if (cli.error)
      fprintf(stderr, "oak: %s\n", cli.error);
    oak_cli_usage(stderr);
    return 1;
  }

  if (cli.help)
  {
    oak_cli_usage(stdout);
    return 0;
  }

  oak_allocator_t allocator;
  if (cli.track_memory)
    oak_tracking_allocator_init(&allocator);
  else
    oak_allocator_init(&allocator, mi_malloc, mi_realloc, mi_free);

  oak_compile_options_t compile_opts;
  oak_compile_options_init(&compile_opts, &allocator);
  compile_opts.source_name = cli.script_path;
  compile_opts.emit_debug_info = cli.debug || !cli.no_debug_symbols;
  compile_opts.allow_synthetic_native_modules = cli.allow_synthetic_modules;
  oak_stdlib_register(&compile_opts);

  oak_module_registry_t registry;
  oak_module_registry_init(&registry, &allocator);
  oak_module_loader_result_t lr = { 0 };

  int exit_code = 1;
  const int load_rc = oak_module_loader_load_program(
      cli.script_path, &compile_opts, &registry, &lr);
  for (int i = 0; i < lr.error_count; i++)
  {
    const oak_diagnostic_t* d = &lr.errors[i];
    if (d->line > 0)
      oak_log(OAK_LOG_ERROR, "%d:%d: %s", d->line, d->column, d->message);
    else
      oak_log(OAK_LOG_ERROR, "%s", d->message);
  }

  if (load_rc == 0 && lr.entry && lr.entry->chunk)
  {
    exit_code = 0;
    if (cli.disassemble)
    {
      oak_module_t* const* modules =
          OAK_DATA(oak_module_t*, registry.modules);
      for (usize i = 0; i < oak_size(registry.modules); ++i)
      {
        const oak_module_t* m = modules[i];
        oak_log(OAK_LOG_INFO,
                "==== module [%s] ====",
                m->dotted_name ? m->dotted_name : "<entry>");
        oak_chunk_disassemble(m->chunk);
      }
    }
    else
    {
      oak_vm_t vm;
      oak_vm_init(&vm, &allocator);
      oak_vm_set_module_registry(&vm, &registry);
      oak_debugger_t debugger;
      oak_vm_debug_hook_t dbg_hook;
      if (cli.debug)
      {
        oak_debugger_init(&debugger, &allocator);
        dbg_hook.fn = oak_debugger_hook;
        dbg_hook.ctx = &debugger;
        oak_vm_set_debug_hook(&vm, &dbg_hook);
      }
      const oak_vm_result_t vm_result =
          cli.debug
              ? oak_dap_serve(&debugger, &vm, lr.entry->chunk,
                              cli.debug_port_set ? cli.debug_port : 4711)
              : oak_vm_run(&vm, lr.entry->chunk);
      if (vm_result == OAK_VM_DEBUG_HALT)
        exit_code = 130;
      else
        exit_code = vm_result != OAK_VM_OK;
      if (cli.debug)
        oak_debugger_free(&debugger);
      oak_vm_free(&vm);
    }
  }

  oak_module_registry_free(&registry);
  oak_compile_options_free(&compile_opts);
  if (allocator.shutdown(&allocator) > 0 && exit_code == 0)
    exit_code = 2;
  return exit_code;
}
