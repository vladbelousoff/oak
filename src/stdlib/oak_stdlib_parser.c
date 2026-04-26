#include "oak_stdlib_parser.h"

#include "oak_bind.h"
#include "oak_lexer_internal.h"
#include "oak_list.h"
#include "oak_mem.h"
#include "oak_parser.h"
#include "oak_stdlib_lexer.h"
#include "oak_token.h"
#include "oak_value.h"

#include <stdio.h>
#include <string.h>

typedef struct oak_diagnostic_box_t
{
  int line;
  int column;
  struct oak_obj_string_t* message;
} oak_diagnostic_box_t;

typedef struct oak_parse_box_t
{
  struct oak_lexer_result_t* lexer;
  struct oak_parser_result_t parse;
  struct oak_obj_array_t* errors_arr;
  int disposed;
} oak_parse_box_t;

static const struct oak_native_type_t* s_diag_type;
static const struct oak_native_type_t* s_ast_type;
static const struct oak_native_type_t* s_parse_result_type;

static struct oak_obj_string_t* empty_str_obj(void)
{
  return oak_string_new("", 0);
}

static struct oak_value_t empty_string_value(void)
{
  struct oak_obj_string_t* s = empty_str_obj();
  return OAK_VALUE_OBJ(&s->obj);
}

static struct oak_obj_string_t*
token_to_display_string(const struct oak_token_t* t)
{
  if (!t)
    return empty_str_obj();
  if (t->kind == OAK_TOKEN_INT)
  {
    char b[32];
    const int n = snprintf(b, sizeof b, "%d", oak_token_as_i32(t));
    if (n < 0)
      return null;
    return oak_string_new(b, (usize)n);
  }
  if (t->kind == OAK_TOKEN_FLOAT)
  {
    char b[64];
    const int n = snprintf(b, sizeof b, "%f", oak_token_as_f32(t));
    if (n < 0)
      return null;
    return oak_string_new(b, (usize)n);
  }
  return oak_string_new(oak_token_text(t), oak_token_length(t));
}

static int push_token_from_view(struct oak_lexer_result_t* lexer,
                                const struct oak_stdlib_token_view_t* v)
{
  usize token_size = sizeof(struct oak_token_t);
  if (v->kind == OAK_TOKEN_INT)
    token_size += sizeof(int);
  else if (v->kind == OAK_TOKEN_FLOAT)
    token_size += sizeof(float);
  else if (v->kind == OAK_TOKEN_STRING && v->text_len == 0)
    token_size += 1;
  else
    token_size += v->text_len + 1;

  struct oak_token_t* t = oak_arena_alloc(&lexer->arena, token_size);
  if (!t)
    return -1;
  t->kind = v->kind;
  t->line = v->line;
  t->column = v->column;
  t->offset = v->offset;
  if (v->kind == OAK_TOKEN_INT)
  {
    t->length = sizeof(int);
    memcpy(t->text, &v->i32, sizeof(int));
  }
  else if (v->kind == OAK_TOKEN_FLOAT)
  {
    t->length = sizeof(float);
    memcpy(t->text, &v->f32, sizeof(float));
  }
  else if (v->text_len > 0)
  {
    t->length = v->text_len;
    memcpy(t->text, v->text_ptr, v->text_len);
    t->text[v->text_len] = '\0';
  }
  else
  {
    t->length = 0;
    t->text[0] = '\0';
  }
  oak_list_add_tail(&lexer->tokens, &t->link);
  return 0;
}

static struct oak_value_t diag_get_line(const struct oak_value_t self)
{
  const oak_diagnostic_box_t* d = oak_native_instance(self);
  if (!d)
    return OAK_VALUE_I32(0);
  return OAK_VALUE_I32(d->line);
}

static struct oak_value_t diag_get_column(const struct oak_value_t self)
{
  const oak_diagnostic_box_t* d = oak_native_instance(self);
  if (!d)
    return OAK_VALUE_I32(0);
  return OAK_VALUE_I32(d->column);
}

static struct oak_value_t diag_get_message(const struct oak_value_t self)
{
  const oak_diagnostic_box_t* d = oak_native_instance(self);
  if (!d || !d->message)
    return empty_string_value();
  oak_obj_incref(&d->message->obj);
  return OAK_VALUE_OBJ(&d->message->obj);
}

