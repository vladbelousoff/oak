/*
 * The module loader: oak_module_loader_load_program.
 *
 * This is the entry point behind the CLI and the WebAssembly playground, and
 * the only one an embedder uses for multi-file programs. The examples exercise
 * it end to end through the CLI; what is checked here is the C API contract
 * itself -- what it returns, what it leaves in the registry and the result, and
 * that both are released without leaking on the failure paths as well as the
 * successful one.
 *
 * Loading is inherently file-backed, so this suite reads the multi-module
 * example from the source tree through OAK_TEST_EXAMPLES_DIR. It writes
 * nothing.
 */

#include "oak_test_support.h"

#include "oak_chunk_impl.h"
#include "oak_module_impl.h"
#include "oak_module_loader.h"
#include "oak_stdlib.h"
#include "oak_type.h"
#include "oak_value.h"

#include <string.h>

OAK_TEST_SUITE(module_loader);

#ifndef OAK_TEST_EXAMPLES_DIR
#define OAK_TEST_EXAMPLES_DIR "examples"
#endif

#define ENTRY_OK OAK_TEST_EXAMPLES_DIR "/06_modules/06_modules.oak"
/* Imports `io`, whose native bindings are declared by an Oak stub in stdlib/.
 * Only loaded here, never run: running it would read a file. */
#define ENTRY_NATIVE OAK_TEST_EXAMPLES_DIR "/08_file_io/08_file_io.oak"

/* Load `path`, run every assertion the caller wants through `fn`, then tear
 * down in the documented order. The fixture's tracking allocator fails the test
 * if any of it leaks. */
typedef struct load_fixture load_fixture_t;
struct load_fixture
{
  oak_compile_options_t opts;
  oak_module_registry_t registry;
  oak_module_loader_result_t result;
  int rc;
};

static void load_begin(load_fixture_t* f, oak_allocator_t* a, const char* path)
{
  memset(&f->result, 0, sizeof f->result);
  oak_compile_options_init(&f->opts, a);
  oak_module_registry_init(&f->registry, a);
  f->rc = oak_module_loader_load_program(
      path, &f->opts, &f->registry, &f->result);
}

/* Same, with the stdlib bound in. Needed by anything that imports a native
 * module: without the bindings the stub is only a set of bodyless
 * declarations and the load is rejected. */
static void load_begin_with_stdlib(load_fixture_t* f,
                                   oak_allocator_t* a,
                                   const char* path)
{
  memset(&f->result, 0, sizeof f->result);
  oak_compile_options_init(&f->opts, a);
  oak_stdlib_register(&f->opts);
  oak_module_registry_init(&f->registry, a);
  f->rc = oak_module_loader_load_program(
      path, &f->opts, &f->registry, &f->result);
}

static void load_end(load_fixture_t* f)
{
  oak_module_registry_free(&f->registry);
  oak_compile_options_free(&f->opts);
}

/* The registry is keyed by path and id, not by dotted name, so a test that
 * wants a named dependency looks it up the long way. */
static oak_module_t* find_module(const oak_module_registry_t* reg,
                                 const char* dotted)
{
  oak_module_t* const* mods = OAK_DATA(oak_module_t*, reg->modules);
  for (usize i = 0; i < oak_size(reg->modules); ++i)
  {
    const char* name = oak_module_dotted_name(mods[i]);
    if (name && strcmp(name, dotted) == 0)
      return mods[i];
  }
  return OAK_NULL;
}

/* The export lookups and the type-registry lookup are internal to the library
 * and not in its export table, so a test that links against it walks the
 * payload vectors itself. Each is a plain vector keyed by the entry's name. */
static const oak_module_export_record_t* find_export_record(
    const oak_module_t* mod, const char* name)
{
  const oak_module_export_record_t* records =
      OAK_CDATA(oak_module_export_record_t, mod->exports.records);
  for (usize i = 0; i < oak_size(mod->exports.records); ++i)
    if (records[i].name && strcmp(records[i].name, name) == 0)
      return &records[i];
  return OAK_NULL;
}

static const oak_module_export_enum_t* find_export_enum(
    const oak_module_t* mod, const char* name)
{
  const oak_module_export_enum_t* enums =
      OAK_CDATA(oak_module_export_enum_t, mod->exports.enums);
  for (usize i = 0; i < oak_size(mod->exports.enums); ++i)
    if (enums[i].name && strcmp(enums[i].name, name) == 0)
      return &enums[i];
  return OAK_NULL;
}

