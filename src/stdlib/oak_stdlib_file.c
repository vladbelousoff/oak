#include "oak_stdlib_file.h"

#include "oak_allocator.h"
#include "oak_bind.h"
#include "oak_value.h"
#include "oak_vm.h"

#include <stdio.h>
#include <string.h>

typedef struct oak_file_handle oak_file_handle_t;
struct oak_file_handle
{
  FILE* fp; /* null after close() */
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

/* FileMode variant integer values. Must match the order/values registered in
 * oak_stdlib_register_file. */
typedef enum oak_file_mode oak_file_mode_t;
enum oak_file_mode
{
  OAK_FILE_MODE_READ = 0,
  OAK_FILE_MODE_WRITE = 1,
  OAK_FILE_MODE_APPEND = 2,
};

static const oak_bind_type_t* s_file_type;

static oak_fn_call_result_t file_open(oak_native_ctx_t* ctx,
                                           const oak_value_t* args,
                                           int argc,
                                           oak_value_t* out)
{
  if (argc != 2 || !oak_is_string(args[0]) || !oak_is_i32(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const char* mode;
  switch (oak_as_i32(args[1]))
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
  h->allocator = ctx->allocator;
  *out = oak_vm_native_record_new(ctx->vm, s_file_type, h);
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
  if (argc != 2 || !oak_is_native_record(args[0]) || !oak_is_string(args[1]))
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
  *out = OAK_VALUE_BOOL(feof(h->fp) != 0);
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
  s_file_type = t;

  oak_bind_enum_t* mode =
      oak_bind_enum_in_module(opts, "io", "FileMode");
  if (mode)
  {
    oak_bind_enum_variant(mode, "Read", OAK_FILE_MODE_READ);
    oak_bind_enum_variant(mode, "Write", OAK_FILE_MODE_WRITE);
    oak_bind_enum_variant(mode, "Append", OAK_FILE_MODE_APPEND);
  }

  oak_bind_fn_global(opts,
                     &(oak_bind_global_fn_t){
                         .module_name = "io",
                         .name = "open",
                         .impl = file_open,
                         .arity = 2,
                         .return_type = OAK_BIND_NATIVE(t),
                     });
  oak_bind_fn(opts,
              &(oak_bind_fn_t){
                  .kind = OAK_BIND_FN_STATIC_METHOD,
                  .receiver_type = t,
                  .name = "open",
                  .impl = file_open,
                  .arity = 2,
                  .return_type = OAK_BIND_NATIVE(t),
              });
  oak_bind_fn(opts,
              &(oak_bind_fn_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type = t,
                  .name = "read",
                  .impl = file_read,
                  .arity = 0,
                  .return_type = OAK_BIND_SCALAR(OAK_TYPE_STRING),
              });
  oak_bind_fn(opts,
              &(oak_bind_fn_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type = t,
                  .name = "read_all",
                  .impl = file_read_all,
                  .arity = 0,
                  .return_type = OAK_BIND_SCALAR(OAK_TYPE_STRING),
              });
  oak_bind_fn(opts,
              &(oak_bind_fn_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type = t,
                  .name = "write",
                  .impl = file_write,
                  .arity = 1,
                  .return_type = OAK_BIND_SCALAR(OAK_TYPE_VOID),
              });
  oak_bind_fn(opts,
              &(oak_bind_fn_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type = t,
                  .name = "eof",
                  .impl = file_eof,
                  .arity = 0,
                  .return_type = OAK_BIND_SCALAR(OAK_TYPE_BOOL),
              });
  oak_bind_fn(opts,
              &(oak_bind_fn_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type = t,
                  .name = "close",
                  .impl = file_close,
                  .arity = 0,
                  .return_type = OAK_BIND_SCALAR(OAK_TYPE_VOID),
              });
}
