#include "oak_stdlib_lexer.h"

#include "oak_bind.h"
#include "oak_lexer.h"
#include "oak_list.h"
#include "oak_mem.h"
#include "oak_token.h"
#include "oak_value.h"

#include <stdio.h>
#include <string.h>

typedef struct oak_token_value_box_t
{
  enum oak_token_kind_t kind;
  union {
    int i32;
    float f32;
    struct oak_obj_string_t* text;
  } u;
} oak_token_value_box_t;

typedef struct oak_token_box_t
{
  int line;
  int column;
  int offset;
  oak_token_value_box_t value;
} oak_token_box_t;

static const struct oak_native_type_t* s_token_value_type;
static const struct oak_native_type_t* s_token_type;

static oak_token_value_box_t s_token_value_sentinel;

static struct oak_obj_string_t* format_token_value_as_string(
    const oak_token_value_box_t* v)
{
  if (!v)
    return oak_string_new("", 0);
  if (v->kind == OAK_TOKEN_INT)
  {
    char b[32];
    const int n = snprintf(b, sizeof b, "%d", v->u.i32);
    if (n < 0)
      return null;
    return oak_string_new(b, (usize)n);
  }
  if (v->kind == OAK_TOKEN_FLOAT)
  {
    char b[64];
    const int n = snprintf(b, sizeof b, "%f", v->u.f32);
    if (n < 0)
      return null;
    return oak_string_new(b, (usize)n);
  }
  if (!v->u.text)
    return oak_string_new("", 0);
  oak_obj_incref(&v->u.text->obj);
  return v->u.text;
}

static struct oak_value_t empty_string_value(void)
{
  struct oak_obj_string_t* s = oak_string_new("", 0);
  return OAK_VALUE_OBJ(&s->obj);
}

static struct oak_value_t oak_token_get_kind(const struct oak_value_t self)
{
  const oak_token_box_t* b = oak_native_instance(self);
  if (!b)
    return empty_string_value();
  const char* s = oak_token_name(b->value.kind);
  const usize len = strlen(s);
  struct oak_obj_string_t* str = oak_string_new(s, len);
  return OAK_VALUE_OBJ(&str->obj);
}

static struct oak_value_t oak_token_get_value(const struct oak_value_t self)
{
  const oak_token_box_t* b = oak_native_instance(self);
  if (!b)
    return oak_native_record_new(s_token_value_type, &s_token_value_sentinel);
  return oak_native_record_new(s_token_value_type, (void*)&b->value);
}

static struct oak_value_t oak_token_get_line(const struct oak_value_t self)
{
  const oak_token_box_t* b = oak_native_instance(self);
  if (!b)
    return OAK_VALUE_I32(0);
  return OAK_VALUE_I32(b->line);
}

static struct oak_value_t oak_token_get_column(const struct oak_value_t self)
{
  const oak_token_box_t* b = oak_native_instance(self);
  if (!b)
    return OAK_VALUE_I32(0);
  return OAK_VALUE_I32(b->column);
}

static struct oak_value_t oak_token_get_offset(const struct oak_value_t self)
{
  const oak_token_box_t* b = oak_native_instance(self);
  if (!b)
    return OAK_VALUE_I32(0);
  return OAK_VALUE_I32(b->offset);
}