static oak_type_id_t find_type_id(const oak_module_t* mod, const char* name)
{
  const oak_type_entry_t* entries =
      OAK_CDATA(oak_type_entry_t, mod->types.entries);
  for (usize i = 0; i < oak_size(mod->types.entries); ++i)
    if (entries[i].name && strcmp(entries[i].name, name) == 0)
      return entries[i].id;
  return OAK_TYPE_VOID;
}

static const oak_module_export_record_method_t* find_method(
    const oak_module_export_record_t* rec, const char* name)
{
  const oak_module_export_record_method_t* methods =
      OAK_CDATA(oak_module_export_record_method_t, rec->methods);
  for (usize i = 0; i < oak_size(rec->methods); ++i)
    if (strcmp(methods[i].name, name) == 0)
      return &methods[i];
  return OAK_NULL;
}

/* The success path: every reachable module is loaded, compiled, and reachable
 * from the registry by id and by path. */
UTEST_F(module_loader, loads_an_entry_and_its_imports)
{
  load_fixture_t f;
  load_begin(&f, OAK_A, ENTRY_OK);

  EXPECT_EQ(0, f.rc);
  EXPECT_EQ(0, f.result.error_count);
  ASSERT_TRUE(f.result.entry != OAK_NULL);

  /* The entry compiled and is marked as the entry. */
  EXPECT_TRUE(oak_module_chunk(f.result.entry) != OAK_NULL);
  EXPECT_TRUE(oak_module_is_entry(f.result.entry));

  /* Its imports were pulled in too: the example imports analytics.stats and
   * domain.project, so the registry holds more than the entry alone. */
  EXPECT_GT(oak_size(f.registry.modules), (usize)1);

  /* Every module in the registry round-trips through the id lookup, and each
   * one that compiled has a chunk. */
  oak_module_t* const* mods = OAK_DATA(oak_module_t*, f.registry.modules);
  for (usize i = 0; i < oak_size(f.registry.modules); ++i)
  {
    oak_module_t* m = mods[i];
    ASSERT_TRUE(m != OAK_NULL);
    /* oak_module_t is opaque, so compare the handles with EXPECT_TRUE:
     * EXPECT_EQ would have utest try to print an incomplete type. */
    EXPECT_TRUE(m == oak_module_registry_get(&f.registry, oak_module_id(m)));
    EXPECT_TRUE(oak_module_path(m) != OAK_NULL);
    EXPECT_TRUE(m == oak_module_registry_find_by_path(&f.registry,
                                                      oak_module_path(m)));
  }

  load_end(&f);
}

/* Only the entry module is the entry, however many modules get loaded. */
UTEST_F(module_loader, exactly_one_module_is_the_entry)
{
  load_fixture_t f;
  load_begin(&f, OAK_A, ENTRY_OK);
  ASSERT_EQ(0, f.rc);

  int entries = 0;
  oak_module_t* const* mods = OAK_DATA(oak_module_t*, f.registry.modules);
  for (usize i = 0; i < oak_size(f.registry.modules); ++i)
    entries += oak_module_is_entry(mods[i]) ? 1 : 0;
  EXPECT_EQ(1, entries);

  load_end(&f);
}

/* An import pulls the named module into the registry under its dotted name,
 * compiled, and records the dependency on the importer. */
UTEST_F(module_loader, an_import_loads_the_named_module_once)
{
  load_fixture_t f;
  load_begin_with_stdlib(&f, OAK_A, ENTRY_NATIVE);
  ASSERT_EQ(0, f.rc);
  ASSERT_EQ(0, f.result.error_count);
  ASSERT_TRUE(f.result.entry != OAK_NULL);

  int io_modules = 0;
  oak_module_t* const* mods = OAK_DATA(oak_module_t*, f.registry.modules);
  for (usize i = 0; i < oak_size(f.registry.modules); ++i)
  {
    const char* name = oak_module_dotted_name(mods[i]);
    io_modules += (name && strcmp(name, "io") == 0) ? 1 : 0;
  }
  EXPECT_EQ(1, io_modules);

  const oak_module_t* io = find_module(&f.registry, "io");
  ASSERT_TRUE(io != OAK_NULL);
  EXPECT_TRUE(oak_module_chunk(io) != OAK_NULL);
  EXPECT_FALSE(oak_module_is_entry(io));

  /* The entry names io as a direct dependency. */
  const u16* deps = OAK_CDATA(u16, f.result.entry->import_modules);
  int names_io = 0;
  for (usize i = 0; i < oak_size(f.result.entry->import_modules); ++i)
    names_io += (deps[i] == oak_module_id(io)) ? 1 : 0;
  EXPECT_EQ(1, names_io);

  load_end(&f);
}

