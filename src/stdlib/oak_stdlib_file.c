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
};

/* Runs when the File record's refcount hits zero: closes a still-open
 * stream (file dropped without close()) and frees the handle. close()
 * only nulls fp so the record can outlive it safely.
 *
 * The allocator arrives as the descriptor's user_data rather than being
 * stashed in every handle: teardown happens with no VM and no call in scope,
 * so there is nowhere else for it to come from. */
static void file_destroy(void* instance, void* user_data)
{
  oak_file_handle_t* h = instance;
  if (!h)
    return;
  if (h->fp)
    fclose(h->fp);
  OAK_FREE((oak_allocator_t*)user_data, h);
}

/* Compile-time signature for open(). Built in oak_stdlib_register_file rather
 * than here, because typing the mode as the io.FileMode enum needs that
 * descriptor in scope. At run time an enum is just its integer value, which is
 * what oak_arg_i32 below checks for. */

/* open() is bound twice -- as the module-level io.open and as the static
 * method io.File.open -- and only the method has a receiver type. Take the
 * descriptor from whichever the call supplies, so neither registration needs a
 * file static that would tie two oak_compile_options_t in one process
 * together. */
static const oak_bind_type_t* file_type_of(const oak_native_call_t* call)
{
  return call->self_type ? call->self_type
                         : (const oak_bind_type_t*)call->user_data;
}

static oak_fn_call_result_t file_open(oak_native_call_t* call,
                                      const oak_value_t* args,
                                      const usize argc,
                                      oak_value_t* out)
{
  const char* path;
  int mode_value;
  if (!oak_arg_cstring(call, args, argc, 0, &path) ||
      !oak_arg_i32(call, args, argc, 1, &mode_value))
    return OAK_FN_CALL_RUNTIME_ERROR;

  const char* mode;
  const oak_file_mode_t requested = (oak_file_mode_t)mode_value;
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
      return oak_native_error(call, "unknown file mode %d", mode_value);
  }
  FILE* fp = fopen(path, mode);
  if (!fp)
    return oak_native_error(call, "cannot open '%s' for '%s'", path, mode);
  oak_file_handle_t* h = OAK_ALLOC(call->allocator, sizeof *h);
  if (!h)
  {
    fclose(fp);
    return oak_native_error(call, "out of memory opening '%s'", path);
  }
  h->fp = fp;
  h->mode = requested;
  *out = oak_vm_native_record_new(call->vm, file_type_of(call), h);
  return OAK_FN_CALL_OK;
}

/* Every instance method wants the same two things: the receiver's handle, and
 * for its stream to still be open. close() only nulls fp -- the record outlives
 * it deliberately -- so a closed File reaches these methods and must be turned
 * away with a reason rather than by touching a null FILE*. */
static int file_handle(oak_native_call_t* call,
                       const oak_value_t* args,
                       const usize argc,
                       oak_file_handle_t** out)
{
  oak_file_handle_t* h;
  if (!oak_arg_self(call, args, argc, (void**)&h))
    return 0;
  if (!h->fp)
  {
    oak_native_error(call, "the file is closed");
    return 0;
  }
  *out = h;
  return 1;
}

static oak_fn_call_result_t file_read(oak_native_call_t* call,
                                      const oak_value_t* args,
                                      const usize argc,
                                      oak_value_t* out)
{
  oak_file_handle_t* h;
  if (!file_handle(call, args, argc, &h))
    return OAK_FN_CALL_RUNTIME_ERROR;
  char buf[4096];
  if (!fgets(buf, sizeof buf, h->fp))
  {
    *out = oak_vm_string_value_len(call->vm, "", 0);
    return OAK_FN_CALL_OK;
  }
  *out = oak_vm_string_value_len(call->vm, buf, strlen(buf));
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t file_read_all(oak_native_call_t* call,
                                          const oak_value_t* args,
                                          const usize argc,
                                          oak_value_t* out)
{
  oak_file_handle_t* h;
  if (!file_handle(call, args, argc, &h))
    return OAK_FN_CALL_RUNTIME_ERROR;
  FILE* const f = h->fp;
  const long pos = ftell(f);
  if (pos < 0)
    return oak_native_error(call, "cannot read the file position");
  if (fseek(f, 0, SEEK_END) != 0)
    return oak_native_error(call, "cannot seek to the end of the file");
  const long end = ftell(f);
  if (end < 0 || fseek(f, pos, SEEK_SET) != 0)
    return oak_native_error(call, "cannot restore the file position");
  const size_t n = (size_t)(end - pos);
  if (n == 0)
  {
    *out = oak_vm_string_value_len(call->vm, "", 0);
    return OAK_FN_CALL_OK;
  }
  char* buf = OAK_ALLOC(call->allocator, n + 1u);
  if (!buf)
    return oak_native_error(call, "out of memory reading %zu bytes", n);
  const size_t got = fread(buf, 1u, n, f);
  buf[got] = '\0';
  *out = oak_vm_string_value_len(call->vm, buf, got);
  OAK_FREE(call->allocator, buf);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t file_write(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       const usize argc,
                                       oak_value_t* out)
{
  oak_file_handle_t* h;
  const char* text;
  /* args[0] is the receiver, so the explicit parameter is at index 1. */
  if (!file_handle(call, args, argc, &h) ||
      !oak_arg_cstring(call, args, argc, 1, &text))
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (fputs(text, h->fp) == EOF)
    return oak_native_error(call, "write failed");
  *out = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t file_eof(oak_native_call_t* call,
                                     const oak_value_t* args,
                                     const usize argc,
                                     oak_value_t* out)
{
  oak_file_handle_t* h;
  if (!file_handle(call, args, argc, &h))
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
    return oak_native_error(call, "cannot read the file position");
  if (fseek(f, 0, SEEK_END) != 0)
    return oak_native_error(call, "cannot seek to the end of the file");
  const long end = ftell(f);
  if (end < 0 || fseek(f, pos, SEEK_SET) != 0)
    return oak_native_error(call, "cannot restore the file position");

  *out = OAK_VALUE_BOOL(pos >= end);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t file_close(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       const usize argc,
                                       oak_value_t* out)
{
  oak_file_handle_t* h;
  if (!file_handle(call, args, argc, &h))
    return OAK_FN_CALL_RUNTIME_ERROR;
  fclose(h->fp);
  /* The handle stays alive (freed by file_destroy) so that read/write/close
   * on an already-closed File fail cleanly instead of touching freed memory. */
  h->fp = null;
  *out = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

/* The compile-time signature for write(). Static, not a local in the function
 * below: param_types is borrowed, so the array has to outlive oak_compile_ex.
 * The callback no longer reads it -- oak_arg_cstring does that check and says
 * what went wrong -- but declaring it still buys call-site checking in Oak. */
static const oak_bind_type_ref_t file_write_params[] = {
  OAK_BIND_SCALAR_INIT(OAK_TYPE_STRING),
};

void oak_stdlib_register_file(oak_compile_options_t* opts)
{
  if (!opts)
    return;
  oak_bind_type_t* t =
      oak_bind_type_in_module(opts, "io", OAK_BIND_TYPE_RECORD, "File");
  if (!t)
    return;
  t->destructor = file_destroy;
  t->user_data = opts->allocator;

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
      /* A global function has no receiver, so this is the only way the
       * descriptor reaches file_open; the static method below gets it as
       * self_type instead (see file_type_of). */
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
      .param_count = mode ? (int)oak_count_of(open_sig) : 0 },
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
      OAK_BIND_PARAMS(file_write_params),
      .return_type = OAK_BIND_SCALAR(OAK_TYPE_VOID) },
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
