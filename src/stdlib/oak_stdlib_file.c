#include "oak_stdlib_file.h"

#include "oak_allocator.h"
#include "oak_bind.h"
#include "oak_count_of.h"
#include "oak_value_impl.h"
#include "oak_vm.h"

#include <stdio.h>
#include <string.h>

/* FileMode variant integer values. Must match the order/values registered in
 * oak_stdlib_register_file. */
typedef enum oak_file_mode oak_file_mode_t;
enum oak_file_mode
{
  OAK_FILE_MODE_READ = 0,
  OAK_FILE_MODE_WRITE = 1,
  OAK_FILE_MODE_APPEND = 2,
};

typedef struct oak_file_handle oak_file_handle_t;
struct oak_file_handle
{
  FILE* fp; /* null after close() */
  /* The mode the stream was opened in. eof() needs it because a write-only
   * stream has no readable position to compare against. */
  oak_file_mode_t mode;
  /* Kept so the destructor can free the handle without a native ctx. */
  oak_allocator_t* allocator;
};

/* Runs when the File record's refcount hits zero: closes a still-open
 * stream (file dropped without close()) and frees the handle. close()
 * only nulls fp so the record can outlive it safely. */
static void file_destroy(void* instance)
{
  oak_file_handle_t* h = instance;
  if (!h)
    return;
  if (h->fp)
    fclose(h->fp);
  OAK_FREE(h->allocator, h);
}

/* Runtime guard for open(). The compile-time signature is built in
 * oak_stdlib_register_file, where the io.FileMode descriptor is in scope and
 * the mode parameter is typed as that enum via OAK_BIND_ENUM. At run time an
 * enum is just its integer value, so a number check is the right test here. */
static const oak_bind_type_ref_t file_open_params[] = {
  OAK_BIND_SCALAR_INIT(OAK_TYPE_STRING),
  OAK_BIND_SCALAR_INIT(OAK_TYPE_NUMBER),
};

/* Declared once and used twice: as param_types, so the compiler checks Oak
 * call sites, and as the callback's own guard against calls arriving from C. */
static const oak_bind_type_ref_t file_write_params[] = {
  OAK_BIND_SCALAR_INIT(OAK_TYPE_STRING),
};