/* A native module declared by an Oak stub ends up with the bound C functions
 * in its constant pool -- not the bodyless placeholders the stub compiled to.
 *
 * This is the load-time half of the contract the VM depends on: a placeholder
 * is an ordinary bytecode function whose code offset is 0, so calling one does
 * not fail, it jumps to the top of the module's chunk (a bare OP_HALT) and
 * ends the whole program with a success status and no output. */
UTEST_F(module_loader, a_native_module_stub_binds_every_method_to_its_impl)
{
  load_fixture_t f;
  load_begin_with_stdlib(&f, OAK_A, ENTRY_NATIVE);
  ASSERT_EQ(0, f.rc);
  ASSERT_EQ(0, f.result.error_count);

  const oak_module_t* io = find_module(&f.registry, "io");
  ASSERT_TRUE(io != OAK_NULL);
  const oak_chunk_t* chunk = oak_module_chunk(io);
  ASSERT_TRUE(chunk != OAK_NULL);

  const oak_module_export_record_t* file =
      find_export_record(io, "File");
  ASSERT_TRUE(file != OAK_NULL);
  ASSERT_GT(oak_size(file->methods), (usize)0);

  const oak_module_export_record_method_t* methods =
      OAK_CDATA(oak_module_export_record_method_t, file->methods);
  for (usize i = 0; i < oak_size(file->methods); ++i)
  {
    const oak_module_export_record_method_t* me = &methods[i];
    ASSERT_LT((usize)me->const_idx, oak_size(chunk->constants));
    const oak_value_t v = oak_chunk_constant(chunk, (usize)me->const_idx);
    EXPECT_TRUE(oak_is_native_fn(v));
    if (!oak_is_native_fn(v))
      continue;
    const oak_obj_native_fn_t* native = oak_as_native_fn(v);
    /* The callback is reached through the receiver's descriptor, and its
     * arity is what the VM checks the call against. */
    EXPECT_TRUE(native->fn != OAK_NULL);
    EXPECT_TRUE(native->self_type != OAK_NULL);
    EXPECT_EQ((usize)me->arity, native->arity);
  }

  /* The static method takes no implicit self; the instance methods do. */
  const oak_module_export_record_method_t* open = find_method(file, "open");
  const oak_module_export_record_method_t* read_all =
      find_method(file, "read_all");
  ASSERT_TRUE(open != OAK_NULL);
  ASSERT_TRUE(read_all != OAK_NULL);
  EXPECT_EQ(1, open->is_static);
  EXPECT_EQ(2, open->arity);
  EXPECT_EQ(0, read_all->is_static);
  EXPECT_EQ(1, read_all->arity);

  load_end(&f);
}

/* The signatures those bindings carry are lowered against the stub module's
 * own type registry. A descriptor that never got an id would silently lower
 * to void, so an importer would see `File.open` return nothing. */
UTEST_F(module_loader, a_native_module_stub_lowers_signatures_to_its_own_types)
{
  load_fixture_t f;
  load_begin_with_stdlib(&f, OAK_A, ENTRY_NATIVE);
  ASSERT_EQ(0, f.rc);

  oak_module_t* io = find_module(&f.registry, "io");
  ASSERT_TRUE(io != OAK_NULL);

  const oak_type_id_t file_id = find_type_id(io, "File");
  EXPECT_NE(OAK_TYPE_VOID, file_id);
  EXPECT_GE(file_id, (oak_type_id_t)OAK_TYPE_FIRST_USER);

  const oak_module_export_record_t* file =
      find_export_record(io, "File");
  ASSERT_TRUE(file != OAK_NULL);
  const oak_module_export_record_method_t* open = find_method(file, "open");
  ASSERT_TRUE(open != OAK_NULL);
  EXPECT_EQ(file_id, open->return_type.id);

  /* open(path : string, mode : FileMode): the enum parameter resolves to the
   * same module's enum type, not to a bare integer. */
  const oak_type_id_t mode_id = find_type_id(io, "FileMode");
  EXPECT_NE(OAK_TYPE_VOID, mode_id);
  ASSERT_TRUE(open->param_types != OAK_NULL);
  EXPECT_EQ((oak_type_id_t)OAK_TYPE_STRING, open->param_types[0].id);
  EXPECT_EQ(mode_id, open->param_types[1].id);

  load_end(&f);
}