static struct oak_value_t ast_get_kind(const struct oak_value_t self)
{
  const struct oak_ast_node_t* n = oak_native_instance(self);
  if (!n)
    return empty_string_value();
  const char* s = oak_ast_node_kind_name(n->kind);
  const usize len = strlen(s);
  struct oak_obj_string_t* str = oak_string_new(s, len);
  return OAK_VALUE_OBJ(&str->obj);
}

static struct oak_value_t ast_get_child_count(const struct oak_value_t self)
{
  const struct oak_ast_node_t* n = oak_native_instance(self);
  if (!n)
    return OAK_VALUE_I32(0);
  return OAK_VALUE_I32((int)oak_ast_node_child_count(n));
}

static struct oak_value_t ast_get_token_value(const struct oak_value_t self)
{
  const struct oak_ast_node_t* n = oak_native_instance(self);
  if (!n || !oak_node_is_token_terminal(n->kind) || !n->token)
    return empty_string_value();
  struct oak_obj_string_t* s = token_to_display_string(n->token);
  if (!s)
    return empty_string_value();
  return OAK_VALUE_OBJ(&s->obj);
}

static struct oak_value_t ast_get_is_terminal(const struct oak_value_t self)
{
  const struct oak_ast_node_t* n = oak_native_instance(self);
  if (!n)
    return OAK_VALUE_BOOL(0);
  return OAK_VALUE_BOOL(oak_node_is_token_terminal(n->kind));
}