static enum oak_fn_call_result_t oak_token_value_to_string_impl(
    struct oak_native_ctx_t* ctx,
    const struct oak_value_t* args,
    int argc,
    struct oak_value_t* out)
{
  (void)ctx;
  if (argc < 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  const oak_token_value_box_t* v = oak_native_instance(args[0]);
  struct oak_obj_string_t* s = format_token_value_as_string(v);
  if (!s)
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_OBJ(&s->obj);
  return OAK_FN_CALL_OK;
}

static void oak_token_box_destroy(void* instance)
{
  oak_token_box_t* b = instance;
  if (!b)
    return;
  if (b->value.kind != OAK_TOKEN_INT && b->value.kind != OAK_TOKEN_FLOAT &&
      b->value.u.text)
    oak_obj_decref(&b->value.u.text->obj);
  oak_free(b, OAK_SRC_LOC);
}

static int fill_token_value(
    oak_token_value_box_t* out, const struct oak_token_t* tok)
{
  out->kind = tok->kind;
  if (tok->kind == OAK_TOKEN_INT)
  {
    out->u.i32 = oak_token_as_i32(tok);
    return 0;
  }
  if (tok->kind == OAK_TOKEN_FLOAT)
  {
    out->u.f32 = oak_token_as_f32(tok);
    return 0;
  }
  out->u.text =
      oak_string_new(oak_token_text(tok), oak_token_length(tok));
  return out->u.text ? 0 : -1;
}

int oak_stdlib_oak_token_inspect(const struct oak_value_t v,
                                 struct oak_stdlib_token_view_t* out)
{
  if (!s_token_type || !out)
    return -1;
  if (!oak_is_native_record(v))
    return -1;
  const struct oak_obj_native_record_t* nr = oak_as_native_record(v);
  if (nr->type != s_token_type)
    return -1;
  const oak_token_box_t* b = nr->instance;
  if (!b)
    return -1;
  memset(out, 0, sizeof *out);
  out->line = b->line;
  out->column = b->column;
  out->offset = b->offset;
  out->kind = b->value.kind;
  if (b->value.kind == OAK_TOKEN_INT)
    out->i32 = b->value.u.i32;
  else if (b->value.kind == OAK_TOKEN_FLOAT)
    out->f32 = b->value.u.f32;
  else if (b->value.u.text)
  {
    out->text_ptr = b->value.u.text->chars;
    out->text_len = b->value.u.text->length;
  }
  return 0;
}

static enum oak_fn_call_result_t oak_lexer_tokenize_impl(
    struct oak_native_ctx_t* ctx,
    const struct oak_value_t* args,
    int argc,
    struct oak_value_t* out)
{
  (void)ctx;
  if (argc != 1 || !oak_is_string(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const struct oak_obj_string_t* sv = oak_as_string(args[0]);
  struct oak_lexer_result_t* L =
      oak_lexer_tokenize(sv->chars, sv->length);
  if (!L)
  {
    *out = OAK_VALUE_I32(0);
    return OAK_FN_CALL_OK;
  }

  struct oak_obj_array_t* arr = oak_array_new();
  const struct oak_list_entry_t* const head = oak_lexer_tokens(L);
  struct oak_list_entry_t* const list_head = (struct oak_list_entry_t*)head;
  for (struct oak_list_entry_t* p = list_head->next; p != list_head;
       p = p->next)
  {
    const struct oak_token_t* tok =
        oak_container_of(p, struct oak_token_t, link);
    oak_token_box_t* box = oak_alloc(sizeof *box, OAK_SRC_LOC);
    if (!box)
    {
      oak_lexer_free(L);
      return OAK_FN_CALL_RUNTIME_ERROR;
    }
    box->line = oak_token_line(tok);
    box->column = oak_token_column(tok);
    box->offset = oak_token_offset(tok);
    if (fill_token_value(&box->value, tok) < 0)
    {
      oak_free(box, OAK_SRC_LOC);
      oak_lexer_free(L);
      return OAK_FN_CALL_RUNTIME_ERROR;
    }
    struct oak_value_t v = oak_native_record_new(s_token_type, box);
    oak_array_push(arr, v);
    oak_value_decref(v);
  }

  oak_lexer_free(L);
  *out = OAK_VALUE_OBJ(&arr->obj);
  return OAK_FN_CALL_OK;
}

void oak_stdlib_register_lexer(struct oak_compile_options_t* opts)
{
  if (!opts)
    return;

  struct oak_native_type_t* tval =
      oak_bind_type(opts, OAK_BIND_RECORD, "OakTokenValue");
  if (!tval)
    return;
  s_token_value_type = tval;
  if (oak_bind_fn(
          opts,
          &(oak_bind_fn_params_t){
              .kind = OAK_BIND_FN_INSTANCE_METHOD,
              .receiver_type_id = tval->type_id,
              .name = "to_string",
              .impl = oak_token_value_to_string_impl,
              .arity = 0,
              .return_type_id = OAK_TYPE_STRING,
              .return_shape = OAK_BIND_RETURN_SCALAR,
          }) != 0)
    return;

  struct oak_native_type_t* tok =
      oak_bind_type(opts, OAK_BIND_RECORD, "OakToken");
  if (!tok)
    return;
  tok->destroy_instance = oak_token_box_destroy;
  s_token_type = tok;
  if (oak_bind_field(
          tok,
          &(struct oak_native_field_t){ .name = "kind",
                                     .field_type_id = OAK_TYPE_STRING,
                                     .getter = oak_token_get_kind,
                                     .setter = null }) < 0)
    return;
  if (oak_bind_field(
          tok,
          &(struct oak_native_field_t){ .name = "value",
                                     .field_type_id = tval->type_id,
                                     .getter = oak_token_get_value,
                                     .setter = null }) < 0)
    return;
  if (oak_bind_field(
          tok,
          &(struct oak_native_field_t){ .name = "line",
                                     .field_type_id = OAK_TYPE_NUMBER,
                                     .getter = oak_token_get_line,
                                     .setter = null }) < 0)
    return;
  if (oak_bind_field(
          tok,
          &(struct oak_native_field_t){ .name = "column",
                                     .field_type_id = OAK_TYPE_NUMBER,
                                     .getter = oak_token_get_column,
                                     .setter = null }) < 0)
    return;
  if (oak_bind_field(
          tok,
          &(struct oak_native_field_t){ .name = "offset",
                                     .field_type_id = OAK_TYPE_NUMBER,
                                     .getter = oak_token_get_offset,
                                     .setter = null }) < 0)
    return;

  struct oak_native_type_t* lexer =
      oak_bind_type(opts, OAK_BIND_RECORD, "OakLexer");
  if (!lexer)
    return;

  if (oak_bind_fn(
          opts,
          &(oak_bind_fn_params_t){
              .kind = OAK_BIND_FN_STATIC_METHOD,
              .receiver_type_id = lexer->type_id,
              .name = "tokenize",
              .impl = oak_lexer_tokenize_impl,
              .arity = 1,
              .return_type_id = tok->type_id,
              .return_shape = OAK_BIND_RETURN_ARRAY,
          }) != 0)
    return;
}
