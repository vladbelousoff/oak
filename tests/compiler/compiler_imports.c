#include "oak_bind.h"
#include "oak_compiler.h"
#include "oak_module.h"
#include "oak_module_loader.h"
#include "oak_stdlib.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* These tests build a tiny on-disk module graph and drive it through the real
 * module loader. They cover the import path that uses the per-compiler
 * pre-import snapshot counters (formerly file-scope statics): loading the same
 * graph twice in one process must behave identically, and a name exported by
 * two different modules must be reported as an import collision. */

static void write_file(const char* path, const char* contents)
{
  FILE* f = fopen(path, "wb");
  if (f)
  {
    fwrite(contents, 1, strlen(contents), f);
    fclose(f);
  }
}

/* Load and run a module program. Returns 0 only when every module loaded,
 * compiled, and the entry chunk ran to OAK_VM_OK. */
static int load_and_run(const char* entry_path)
{
  struct oak_allocator_t* a = oak_test_allocator();

  struct oak_compile_options_t opts;
  oak_compile_options_init(&opts, a);
  opts.source_name = entry_path;
  opts.emit_debug_info = 0;
  oak_stdlib_register(&opts);

  struct oak_module_registry_t registry;
  oak_module_registry_init(&registry, a);
  struct oak_module_loader_result_t lr = { 0 };

  const int load_rc =
      oak_module_loader_load_program(entry_path, &opts, &registry, &lr);

  int rc = -1;
  if (load_rc == 0 && lr.entry && lr.entry->chunk)
  {
    struct oak_vm_t vm;
    oak_vm_init(&vm, a);
    oak_vm_set_module_registry(&vm, &registry);
    rc = oak_vm_run(&vm, lr.entry->chunk) == OAK_VM_OK ? 0 : -1;
    oak_vm_free(&vm);
  }

  oak_module_registry_free(&registry);
  oak_compile_options_free(&opts);
  return rc;
}

/* Create a fresh temp directory with a lib/ subdir. Returns 0 on success and
 * writes the directory path into `dir`. */
static int make_module_dir(char* dir, usize dir_size)
{
  char templ[] = "/tmp/oak_imports_XXXXXX";
  if (!mkdtemp(templ))
    return -1;
  snprintf(dir, dir_size, "%s", templ);
  char lib[512];
  snprintf(lib, sizeof(lib), "%s/lib", dir);
  if (mkdir(lib, 0700) != 0)
    return -1;
  return 0;
}

OAK_TEST_DECL(ImportLoadRunsAndIsRepeatable)
{
  char dir[256];
  OAK_CHECK(make_module_dir(dir, sizeof(dir)) == 0);

  char main_path[512];
  char lib_path[512];
  snprintf(main_path, sizeof(main_path), "%s/main.oak", dir);
  snprintf(lib_path, sizeof(lib_path), "%s/lib/math.oak", dir);

  write_file(lib_path, "fn triple(n : number) -> number {\n"
                       "  return n * 3;\n"
                       "}\n");
  write_file(main_path, "import { triple } from lib.math;\n"
                        "let r = triple(7);\n");

  /* Loading the same graph twice in one process must produce the same result;
   * a residual cross-compile import counter would break the second run. */
  OAK_CHECK(load_and_run(main_path) == 0);
  OAK_CHECK(load_and_run(main_path) == 0);

  remove(lib_path);
  remove(main_path);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(ImportCollisionIsRejected)
{
  char dir[256];
  OAK_CHECK(make_module_dir(dir, sizeof(dir)) == 0);

  char main_path[512];
  char a_path[512];
  char b_path[512];
  snprintf(main_path, sizeof(main_path), "%s/main.oak", dir);
  snprintf(a_path, sizeof(a_path), "%s/lib/a.oak", dir);
  snprintf(b_path, sizeof(b_path), "%s/lib/b.oak", dir);

  /* Two different modules each export a record named Shared. */
  write_file(a_path, "record Shared {\n  x : number;\n}\n");
  write_file(b_path, "record Shared {\n  y : number;\n}\n");
  write_file(main_path, "import * from lib.a;\n"
                        "import * from lib.b;\n");

  /* Wildcard-importing both must be rejected as an import collision. */
  OAK_CHECK(load_and_run(main_path) != 0);

  remove(a_path);
  remove(b_path);
  remove(main_path);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(CrossModuleTraitDispatch)
{
  char dir[256];
  OAK_CHECK(make_module_dir(dir, sizeof(dir)) == 0);

  char main_path[512];
  char trait_path[512];
  snprintf(main_path, sizeof(main_path), "%s/main.oak", dir);
  snprintf(trait_path, sizeof(trait_path), "%s/lib/shape.oak", dir);

  /* The trait is defined in one module and the concrete impl + dynamic
   * dispatch (direct call and through a Shape[] array) live in another. */
  write_file(trait_path, "trait Shape {\n"
                         "  fn area(self) -> number;\n"
                         "}\n");
  write_file(main_path,
             "import { Shape } from lib.shape;\n"
             "record Circle { radius : number; }\n"
             "fn Circle.area(self) -> number { return self.radius * self.radius; }\n"
             "fn use_shape(s : Shape) -> number { return s.area(); }\n"
             "let mut c = new Circle { radius : 4 };\n"
             "print(use_shape(c));\n"
             "let mut shapes = [] as Shape[];\n"
             "shapes.push(c);\n"
             "print(shapes[0].area());\n");

  OAK_CHECK(load_and_run(main_path) == 0);

  remove(trait_path);
  remove(main_path);
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(ImportLoadRunsAndIsRepeatable),
    OAK_TEST_ENTRY(ImportCollisionIsRejected),
    OAK_TEST_ENTRY(CrossModuleTraitDispatch),
  };
  return oak_test_run(tests, sizeof(tests) / sizeof(tests[0]));
}
