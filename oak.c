#include "oak_allocator.h"
#include "oak_bind.h"
#include "oak_cli.h"
#include "oak_compiler.h"
#include "oak_debugger.h"
#include "oak_diagnostic.h"
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

  /* stdio buffers a pipe fully, so a piped long-running script shows nothing
   * until it exits and loses whatever is still buffered if it is killed. This
   * opts out of that, at a write syscall per print. A debug session does the
   * same on its own -- see oak_debugger_init(). */
  if (cli.unbuffered)
    setvbuf(stdout, OAK_NULL, _IONBF, 0);

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
  oak_diagnostics_print(lr.errors, lr.error_count);

  if (load_rc == 0 && lr.entry && oak_module_chunk(lr.entry))
  {
    exit_code = 0;
    if (cli.disassemble)
    {
      oak_module_t* const* modules =
          OAK_DATA(oak_module_t*, registry.modules);
      for (usize i = 0; i < oak_size(registry.modules); ++i)
      {
        const oak_module_t* m = modules[i];
        OAK_LOG(OAK_LOG_INFO,
                "==== module [%s] ====",
                oak_module_dotted_name(m) ? oak_module_dotted_name(m) : "<entry>");
        oak_chunk_disassemble(oak_module_chunk(m));
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
              ? oak_dap_serve(&debugger, &vm, oak_module_chunk(lr.entry),
                              cli.debug_port_set ? cli.debug_port : 4711)
              : oak_vm_run(&vm, oak_module_chunk(lr.entry));
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
