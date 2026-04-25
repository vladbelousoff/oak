#include "oak_stdlib_file.h"

#include "oak_bind.h"
#include "oak_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

typedef struct oak_file_handle_t
{
  FILE* fp;
} oak_file_handle_t;

static const struct oak_native_type_t* s_file_type;

/* Returns the process current working directory (no trailing separator). */
static enum oak_fn_call_result_t builtin_pwd(void* vm,
                                             const struct oak_value_t* args,
                                             int argc,
                                             struct oak_value_t* out)
{
  (void)vm;
  if (argc != 0)
    return OAK_FN_CALL_RUNTIME_ERROR;
  char buf[4096];
#if defined(_WIN32)
  if (_getcwd(buf, (int)sizeof buf) == null)
    return OAK_FN_CALL_RUNTIME_ERROR;
#else
  if (getcwd(buf, sizeof buf) == null)
    return OAK_FN_CALL_RUNTIME_ERROR;
#endif
  const usize n = strlen(buf);
  struct oak_obj_string_t* s = oak_string_new(buf, n);
  *out = OAK_VALUE_OBJ(&s->obj);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t file_open(void* vm,
                                           const struct oak_value_t* args,
                                           int argc,
                                           struct oak_value_t* out)
{
  (void)vm;
  if (argc != 2 || !oak_is_string(args[0]) || !oak_is_string(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  FILE* fp = fopen(oak_as_cstring(args[0]), oak_as_cstring(args[1]));
  if (!fp)
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_file_handle_t* h = malloc(sizeof *h);
  if (!h)
  {
    fclose(fp);
    return OAK_FN_CALL_RUNTIME_ERROR;
  }
  h->fp = fp;
  *out = oak_native_struct_new(s_file_type, h);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t file_read(void* vm,
                                           const struct oak_value_t* args,
                                           int argc,
                                           struct oak_value_t* out)
{
  (void)vm;
  if (argc != 1 || !oak_is_native_struct(args[0]))
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

static enum oak_fn_call_result_t file_read_all(void* vm,
                                               const struct oak_value_t* args,
                                               int argc,
                                               struct oak_value_t* out)
{
  (void)vm;
  if (argc != 1 || !oak_is_native_struct(args[0]))
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
  char* buf = malloc(n + 1u);
  if (!buf)
    return OAK_FN_CALL_RUNTIME_ERROR;
  const size_t got = fread(buf, 1u, n, f);
  buf[got] = '\0';
  struct oak_obj_string_t* s = oak_string_new(buf, got);
  free(buf);
  *out = OAK_VALUE_OBJ(&s->obj);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t file_write(void* vm,
                                            const struct oak_value_t* args,
                                            int argc,
                                            struct oak_value_t* out)
{
  (void)vm;
  if (argc != 2 || !oak_is_native_struct(args[0]) || !oak_is_string(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_file_handle_t* h = oak_native_instance(args[0]);
  if (!h || !h->fp)
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (fputs(oak_as_cstring(args[1]), h->fp) == EOF)
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t file_eof(void* vm,
                                          const struct oak_value_t* args,
                                          int argc,
                                          struct oak_value_t* out)
{
  (void)vm;
  if (argc != 1 || !oak_is_native_struct(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_file_handle_t* h = oak_native_instance(args[0]);
  if (!h || !h->fp)
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_BOOL(feof(h->fp) != 0);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t file_close(void* vm,
                                            const struct oak_value_t* args,
                                            int argc,
                                            struct oak_value_t* out)
{
  (void)vm;
  if (argc != 1 || !oak_is_native_struct(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_file_handle_t* h = oak_native_instance(args[0]);
  if (!h || !h->fp)
    return OAK_FN_CALL_RUNTIME_ERROR;
  fclose(h->fp);
  h->fp = null;
  free(h);
  *out = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

void oak_stdlib_register_file(struct oak_compile_options_t* opts)
{
  if (!opts)
    return;
  oak_bind_fn(opts,
              OAK_BIND_FN_GLOBAL,
              OAK_TYPE_VOID,
              "pwd",
              builtin_pwd,
              0,
              OAK_TYPE_STRING,
              OAK_BIND_RETURN_SCALAR);
  struct oak_native_type_t* t = oak_bind_type(opts, OAK_BIND_STRUCT, "File");
  if (!t)
    return;
  s_file_type = t;

  oak_bind_fn(opts,
              OAK_BIND_FN_STATIC_METHOD,
              t->type_id,
              "open",
              file_open,
              2,
              t->type_id,
              OAK_BIND_RETURN_SCALAR);
  oak_bind_fn(opts,
              OAK_BIND_FN_INSTANCE_METHOD,
              t->type_id,
              "read",
              file_read,
              0,
              OAK_TYPE_STRING,
              OAK_BIND_RETURN_SCALAR);
  oak_bind_fn(opts,
              OAK_BIND_FN_INSTANCE_METHOD,
              t->type_id,
              "read_all",
              file_read_all,
              0,
              OAK_TYPE_STRING,
              OAK_BIND_RETURN_SCALAR);
  oak_bind_fn(opts,
              OAK_BIND_FN_INSTANCE_METHOD,
              t->type_id,
              "write",
              file_write,
              1,
              OAK_TYPE_VOID,
              OAK_BIND_RETURN_SCALAR);
  oak_bind_fn(opts,
              OAK_BIND_FN_INSTANCE_METHOD,
              t->type_id,
              "eof",
              file_eof,
              0,
              OAK_TYPE_BOOL,
              OAK_BIND_RETURN_SCALAR);
  oak_bind_fn(opts,
              OAK_BIND_FN_INSTANCE_METHOD,
              t->type_id,
              "close",
              file_close,
              0,
              OAK_TYPE_VOID,
              OAK_BIND_RETURN_SCALAR);
}
