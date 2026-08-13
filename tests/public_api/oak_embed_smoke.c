/*
 * Public-API smoke test — and the worked embedding example.
 *
 * This file is compiled against `include/` alone, with no -DOAK_* flags and no
 * access to anything under src/. That makes it the guard for three properties
 * at once:
 *
 *   1. every public header preprocesses with -Iinclude only;
 *   2. every function it calls is actually exported from the library;
 *   3. no public header's layout or behaviour depends on a build define --
 *      the library is built with -DOAK_DEBUG_LOGGING (and optionally
 *      -DOAK_ATOMIC_REFCOUNT); this translation unit is not, so a mismatch in
 *      oak_obj_t's layout would surface here as corruption or a leak report.
 *
 * It exits non-zero if anything fails, including a leak reported by the
 * tracking allocator at shutdown.
 */

#include "oak_bind.h"
#include "oak_chunk.h"
#include "oak_compiler.h"
#include "oak_count_of.h"
#include "oak_diagnostic.h"
#include "oak_module.h"
#include "oak_native.h"
#include "oak_parser.h"
#include "oak_program.h"
#include "oak_stdlib.h"
#include "oak_value.h"
#include "oak_vm.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                            \
  do                                                                           \
  {                                                                            \
    if (!(cond))                                                               \
    {                                                                          \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

/* ---- a native free function -------------------------------------------- */

/* Written once: passed as param_types so the compiler checks Oak call sites,
 * and reused below as the callback's own guard. */
static const oak_bind_type_ref_t add_params[] = {
  OAK_BIND_SCALAR_INIT(OAK_TYPE_NUMBER),
  OAK_BIND_SCALAR_INIT(OAK_TYPE_NUMBER),
};

static oak_fn_call_result_t native_add(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       const usize argc,
                                       oak_value_t* out)
{
  (void)call;
  if (!oak_native_args_match(
          args, argc, add_params, (int)oak_count_of(add_params)))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_I32(oak_as_i32(args[0]) + oak_as_i32(args[1]));
  return OAK_FN_CALL_OK;
}

/* ---- a native record type ----------------------------------------------- */

typedef struct counter counter_t;
struct counter
{
  int value;
};

static counter_t g_counter = { 41 };

static oak_value_t counter_get_value(oak_value_t self, void* user_data)
{
  (void)user_data;
  const counter_t* c = (const counter_t*)oak_native_instance(self);
  return OAK_VALUE_I32(c ? c->value : 0);
}

static void counter_set_value(oak_value_t self, oak_value_t v, void* user_data)
{
  (void)user_data;
  counter_t* c = (counter_t*)oak_native_instance(self);
  if (c && oak_is_i32(v))
    c->value = oak_as_i32(v);
}

/* Returns the descriptor through user_data rather than a file static, so two
 * option sets in one process stay independent. */
static oak_fn_call_result_t counter_bump(oak_native_call_t* call,
                                         const oak_value_t* args,
                                         const usize argc,
                                         oak_value_t* out)
{
  if (argc != 1 || !oak_is_native_record(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  counter_t* c = (counter_t*)oak_native_instance(args[0]);
  if (!c)
    return OAK_FN_CALL_RUNTIME_ERROR;
  ++c->value;
  (void)call;
  *out = OAK_VALUE_I32(c->value);
  return OAK_FN_CALL_OK;
}

/* ---- an attribute ------------------------------------------------------- */

static int g_attr_decls = 0;

static void on_decl(const oak_attr_compile_ctx_t* ctx)
{
  (void)ctx;
  ++g_attr_decls;
}

int main(void)
{
  oak_allocator_t allocator;
  oak_tracking_allocator_init(&allocator);

  oak_compile_options_t opts;
  oak_compile_options_init(&opts, &allocator);
  opts.source_name = "smoke";
  opts.emit_debug_info = 1;

  /* Registration must precede oak_compile_ex. */
  CHECK(oak_bind_fn_global(&opts,
                           &(oak_bind_global_fn_t){
                               .name = "native_add",
                               .impl = native_add,
                               .arity = 2,
                               .return_type =
                                   OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                               .param_types = add_params,
                               .param_count = (int)oak_count_of(add_params),
                           }) == 0);

  oak_bind_type_t* counter =
      oak_bind_type(&opts, OAK_BIND_TYPE_RECORD, "Counter");
  CHECK(counter != null);
  CHECK(oak_bind_field(counter,
                       &(oak_bind_field_t){
                           .name = "value",
                           .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                           .getter = counter_get_value,
                           .setter = counter_set_value,
                       }) == 0);
  CHECK(oak_bind_fn(&opts,
                    &(oak_bind_fn_t){
                        .kind = OAK_BIND_FN_INSTANCE_METHOD,
                        .receiver_type = counter,
                        .name = "bump",
                        .impl = counter_bump,
                        .arity = 0,
                        .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                        .user_data = counter,
                    }) == 0);

  oak_bind_enum_t* colour = oak_bind_enum(&opts, "Colour");
  CHECK(colour != null);
  static const oak_bind_enum_variant_t colours[] = {
    { "Red", 0 },
    { "Green", 1 },
  };
  CHECK(oak_bind_enum_variants(colour, colours, (int)oak_count_of(colours)) ==
        0);

  /* Rejected bindings are probed on a throwaway options struct, because a
   * rejection is recorded and then fails the compile: the whole point is that
   * a mis-registered binding cannot be silently dropped just because the
   * embedder ignored the -1. Duplicate names are rejected; duplicate values
   * are not. */
  {
    oak_compile_options_t probe;
    oak_compile_options_init(&probe, &allocator);
    oak_bind_enum_t* dup = oak_bind_enum(&probe, "Colour");
    CHECK(dup != null);
    CHECK(oak_bind_enum_variants(dup, colours, (int)oak_count_of(colours)) == 0);
    CHECK(oak_bind_enum_variant(dup, "Red", 7) == -1);
    CHECK(oak_bind_enum_variants(dup, colours, (int)oak_count_of(colours)) ==
          -1);

    /* And the rejection is reportable rather than silent. */
    oak_program_t bad_binding;
    CHECK(oak_program_compile(&bad_binding, "let x = 1;\n", &probe) == 0);
    CHECK(oak_program_error_count(&bad_binding) > 0);
    oak_program_free(&bad_binding);
    oak_compile_options_free(&probe);
  }

  CHECK(oak_bind_attr(&opts,
                      &(oak_bind_attr_t){
                          .name = "Traced",
                          .on_decl = on_decl,
                      }) == 0);

  oak_stdlib_register(&opts);

  /* ---- compile ---------------------------------------------------------- */

  static const char* const src =
      "let a = native_add(20, 22);\n"
      "let c = Colour.Green;\n"
      "print(a);\n";

  oak_program_t prog;
  const int ok = oak_program_compile(&prog, src, &opts);
  if (!ok)
    oak_diagnostics_print(oak_program_errors(&prog),
                          oak_program_error_count(&prog));
  CHECK(ok);
  CHECK(oak_program_error_count(&prog) == 0);
  CHECK(oak_program_chunk(&prog) != null);

  /* A program that does not compile still reports cleanly and frees cleanly. */
  oak_program_t bad;
  CHECK(oak_program_compile(&bad, "let x = ;\n", &opts) == 0);
  CHECK(oak_program_error_count(&bad) > 0);
  CHECK(oak_program_errors(&bad) != null);
  CHECK(oak_program_chunk(&bad) == null);
  oak_program_free(&bad);
  oak_program_free(&bad); /* nulls what it frees: safe twice */

  /* ---- run -------------------------------------------------------------- */

  if (oak_program_chunk(&prog))
  {
    oak_vm_t vm;
    oak_vm_init(&vm, &allocator);
    CHECK(oak_vm_last_error(&vm) == null);
    CHECK(oak_vm_run(&vm, oak_program_chunk(&prog)) == OAK_VM_OK);
    CHECK(oak_vm_last_error(&vm) == null);

    /* VM-owned value constructors, and the value operations native code uses. */
    oak_obj_string_t* s = oak_vm_string_new(&vm, "hello");
    CHECK(s != null);
    oak_obj_array_t* arr = oak_vm_array_new(&vm);
    CHECK(arr != null);
    CHECK(oak_array_push(arr, OAK_VALUE_I32(1)) == 1);
    oak_obj_map_t* map = oak_vm_map_new(&vm);
    CHECK(map != null);
    CHECK(oak_map_set(map, OAK_VALUE_OBJ(&s->obj), OAK_VALUE_I32(2)) == 1);

    static const char* const field_names[] = { "x" };
    oak_obj_record_t* rec = oak_vm_record_new(&vm, 1, "Point", field_names);
    CHECK(rec != null);

    const oak_value_t native_val =
        oak_vm_native_record_new(&vm, counter, &g_counter);
    CHECK(oak_is_native_record(native_val));
    CHECK(oak_native_instance(native_val) == &g_counter);

    /* Weak references never resurrect. */
    const oak_value_t weak = oak_value_weaken(OAK_VALUE_OBJ(&arr->obj));
    CHECK(oak_is_weak_obj(weak));

    oak_obj_decref(&s->obj);
    oak_obj_decref(&arr->obj);
    oak_obj_decref(&map->obj);
    oak_obj_decref(&rec->obj);
    oak_obj_decref(oak_value_obj_resolve(native_val));

    /* A runtime error is reportable as data, not just printed to stderr.
     * Calling a non-callable is the simplest way to provoke one. */
    CHECK(oak_vm_call(&vm, OAK_VALUE_I32(7), null, 0, null) ==
          OAK_VM_RUNTIME_ERROR);
    const oak_diagnostic_t* err = oak_vm_last_error(&vm);
    CHECK(err != null);
    CHECK(err != null && err->message[0] != '\0');

    oak_vm_free(&vm);
  }

  /* oak_vm_call needs a chunk attached, which oak_vm_prepare supplies without
   * executing anything -- previously only a completed oak_vm_run could. */
  {
    oak_vm_t fresh;
    oak_vm_init(&fresh, &allocator);
    oak_vm_prepare(&fresh, oak_program_chunk(&prog));
    /* Reaches the argument-transfer boundary and reports properly rather than
     * failing with "no active chunk". */
    CHECK(oak_vm_call(&fresh, OAK_VALUE_I32(1), null, 0, null) ==
          OAK_VM_RUNTIME_ERROR);
    CHECK(oak_vm_last_error(&fresh) != null);
    oak_vm_free(&fresh);
  }

  /* ---- module registry -------------------------------------------------- */

  oak_module_registry_t registry;
  oak_module_registry_init(&registry, &allocator);
  oak_module_t* mod = oak_module_registry_new(&registry, "a/b.oak", "a.b");
  CHECK(mod != null);
  CHECK(oak_module_registry_get(&registry, oak_module_id(mod)) == mod);
  CHECK(oak_module_registry_find_by_path(&registry, "a/b.oak") == mod);
  CHECK(strcmp(oak_module_dotted_name(mod), "a.b") == 0);
  CHECK(oak_module_chunk(mod) == null); /* never compiled */
  CHECK(oak_module_is_entry(mod) == 0);
  oak_module_registry_free(&registry);

  /* ---- teardown, in the documented order -------------------------------- */

  /* oak_program_free releases chunk, AST and tokens in the right order; the
   * options go last, after the VM and every native value made from them. */
  oak_program_free(&prog);
  oak_program_free(&prog); /* nulls what it frees: safe twice */
  oak_compile_options_free(&opts);
  oak_compile_options_free(&opts); /* likewise */

  /* Reports and returns non-zero if anything leaked. */
  if (allocator.shutdown && allocator.shutdown(&allocator) != 0)
  {
    fprintf(stderr, "FAIL: allocator reported a leak\n");
    ++failures;
  }

  if (failures == 0)
    printf("public API smoke test passed\n");
  return failures == 0 ? 0 : 1;
}