static oak_fn_call_result_t file_open(oak_native_ctx_t* ctx,
                                           const oak_value_t* args,
                                           int argc,
                                           oak_value_t* out)
{
  if (!oak_native_args_match(
          args, argc, file_open_params, (int)oak_count_of(file_open_params)))
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (!oak_is_i32(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const char* mode;
  const oak_file_mode_t requested = (oak_file_mode_t)oak_as_i32(args[1]);
  switch (requested)
  {
    case OAK_FILE_MODE_READ:
      mode = "r";
      break;
    case OAK_FILE_MODE_WRITE:
      mode = "w";
      break;
    case OAK_FILE_MODE_APPEND:
      mode = "a";
      break;
    default:
      return OAK_FN_CALL_RUNTIME_ERROR;
  }
  FILE* fp = fopen(oak_as_cstring(args[0]), mode);
  if (!fp)
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_file_handle_t* h = OAK_ALLOC(ctx->allocator, sizeof *h);
  if (!h)
  {
    fclose(fp);
    return OAK_FN_CALL_RUNTIME_ERROR;
  }
  h->fp = fp;
  h->mode = requested;
  h->allocator = ctx->allocator;
  /* The io.File descriptor travels through user_data rather than a file
   * static, so two oak_compile_options_t in one process stay independent. */
  *out = oak_vm_native_record_new(
      ctx->vm, (const oak_bind_type_t*)ctx->user_data, h);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t file_read(oak_native_ctx_t* ctx,
                                           const oak_value_t* args,
                                           int argc,
                                           oak_value_t* out)
{
  if (argc != 1 || !oak_is_native_record(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_file_handle_t* h = oak_native_instance(args[0]);
  if (!h || !h->fp)
    return OAK_FN_CALL_RUNTIME_ERROR;
  char buf[4096];
  if (!fgets(buf, sizeof buf, h->fp))
  {
    oak_obj_string_t* s = oak_vm_string_new_len(ctx->vm, "", 0);
    *out = OAK_VALUE_OBJ(&s->obj);
    return OAK_FN_CALL_OK;
  }
  const usize len = strlen(buf);
  oak_obj_string_t* s = oak_vm_string_new_len(ctx->vm, buf, len);
  *out = OAK_VALUE_OBJ(&s->obj);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t file_read_all(oak_native_ctx_t* ctx,
                                               const oak_value_t* args,
                                               int argc,
                                               oak_value_t* out)
{
  if (argc != 1 || !oak_is_native_record(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_file_handle_t* h = oak_native_instance(args[0]);
  if (!h || !h->fp)
    return OAK_FN_CALL_RUNTIME_ERROR;
  FILE* const f = h->fp;
  const long pos = ftell(f);
  if (pos < 0)
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (fseek(f, 0, SEEK_END) != 0)
    return OAK_FN_CALL_RUNTIME_ERROR;
  const long end = ftell(f);
  if (end < 0 || fseek(f, pos, SEEK_SET) != 0)
    return OAK_FN_CALL_RUNTIME_ERROR;
  const size_t n = (size_t)(end - pos);
  if (n == 0)
  {
    oak_obj_string_t* s = oak_vm_string_new_len(ctx->vm, "", 0);
    *out = OAK_VALUE_OBJ(&s->obj);
    return OAK_FN_CALL_OK;
  }
  char* buf = OAK_ALLOC(ctx->allocator, n + 1u);
  if (!buf)
    return OAK_FN_CALL_RUNTIME_ERROR;
  const size_t got = fread(buf, 1u, n, f);
  buf[got] = '\0';
  oak_obj_string_t* s = oak_vm_string_new_len(ctx->vm, buf, got);
  OAK_FREE(ctx->allocator, buf);
  *out = OAK_VALUE_OBJ(&s->obj);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t file_write(oak_native_ctx_t* ctx,
                                            const oak_value_t* args,
                                            int argc,
                                            oak_value_t* out)
{
  (void)ctx;
  /* args[0] is the receiver; param_types covers only explicit parameters. */
  if (argc != 2 || !oak_is_native_record(args[0]) ||
      !oak_native_args_match(args + 1,
                             argc - 1,
                             file_write_params,
                             (int)oak_count_of(file_write_params)))
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_file_handle_t* h = oak_native_instance(args[0]);
  if (!h || !h->fp)
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (fputs(oak_as_cstring(args[1]), h->fp) == EOF)
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t file_eof(oak_native_ctx_t* ctx,
                                          const oak_value_t* args,
                                          int argc,
                                          oak_value_t* out)
{
  (void)ctx;
  if (argc != 1 || !oak_is_native_record(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_file_handle_t* h = oak_native_instance(args[0]);
  if (!h || !h->fp)
    return OAK_FN_CALL_RUNTIME_ERROR;

  /* Reports whether any data remains to be read, rather than whether the
   * stream's EOF indicator happens to be set.
   *
   * C only raises that indicator on a read that *attempts* to go past the end.
   * file_read_all() measures the remaining length and reads exactly that many
   * bytes, so it never triggers it -- yet on Windows the file is open in text
   * mode and CRLF translation makes the sized read come up short, which does
   * set it. That made eof() answer differently per platform after the very
   * same sequence of calls. Comparing the position against the end is
   * deterministic everywhere.
   *
   * That comparison only means anything for a readable stream, though. A
   * write-only handle has no readable content at all, so the answer is a flat
   * "nothing remains" -- and it has to be stated rather than derived, because
   * the position it would be derived from is not portable: C leaves the
   * initial file position for append mode unspecified, and the two CRTs
   * disagree in practice (glibc opens "a" positioned at end-of-file, the
   * Microsoft CRT at offset 0). Deriving it would put exactly the platform
   * split this function exists to remove back into append mode. Answering
   * "true" also keeps `while !f.eof()` from spinning forever on a handle it
   * can never read from. */
  FILE* const f = h->fp;
  if (h->mode != OAK_FILE_MODE_READ)
  {
    *out = OAK_VALUE_BOOL(1);
    return OAK_FN_CALL_OK;
  }

  if (feof(f))
  {
    *out = OAK_VALUE_BOOL(1);
    return OAK_FN_CALL_OK;
  }

  const long pos = ftell(f);
  if (pos < 0)
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (fseek(f, 0, SEEK_END) != 0)
    return OAK_FN_CALL_RUNTIME_ERROR;
  const long end = ftell(f);
  if (end < 0 || fseek(f, pos, SEEK_SET) != 0)
    return OAK_FN_CALL_RUNTIME_ERROR;

  *out = OAK_VALUE_BOOL(pos >= end);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t file_close(oak_native_ctx_t* ctx,
                                            const oak_value_t* args,
                                            int argc,
                                            oak_value_t* out)
{
  if (argc != 1 || !oak_is_native_record(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  (void)ctx;
  oak_file_handle_t* h = oak_native_instance(args[0]);
  if (!h || !h->fp)
    return OAK_FN_CALL_RUNTIME_ERROR;
  fclose(h->fp);
  /* The handle stays alive (freed by file_destroy) so that read/write/close
   * on an already-closed File fail cleanly instead of touching freed memory. */
  h->fp = null;
  *out = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

void oak_stdlib_register_file(oak_compile_options_t* opts)
{
  if (!opts)
    return;
  oak_bind_type_t* t =
      oak_bind_type_in_module(opts, "io", OAK_BIND_TYPE_RECORD, "File");
  if (!t)
    return;
  t->destructor = file_destroy;

  oak_bind_enum_t* mode = oak_bind_enum_in_module(opts, "io", "FileMode");
  if (mode)
  {
    static const oak_bind_enum_variant_t modes[] = {
      { "Read", OAK_FILE_MODE_READ },
      { "Write", OAK_FILE_MODE_WRITE },
      { "Append", OAK_FILE_MODE_APPEND },
    };
    oak_bind_enum_variants(mode, modes, (int)oak_count_of(modes));
  }

  /* Not static: these reference `t` and `mode`, known only at run time.
   * Typing the second parameter as the FileMode enum makes the compiler accept
   * `io.open(p, FileMode.Write)` and reject a bare integer. */
  const oak_bind_type_ref_t open_sig[] = {
    OAK_BIND_SCALAR(OAK_TYPE_STRING),
    OAK_BIND_ENUM(mode),
  };
  const oak_bind_global_fn_t globals[] = {
    { .module_name = "io",
      .name = "open",
      .impl = file_open,
      .arity = 2,
      .return_type = OAK_BIND_NATIVE(t),
      .param_types = mode ? open_sig : null,
      .param_count = mode ? (int)oak_count_of(open_sig) : 0,
      .user_data = t },
  };
  oak_bind_fns_global(opts, globals, (int)oak_count_of(globals));

  const oak_bind_fn_t methods[] = {
    { .kind = OAK_BIND_FN_STATIC_METHOD,
      .receiver_type = t,
      .name = "open",
      .impl = file_open,
      .arity = 2,
      .return_type = OAK_BIND_NATIVE(t),
      .param_types = mode ? open_sig : null,
      .param_count = mode ? (int)oak_count_of(open_sig) : 0,
      .user_data = t },
    { .kind = OAK_BIND_FN_INSTANCE_METHOD,
      .receiver_type = t,
      .name = "read",
      .impl = file_read,
      .arity = 0,
      .return_type = OAK_BIND_SCALAR(OAK_TYPE_STRING) },
    { .kind = OAK_BIND_FN_INSTANCE_METHOD,
      .receiver_type = t,
      .name = "read_all",
      .impl = file_read_all,
      .arity = 0,
      .return_type = OAK_BIND_SCALAR(OAK_TYPE_STRING) },
    { .kind = OAK_BIND_FN_INSTANCE_METHOD,
      .receiver_type = t,
      .name = "write",
      .impl = file_write,
      .arity = 1,
      .return_type = OAK_BIND_SCALAR(OAK_TYPE_VOID),
      .param_types = file_write_params,
      .param_count = (int)oak_count_of(file_write_params) },
    { .kind = OAK_BIND_FN_INSTANCE_METHOD,
      .receiver_type = t,
      .name = "eof",
      .impl = file_eof,
      .arity = 0,
      .return_type = OAK_BIND_SCALAR(OAK_TYPE_BOOL) },
    { .kind = OAK_BIND_FN_INSTANCE_METHOD,
      .receiver_type = t,
      .name = "close",
      .impl = file_close,
      .arity = 0,
      .return_type = OAK_BIND_SCALAR(OAK_TYPE_VOID) },
  };
  oak_bind_fns(opts, methods, (int)oak_count_of(methods));
}
