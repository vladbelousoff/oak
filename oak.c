#include "oak_allocator.h"
#include "oak_bind.h"
#include "oak_cli.h"
#include "oak_compiler.h"
#include "oak_log.h"
#include "oak_module.h"
#include "oak_module_loader.h"
#include "oak_stdlib.h"
#include "oak_vm.h"

#include <stdio.h>

int main(const int argc, const char* argv[])
{
  struct oak_cli_args_t cli;
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

  struct oak_allocator_t allocator;
  if (cli.track_memory)
    oak_tracking_allocator_init(&allocator);
  else
    oak_system_allocator_init(&allocator);

  struct oak_compile_options_t compile_opts;
  oak_compile_options_init(&compile_opts, &allocator);
  compile_opts.source_name = cli.script_path;
  compile_opts.emit_debug_info = !cli.no_debug;
  oak_stdlib_register(&compile_opts);

  struct oak_module_registry_t registry;
  oak_module_registry_init(&registry, &allocator);
  struct oak_module_loader_result_t lr = { 0 };

  int exit_code = 1;
  const int load_rc = oak_module_loader_load_program(
      cli.script_path, &compile_opts, &registry, &lr);
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
    exit_code = 0;
    if (cli.disassemble)
    {
      for (int i = 0; i < registry.modules.count; ++i)
      {
        const struct oak_module_t* m = registry.modules.items[i];
        oak_log(OAK_LOG_INFO,
                "==== module [%s] ====",
                m->dotted_name ? m->dotted_name : "<entry>");
        oak_chunk_disassemble(m->chunk);
      }
    }
    else
    {
      struct oak_vm_t vm;
      oak_vm_init(&vm, &allocator);
      oak_vm_set_module_registry(&vm, &registry);
      exit_code = oak_vm_run(&vm, lr.entry->chunk) != OAK_VM_OK;
      oak_vm_free(&vm);
    }
  }

  oak_module_registry_free(&registry);
  oak_compile_options_free(&compile_opts);
  if (allocator.shutdown(&allocator) > 0 && exit_code == 0)
    exit_code = 2;
  return exit_code;
}