/* An enum bound into a native module is exported with its variants in
 * declaration order, which is what the ordinals in the bytecode mean. */
UTEST_F(module_loader, a_native_module_exports_its_enum_variants)
{
  load_fixture_t f;
  load_begin_with_stdlib(&f, OAK_A, ENTRY_NATIVE);
  ASSERT_EQ(0, f.rc);

  const oak_module_t* io = find_module(&f.registry, "io");
  ASSERT_TRUE(io != OAK_NULL);
  const oak_module_export_enum_t* mode =
      find_export_enum(io, "FileMode");
  ASSERT_TRUE(mode != OAK_NULL);
  ASSERT_EQ((usize)3, oak_size(mode->variants));

  const oak_module_export_enum_variant_t* variants =
      OAK_CDATA(oak_module_export_enum_variant_t, mode->variants);
  static const char* const expected[] = { "Read", "Write", "Append" };
  for (usize i = 0; i < OAK_COUNT_OF(expected); ++i)
  {
    EXPECT_STREQ(expected[i], variants[i].name);
    EXPECT_EQ((int)i, variants[i].value);
  }

  load_end(&f);
}

/* A native module is only reachable through the import that names it: the
 * bindings must not also register a bare `File` in every compilation unit. */
UTEST_F(module_loader, a_native_module_type_is_not_in_scope_without_the_import)
{
  oak_compile_options_t opts;
  oak_compile_options_init(&opts, OAK_A);
  oak_stdlib_register(&opts);

  oak_run_result_t r = oak_test_source_opts(
      OAK_A, "let f = File.open('x', FileMode.Read);", &opts);
  EXPECT_FALSE(r.compiled);
  EXPECT_GT(r.error_count, 0);

  oak_compile_options_free(&opts);
}

/* A missing entry file fails with a diagnostic rather than a crash or a
 * silent success, and everything allocated along the way is still released. */
UTEST_F(module_loader, a_missing_entry_file_reports_and_frees_cleanly)
{
  load_fixture_t f;
  load_begin(&f, OAK_A, OAK_TEST_EXAMPLES_DIR "/does_not_exist_xyz.oak");

  EXPECT_EQ(-1, f.rc);
  EXPECT_TRUE(f.result.entry == OAK_NULL);
  ASSERT_GT(f.result.error_count, 0);
  EXPECT_LE(f.result.error_count, OAK_MAX_DIAGNOSTICS);
  /* The message names something; an empty diagnostic would be useless. */
  EXPECT_TRUE(f.result.errors[0].message[0] != '\0');

  load_end(&f);
}

/* A registry that was initialized but never successfully loaded still frees
 * cleanly -- the CLI takes this path whenever a program fails to load. */
UTEST_F(module_loader, an_empty_registry_frees_cleanly)
{
  oak_module_registry_t reg;
  oak_module_registry_init(&reg, OAK_A);
  EXPECT_EQ((usize)0, oak_size(reg.modules));
  EXPECT_TRUE(oak_module_registry_get(&reg, 0) == OAK_NULL);
  EXPECT_TRUE(oak_module_registry_find_by_path(&reg, "nope") == OAK_NULL);
  oak_module_registry_free(&reg);
}

/* Accessors are total: a null module answers rather than dereferencing. */
UTEST_F(module_loader, accessors_tolerate_a_null_module)
{
  EXPECT_TRUE(oak_module_chunk(OAK_NULL) == OAK_NULL);
  EXPECT_TRUE(oak_module_dotted_name(OAK_NULL) == OAK_NULL);
  EXPECT_TRUE(oak_module_path(OAK_NULL) == OAK_NULL);
  EXPECT_EQ(OAK_MODULE_ID_NONE, oak_module_id(OAK_NULL));
  EXPECT_EQ(0, oak_module_is_entry(OAK_NULL));
}
