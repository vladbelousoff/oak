#include "oak_stdlib_file.h"

#include "oak_bind.h"
#include "oak_mem.h"
#include "oak_value.h"
#include "oak_vm.h"

#include <stdio.h>
#include <string.h>

typedef struct oak_file_handle_t
{
  FILE* fp;
} oak_file_handle_t;

static const struct oak_native_type_t* s_file_type;

static enum oak_fn_call_result_t file_open(struct oak_native_ctx_t* ctx,
                                           const struct oak_value_t* args,
                                           int argc,
                                           struct oak_value_t* out)
{
  if (argc != 2 || !oak_is_string(args[0]) || !oak_is_string(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  FILE* fp = fopen(oak_as_cstring(args[0]), oak_as_cstring(args[1]));
  if (!fp)
    return OAK_FN_CALL_RUNTIME_ERROR;
  const struct oak_src_loc_t loc = oak_vm_oak_mem_src_loc(ctx->vm);
  oak_file_handle_t* h = oak_alloc(sizeof *h, loc);
  if (!h)
  {
    fclose(fp);
    return OAK_FN_CALL_RUNTIME_ERROR;
  }
  h->fp = fp;
  *out = oak_native_record_new(s_file_type, h);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t file_read(struct oak_native_ctx_t* ctx,
                                           const struct oak_value_t* args,
                                           int argc,
                                           struct oak_value_t* out)
{
  (void)ctx;
  if (argc != 1 || !oak_is_native_record(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_file_handle_t* h = oak_native_instance(args[0]);
  if (!h || !h->fp)
    return OAK_FN_CALL_RUNTIME_ERROR;
  char buf[4096];
  if (!fgets(buf, sizeof buf, h->fp))
  {
    struct oak_obj_string_t* s = oak_string_new("", 0);
    *out = OAK_VALUE_OBJ(&s->obj);
    return OAK_FN_CALL_OK;
  }
  const usize len = strlen(buf);
  struct oak_obj_string_t* s = oak_string_new(buf, len);
  *out = OAK_VALUE_OBJ(&s->obj);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t file_read_all(struct oak_native_ctx_t* ctx,
                                               const struct oak_value_t* args,
                                               int argc,
                                               struct oak_value_t* out)
{
  (void)ctx;
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
    struct oak_obj_string_t* s = oak_string_new("", 0);
    *out = OAK_VALUE_OBJ(&s->obj);
    return OAK_FN_CALL_OK;
  }
  const struct oak_src_loc_t loc = oak_vm_oak_mem_src_loc(ctx->vm);
  char* buf = oak_alloc(n + 1u, loc);
  if (!buf)
    return OAK_FN_CALL_RUNTIME_ERROR;
  const size_t got = fread(buf, 1u, n, f);
  buf[got] = '\0';
  struct oak_obj_string_t* s = oak_string_new(buf, got);
  oak_free(buf, loc);
  *out = OAK_VALUE_OBJ(&s->obj);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t file_write(struct oak_native_ctx_t* ctx,
                                            const struct oak_value_t* args,
                                            int argc,
                                            struct oak_value_t* out)
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

static enum oak_fn_call_result_t file_eof(struct oak_native_ctx_t* ctx,
                                          const struct oak_value_t* args,
                                          int argc,
                                          struct oak_value_t* out)
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

static enum oak_fn_call_result_t file_close(struct oak_native_ctx_t* ctx,
                                            const struct oak_value_t* args,
                                            int argc,
                                            struct oak_value_t* out)
{
  if (argc != 1 || !oak_is_native_record(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_file_handle_t* h = oak_native_instance(args[0]);
  if (!h || !h->fp)
    return OAK_FN_CALL_RUNTIME_ERROR;
  const struct oak_src_loc_t loc = oak_vm_oak_mem_src_loc(ctx->vm);
  fclose(h->fp);
  h->fp = null;
  oak_free(h, loc);
  *out = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

void oak_stdlib_register_file(struct oak_compile_options_t* opts)
{
  if (!opts)
    return;
  struct oak_native_type_t* t = oak_bind_type(opts, OAK_BIND_RECORD, "File");
  if (!t)
    return;
  s_file_type = t;

  oak_bind_fn(opts,
              &(oak_bind_fn_params_t){
                  .kind = OAK_BIND_FN_STATIC_METHOD,
                  .receiver_type_id = t->type_id,
                  .name = "open",
                  .impl = file_open,
                  .arity = 2,
                  .return_type_id = t->type_id,
                  .return_shape = OAK_BIND_RETURN_SCALAR,
              });
  oak_bind_fn(opts,
              &(oak_bind_fn_params_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type_id = t->type_id,
                  .name = "read",
                  .impl = file_read,
                  .arity = 0,
                  .return_type_id = OAK_TYPE_STRING,
                  .return_shape = OAK_BIND_RETURN_SCALAR,
              });
  oak_bind_fn(opts,
              &(oak_bind_fn_params_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type_id = t->type_id,
                  .name = "read_all",
                  .impl = file_read_all,
                  .arity = 0,
                  .return_type_id = OAK_TYPE_STRING,
                  .return_shape = OAK_BIND_RETURN_SCALAR,
              });
  oak_bind_fn(opts,
              &(oak_bind_fn_params_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type_id = t->type_id,
                  .name = "write",
                  .impl = file_write,
                  .arity = 1,
                  .return_type_id = OAK_TYPE_VOID,
                  .return_shape = OAK_BIND_RETURN_SCALAR,
              });
  oak_bind_fn(opts,
              &(oak_bind_fn_params_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type_id = t->type_id,
                  .name = "eof",
                  .impl = file_eof,
                  .arity = 0,
                  .return_type_id = OAK_TYPE_BOOL,
                  .return_shape = OAK_BIND_RETURN_SCALAR,
              });
  oak_bind_fn(opts,
              &(oak_bind_fn_params_t){
                  .kind = OAK_BIND_FN_INSTANCE_METHOD,
                  .receiver_type_id = t->type_id,
                  .name = "close",
                  .impl = file_close,
                  .arity = 0,
                  .return_type_id = OAK_TYPE_VOID,
                  .return_shape = OAK_BIND_RETURN_SCALAR,
              });
}