static enum oak_fn_call_result_t ast_child_impl(struct oak_native_ctx_t* ctx,
                                                const struct oak_value_t* args,
                                                int argc,
                                                struct oak_value_t* out)
{
  (void)ctx;
  if (argc < 2 || !oak_is_i32(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const struct oak_ast_node_t* parent = oak_native_instance(args[0]);
  if (!parent)
  {
    *out = oak_native_record_new(s_ast_type, null);
    return OAK_FN_CALL_OK;
  }
  const int idx = oak_as_i32(args[1]);
  if (idx < 0)
  {
    *out = oak_native_record_new(s_ast_type, null);
    return OAK_FN_CALL_OK;
  }
  struct oak_ast_node_t* ch =
      oak_ast_node_child_at(parent, (usize)idx);
  *out = oak_native_record_new(s_ast_type, ch);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t ast_describe_impl(
    struct oak_native_ctx_t* ctx,
    const struct oak_value_t* args,
    int argc,
    struct oak_value_t* out)
{
  (void)ctx;
  if (argc < 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  const struct oak_ast_node_t* n = oak_native_instance(args[0]);
  if (!n)
  {
    struct oak_obj_string_t* s = oak_string_new("<null node>", 11u);
    if (!s)
      return OAK_FN_CALL_RUNTIME_ERROR;
    *out = OAK_VALUE_OBJ(&s->obj);
    return OAK_FN_CALL_OK;
  }

  char buf[640];
  const char* nk = oak_ast_node_kind_name(n->kind);
  const int nc = (int)oak_ast_node_child_count(n);

  if (oak_node_is_token_terminal(n->kind) && n->token)
  {
    struct oak_obj_string_t* lex = token_to_display_string(n->token);
    if (!lex)
      return OAK_FN_CALL_RUNTIME_ERROR;
    const int nwr = snprintf(buf,
                             sizeof buf,
                             "%s | children=%d | lexer=%s | lexeme=%s | "
                             "line=%d col=%d offset=%d",
                             nk,
                             nc,
                             oak_token_name(oak_token_kind(n->token)),
                             lex->chars,
                             oak_token_line(n->token),
                             oak_token_column(n->token),
                             oak_token_offset(n->token));
    oak_obj_decref(&lex->obj);
    if (nwr < 0 || (usize)nwr >= sizeof buf)
      return OAK_FN_CALL_RUNTIME_ERROR;
    struct oak_obj_string_t* s = oak_string_new(buf, (usize)nwr);
    if (!s)
      return OAK_FN_CALL_RUNTIME_ERROR;
    *out = OAK_VALUE_OBJ(&s->obj);
    return OAK_FN_CALL_OK;
  }

  const int nwr = snprintf(buf, sizeof buf, "%s | children=%d", nk, nc);
  if (nwr < 0 || (usize)nwr >= sizeof buf)
    return OAK_FN_CALL_RUNTIME_ERROR;
  struct oak_obj_string_t* s = oak_string_new(buf, (usize)nwr);
  if (!s)
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_OBJ(&s->obj);
  return OAK_FN_CALL_OK;
}

static struct oak_value_t parse_get_root(const struct oak_value_t self)
{
  const oak_parse_box_t* box = oak_native_instance(self);
  if (!box || box->disposed)
    return oak_native_record_new(s_ast_type, null);
  return oak_native_record_new(s_ast_type, (void*)box->parse.root);
}

static struct oak_value_t parse_get_error_count(const struct oak_value_t self)
{
  const oak_parse_box_t* box = oak_native_instance(self);
  if (!box || box->disposed)
    return OAK_VALUE_I32(0);
  return OAK_VALUE_I32(box->parse.error_count);
}

static enum oak_fn_call_result_t parse_errors_impl(struct oak_native_ctx_t* ctx,
                                                   const struct oak_value_t* args,
                                                   int argc,
                                                   struct oak_value_t* out)
{
  (void)ctx;
  if (argc < 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  const oak_parse_box_t* box = oak_native_instance(args[0]);
  if (!box || box->disposed || !box->errors_arr)
  {
    struct oak_obj_array_t* empty = oak_array_new();
    if (!empty)
      return OAK_FN_CALL_RUNTIME_ERROR;
    *out = OAK_VALUE_OBJ(&empty->obj);
    return OAK_FN_CALL_OK;
  }
  oak_obj_incref(&box->errors_arr->obj);
  *out = OAK_VALUE_OBJ(&box->errors_arr->obj);
  return OAK_FN_CALL_OK;
}

static void parse_box_free_errors_array(struct oak_obj_array_t* arr)
{
  if (!arr)
    return;
  for (usize i = 0; i < arr->length; ++i)
  {
    struct oak_value_t v = arr->items[i];
    if (oak_is_native_record(v))
    {
      struct oak_obj_native_record_t* nr = oak_as_native_record(v);
      if (nr->type == s_diag_type)
      {
        oak_diagnostic_box_t* d = nr->instance;
        if (d)
        {
          if (d->message)
            oak_obj_decref(&d->message->obj);
          oak_free(d, OAK_SRC_LOC);
        }
      }
    }
    oak_value_decref(v);
  }
  if (arr->items)
    oak_free(arr->items, OAK_SRC_LOC);
  oak_free(arr, OAK_SRC_LOC);
}

static void parse_box_release_inner(oak_parse_box_t* box)
{
  if (!box || box->disposed)
    return;

  oak_parser_free(&box->parse);
  box->parse.root = null;
  box->parse.error_count = 0;

  if (box->lexer)
  {
    oak_lexer_free(box->lexer);
    box->lexer = null;
  }

  if (box->errors_arr)
  {
    parse_box_free_errors_array(box->errors_arr);
    box->errors_arr = null;
  }

  box->disposed = 1;
}

static void parse_box_destroy(void* instance)
{
  oak_parse_box_t* box = instance;
  if (!box)
    return;
  parse_box_release_inner(box);
  oak_free(box, OAK_SRC_LOC);
}

static enum oak_fn_call_result_t parse_dispose_impl(struct oak_native_ctx_t* ctx,
                                                    const struct oak_value_t* args,
                                                    int argc,
                                                    struct oak_value_t* out)
{
  (void)ctx;
  if (argc < 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_parse_box_t* box = oak_native_instance(args[0]);
  if (!box || box->disposed)
  {
    *out = OAK_VALUE_I32(0);
    return OAK_FN_CALL_OK;
  }

  parse_box_release_inner(box);
  *out = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t oak_parser_parse_impl(
    struct oak_native_ctx_t* ctx,
    const struct oak_value_t* args,
    int argc,
    struct oak_value_t* out)
{
  (void)ctx;
  if (argc != 1 || !oak_is_array(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;

  struct oak_obj_array_t* arr = oak_as_array(args[0]);
  struct oak_lexer_result_t* lexer =
      oak_alloc(sizeof *lexer, OAK_SRC_LOC);
  if (!lexer)
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_list_init(&lexer->tokens);
  oak_arena_init(&lexer->arena, 0);
  lexer->error_count = 0;

  for (usize i = 0; i < arr->length; ++i)
  {
    struct oak_stdlib_token_view_t view;
    if (oak_stdlib_oak_token_inspect(arr->items[i], &view) != 0)
    {
      oak_lexer_free(lexer);
      oak_free(lexer, OAK_SRC_LOC);
      return OAK_FN_CALL_RUNTIME_ERROR;
    }
    if (push_token_from_view(lexer, &view) != 0)
    {
      oak_lexer_free(lexer);
      oak_free(lexer, OAK_SRC_LOC);
      return OAK_FN_CALL_RUNTIME_ERROR;
    }
  }

  oak_parse_box_t* box = oak_alloc(sizeof *box, OAK_SRC_LOC);
  if (!box)
  {
    oak_lexer_free(lexer);
    oak_free(lexer, OAK_SRC_LOC);
    return OAK_FN_CALL_RUNTIME_ERROR;
  }
  memset(box, 0, sizeof *box);
  box->lexer = lexer;
  oak_parse(lexer, OAK_NODE_PROGRAM, &box->parse);

  box->errors_arr = oak_array_new();
  if (!box->errors_arr)
  {
    oak_parser_free(&box->parse);
    oak_lexer_free(lexer);
    oak_free(lexer, OAK_SRC_LOC);
    oak_free(box, OAK_SRC_LOC);
    return OAK_FN_CALL_RUNTIME_ERROR;
  }

  for (int ei = 0; ei < box->parse.error_count; ++ei)
  {
    const struct oak_diagnostic_t* src = &box->parse.errors[ei];
    oak_diagnostic_box_t* db = oak_alloc(sizeof *db, OAK_SRC_LOC);
    if (!db)
    {
      parse_box_free_errors_array(box->errors_arr);
      oak_parser_free(&box->parse);
      oak_lexer_free(lexer);
      oak_free(lexer, OAK_SRC_LOC);
      oak_free(box, OAK_SRC_LOC);
      return OAK_FN_CALL_RUNTIME_ERROR;
    }
    db->line = src->line;
    db->column = src->column;
    usize msg_len = 0;
    while (msg_len < sizeof src->message && src->message[msg_len] != '\0')
      msg_len++;
    db->message = oak_string_new(src->message, msg_len);
    if (!db->message)
    {
      oak_free(db, OAK_SRC_LOC);
      parse_box_free_errors_array(box->errors_arr);
      oak_parser_free(&box->parse);
      oak_lexer_free(lexer);
      oak_free(lexer, OAK_SRC_LOC);
      oak_free(box, OAK_SRC_LOC);
      return OAK_FN_CALL_RUNTIME_ERROR;
    }
    struct oak_value_t dv = oak_native_record_new(s_diag_type, db);
    oak_array_push(box->errors_arr, dv);
    oak_value_decref(dv);
  }

  *out = oak_native_record_new(s_parse_result_type, box);
  return OAK_FN_CALL_OK;
}

void oak_stdlib_register_parser(struct oak_compile_options_t* opts)
{
  if (!opts)
    return;

  struct oak_native_type_t* diag =
      oak_bind_type(opts, OAK_BIND_RECORD, "OakDiagnostic");
  if (!diag)
    return;
  s_diag_type = diag;
  if (oak_bind_field(
          diag,
          &(struct oak_native_field_t){ .name = "line",
                                     .field_type_id = OAK_TYPE_NUMBER,
                                     .getter = diag_get_line,
                                     .setter = null }) < 0)
    return;
  if (oak_bind_field(
          diag,
          &(struct oak_native_field_t){ .name = "column",
                                     .field_type_id = OAK_TYPE_NUMBER,
                                     .getter = diag_get_column,
                                     .setter = null }) < 0)
    return;
  if (oak_bind_field(
          diag,
          &(struct oak_native_field_t){ .name = "message",
                                     .field_type_id = OAK_TYPE_STRING,
                                     .getter = diag_get_message,
                                     .setter = null }) < 0)
    return;

  struct oak_native_type_t* ast =
      oak_bind_type(opts, OAK_BIND_RECORD, "OakAstNode");
  if (!ast)
    return;
  s_ast_type = ast;
  if (oak_bind_field(
          ast,
          &(struct oak_native_field_t){ .name = "kind",
                                     .field_type_id = OAK_TYPE_STRING,
                                     .getter = ast_get_kind,
                                     .setter = null }) < 0)
    return;
  if (oak_bind_field(
          ast,
          &(struct oak_native_field_t){ .name = "child_count",
                                     .field_type_id = OAK_TYPE_NUMBER,
                                     .getter = ast_get_child_count,
                                     .setter = null }) < 0)
    return;
  if (oak_bind_field(
          ast,
          &(struct oak_native_field_t){ .name = "token_value",
                                     .field_type_id = OAK_TYPE_STRING,
                                     .getter = ast_get_token_value,
                                     .setter = null }) < 0)
    return;
  if (oak_bind_field(
          ast,
          &(struct oak_native_field_t){ .name = "is_terminal",
                                     .field_type_id = OAK_TYPE_BOOL,
                                     .getter = ast_get_is_terminal,
                                     .setter = null }) < 0)
    return;
  if (oak_bind_fn(
          opts,
          &(oak_bind_fn_params_t){
              .kind = OAK_BIND_FN_INSTANCE_METHOD,
              .receiver_type_id = ast->type_id,
              .name = "child",
              .impl = ast_child_impl,
              .arity = 1,
              .return_type_id = ast->type_id,
              .return_shape = OAK_BIND_RETURN_SCALAR,
          }) != 0)
    return;
  if (oak_bind_fn(
          opts,
          &(oak_bind_fn_params_t){
              .kind = OAK_BIND_FN_INSTANCE_METHOD,
              .receiver_type_id = ast->type_id,
              .name = "describe",
              .impl = ast_describe_impl,
              .arity = 0,
              .return_type_id = OAK_TYPE_STRING,
              .return_shape = OAK_BIND_RETURN_SCALAR,
          }) != 0)
    return;

  struct oak_native_type_t* pres =
      oak_bind_type(opts, OAK_BIND_RECORD, "OakParseResult");
  if (!pres)
    return;
  pres->destroy_instance = parse_box_destroy;
  s_parse_result_type = pres;
  if (oak_bind_field(
          pres,
          &(struct oak_native_field_t){ .name = "root",
                                     .field_type_id = ast->type_id,
                                     .getter = parse_get_root,
                                     .setter = null }) < 0)
    return;
  if (oak_bind_field(
          pres,
          &(struct oak_native_field_t){ .name = "error_count",
                                     .field_type_id = OAK_TYPE_NUMBER,
                                     .getter = parse_get_error_count,
                                     .setter = null }) < 0)
    return;
  if (oak_bind_fn(
          opts,
          &(oak_bind_fn_params_t){
              .kind = OAK_BIND_FN_INSTANCE_METHOD,
              .receiver_type_id = pres->type_id,
              .name = "errors",
              .impl = parse_errors_impl,
              .arity = 0,
              .return_type_id = diag->type_id,
              .return_shape = OAK_BIND_RETURN_ARRAY,
          }) != 0)
    return;
  if (oak_bind_fn(
          opts,
          &(oak_bind_fn_params_t){
              .kind = OAK_BIND_FN_INSTANCE_METHOD,
              .receiver_type_id = pres->type_id,
              .name = "dispose",
              .impl = parse_dispose_impl,
              .arity = 0,
              .return_type_id = OAK_TYPE_VOID,
              .return_shape = OAK_BIND_RETURN_SCALAR,
          }) != 0)
    return;

  struct oak_native_type_t* parser =
      oak_bind_type(opts, OAK_BIND_RECORD, "OakParser");
  if (!parser)
    return;

  if (oak_bind_fn(
          opts,
          &(oak_bind_fn_params_t){
              .kind = OAK_BIND_FN_STATIC_METHOD,
              .receiver_type_id = parser->type_id,
              .name = "parse",
              .impl = oak_parser_parse_impl,
              .arity = 1,
              .return_type_id = pres->type_id,
              .return_shape = OAK_BIND_RETURN_SCALAR,
          }) != 0)
    return;
}
