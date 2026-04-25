#include "oak_stdlib_lexer.h"

#include "oak_bind.h"
#include "oak_lexer.h"
#include "oak_list.h"
#include "oak_mem.h"
#include "oak_token.h"
#include "oak_value.h"

#include <stdio.h>
#include <string.h>

typedef struct oak_token_box_t
{
  enum oak_token_kind_t kind;
  int line;
  int column;
  int offset;
  struct oak_obj_string_t* lexeme;
} oak_token_box_t;

static const struct oak_native_type_t* s_token_type;

static struct oak_obj_string_t* make_lexeme_str(const struct oak_token_t* t)
{
  if (t->kind == OAK_TOKEN_INT)
  {
    char b[32];
    const int n = snprintf(
        b, sizeof b, "%d", oak_token_as_i32(t));
    if (n < 0)
      return null;
    return oak_string_new(b, (usize)n);
  }
  if (t->kind == OAK_TOKEN_FLOAT)
  {
    char b[64];
    const int n = snprintf(
        b, sizeof b, "%.9g", (double)oak_token_as_f32(t));
    if (n < 0)
      return null;
    return oak_string_new(b, (usize)n);
  }
  return oak_string_new(oak_token_text(t), oak_token_length(t));
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
  const char* s = oak_token_name(b->kind);
  const usize len = strlen(s);
  struct oak_obj_string_t* str = oak_string_new(s, len);
  return OAK_VALUE_OBJ(&str->obj);
}

static struct oak_value_t oak_token_get_lexeme(const struct oak_value_t self)
{
  const oak_token_box_t* b = oak_native_instance(self);
  if (!b || !b->lexeme)
    return empty_string_value();
  oak_obj_incref(&b->lexeme->obj);
  return OAK_VALUE_OBJ(&b->lexeme->obj);
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

/* Native struct wrappers are not freed by the VM; per-token heap is a known
   limitation for v1. */

static enum oak_fn_call_result_t oak_lexer_tokenize_impl(void* vm,
                                                         const struct oak_value_t* args,
                                                         int argc,
                                                         struct oak_value_t* out)
{
  (void)vm;
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
    box->kind = tok->kind;
    box->line = oak_token_line(tok);
    box->column = oak_token_column(tok);
    box->offset = oak_token_offset(tok);
    box->lexeme = make_lexeme_str(tok);
    if (!box->lexeme)
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

  struct oak_native_type_t* tok =
      oak_bind_type(opts, OAK_BIND_RECORD, "OakToken");
  if (!tok)
    return;
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
          &(struct oak_native_field_t){ .name = "lexeme",
                                     .field_type_id = OAK_TYPE_STRING,
                                     .getter = oak_token_get_lexeme,
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
