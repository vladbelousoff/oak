#include "internal/oak_parser.h"

#include <stdio.h>

static const struct oak_token_t* oak_parser_current_token(
    const struct oak_parser_t* p)
{
  if (!p || p->curr == p->head)
    return null;
  return oak_container_of(p->curr, struct oak_token_t, link);
}

static const struct oak_token_t* oak_parser_last_token(
    const struct oak_parser_t* p)
{
  if (!p || p->head->prev == p->head)
    return null;
  return oak_container_of(p->head->prev, struct oak_token_t, link);
}

static int oak_parser_token_offset_or_eof(const struct oak_parser_t* p,
                                          const struct oak_token_t* token)
{
  if (token)
    return oak_token_offset(token);
  const struct oak_token_t* last = oak_parser_last_token(p);
  if (!last)
    return 0;
  const int size = oak_token_size(last);
  return oak_token_offset(last) + (size > 0 ? size : 1);
}

static int oak_parser_should_replace_detail(
    const struct oak_parser_t* p,
    const struct oak_token_t* token)
{
  if (!p->detail_valid)
    return 1;
  return oak_parser_token_offset_or_eof(p, token) >=
         oak_parser_token_offset_or_eof(p, p->detail_token);
}

static const char* oak_parser_token_kind_display(
    const enum oak_token_kind_t token_kind)
{
  switch (token_kind)
  {
    case OAK_TOKEN_IDENT:
      return "an identifier";
    case OAK_TOKEN_LPAREN:
      return "'('";
    case OAK_TOKEN_RPAREN:
      return "')'";
    case OAK_TOKEN_LBRACE:
      return "'{'";
    case OAK_TOKEN_RBRACE:
      return "'}'";
    case OAK_TOKEN_LBRACKET:
      return "'['";
    case OAK_TOKEN_RBRACKET:
      return "']'";
    case OAK_TOKEN_COMMA:
      return "','";
    case OAK_TOKEN_SEMICOLON:
      return "';'";
    case OAK_TOKEN_COLON:
      return "':'";
    case OAK_TOKEN_EQUAL_EQUAL:
      return "'=='";
    case OAK_TOKEN_BANG_EQUAL:
      return "'!='";
    case OAK_TOKEN_BANG:
      return "'!'";
    case OAK_TOKEN_QUESTION:
      return "'?'";
    case OAK_TOKEN_LESS:
      return "'<'";
    case OAK_TOKEN_LESS_EQUAL:
      return "'<='";
    case OAK_TOKEN_GREATER:
      return "'>'";
    case OAK_TOKEN_GREATER_EQUAL:
      return "'>='";
    case OAK_TOKEN_SLASH:
      return "'/'";
    case OAK_TOKEN_DOUBLE_SLASH:
      return "'//'";
    case OAK_TOKEN_PERCENT:
      return "'%'";
    case OAK_TOKEN_ARROW:
      return "'->'";
    case OAK_TOKEN_DOT:
      return "'.'";
    case OAK_TOKEN_PLUS:
      return "'+'";
    case OAK_TOKEN_MINUS:
      return "'-'";
    case OAK_TOKEN_STAR:
      return "'*'";
    case OAK_TOKEN_ASSIGN:
      return "'='";
    case OAK_TOKEN_PLUS_ASSIGN:
      return "'+='";
    case OAK_TOKEN_MINUS_ASSIGN:
      return "'-='";
    case OAK_TOKEN_STAR_ASSIGN:
      return "'*='";
    case OAK_TOKEN_SLASH_ASSIGN:
      return "'/='";
    case OAK_TOKEN_PERCENT_ASSIGN:
      return "'%='";
    case OAK_TOKEN_INT:
      return "an integer literal";
    case OAK_TOKEN_FLOAT:
      return "a number literal";
    case OAK_TOKEN_STRING:
      return "a string literal";
    case OAK_TOKEN_AT:
      return "'@'";
    case OAK_TOKEN_LET:
      return "'let'";
    case OAK_TOKEN_MUT:
      return "'mut'";
    case OAK_TOKEN_IF:
      return "'if'";
    case OAK_TOKEN_ELSE:
      return "'else'";
    case OAK_TOKEN_WHILE:
      return "'while'";
    case OAK_TOKEN_FOR:
      return "'for'";
    case OAK_TOKEN_FROM:
      return "'from'";
    case OAK_TOKEN_TO:
      return "'to'";
    case OAK_TOKEN_IN:
      return "'in'";
    case OAK_TOKEN_BREAK:
      return "'break'";
    case OAK_TOKEN_CONTINUE:
      return "'continue'";
    case OAK_TOKEN_RETURN:
      return "'return'";
    case OAK_TOKEN_TRUE:
      return "'true'";
    case OAK_TOKEN_FALSE:
      return "'false'";
    case OAK_TOKEN_AND:
      return "'and'";
    case OAK_TOKEN_OR:
      return "'or'";
    case OAK_TOKEN_NOT:
      return "'not'";
    case OAK_TOKEN_RECORD:
      return "'record'";
    case OAK_TOKEN_ENUM:
      return "'enum'";
    case OAK_TOKEN_FN:
      return "'fn'";
    case OAK_TOKEN_AS:
      return "'as'";
    case OAK_TOKEN_NEW:
      return "'new'";
    case OAK_TOKEN_SELF:
      return "'self'";
    case OAK_TOKEN_IMPORT:
      return "'import'";
    case OAK_TOKEN_TRAIT:
      return "'trait'";
    case OAK_TOKEN_IMPL:
      return "'impl'";
    case OAK_TOKEN_WEAK:
      return "'weak'";
    case OAK_TOKEN_NONE:
      return "'none'";
  }
  return "a token";
}

static const char* oak_parser_token_display(const struct oak_token_t* token)
{
  if (!token)
    return "end of input";
  if (oak_token_kind(token) == OAK_TOKEN_IDENT)
    return oak_token_text(token);
  return oak_parser_token_kind_display(oak_token_kind(token));
}

static const char* oak_parser_node_display(const enum oak_node_kind_t kind)
{
  switch (kind)
  {
    case OAK_NODE_PROGRAM:
      return "a program";
    case OAK_NODE_PROGRAM_ITEM:
      return "a declaration or statement";
    case OAK_NODE_RECORD_DECL:
    case OAK_NODE_RECORD_DECL_EMPTY:
      return "a record declaration";
    case OAK_NODE_TYPE_NAME:
      return "a type name";
    case OAK_NODE_RECORD_FIELD_DECL:
      return "a record field";
    case OAK_NODE_RECORD_FIELDS:
      return "record fields";
    case OAK_NODE_ENUM_DECL:
      return "an enum declaration";
    case OAK_NODE_IDENT:
      return "an identifier";
    case OAK_NODE_STMT:
      return "a statement";
    case OAK_NODE_STMT_EXPR:
      return "an expression statement";
    case OAK_NODE_EXPR:
    case OAK_NODE_EXPR_PRIMARY:
      return "an expression";
    case OAK_NODE_INT:
      return "an integer literal";
    case OAK_NODE_FLOAT:
      return "a number literal";
    case OAK_NODE_STRING:
      return "a string literal";
    case OAK_NODE_TRUE:
    case OAK_NODE_FALSE:
      return "a boolean literal";
    case OAK_NODE_NONE_LITERAL:
      return "'none'";
    case OAK_NODE_STMT_ASSIGNMENT:
      return "an assignment";
    case OAK_NODE_STMT_LET_ASSIGNMENT:
      return "a let assignment";
    case OAK_NODE_FN_DECL:
    case OAK_NODE_FN_PROTO:
      return "a function declaration";
    case OAK_NODE_FN_HEAD:
      return "a function name";
    case OAK_NODE_FN_PARAMS_AND_RET:
      return "function parameters";
    case OAK_NODE_FN_PARAM_LIST:
      return "a function parameter list";
    case OAK_NODE_FN_PARAMS:
      return "function parameters";
    case OAK_NODE_FN_RETURN_TYPE:
      return "a return type";
    case OAK_NODE_FN_PARAM:
      return "a function parameter";
    case OAK_NODE_FN_PARAM_SELF:
      return "a self parameter";
    case OAK_NODE_FN_CALL:
      return "a function call";
    case OAK_NODE_FN_CALL_ARG:
      return "a function argument";
    case OAK_NODE_STMT_RETURN:
      return "a return statement";
    case OAK_NODE_STMT_IF:
      return "an if statement";
    case OAK_NODE_ELSE_BLOCK:
      return "an else block";
    case OAK_NODE_STMT_WHILE:
      return "a while statement";
    case OAK_NODE_STMT_FOR_FROM:
    case OAK_NODE_STMT_FOR_IN:
      return "a for statement";
    case OAK_NODE_STMT_BREAK:
      return "a break statement";
    case OAK_NODE_STMT_CONTINUE:
      return "a continue statement";
    case OAK_NODE_TYPE_ARRAY:
      return "an array type";
    case OAK_NODE_TYPE_MAP:
      return "a map type";
    case OAK_NODE_EXPR_EMPTY_ARRAY:
      return "an empty array literal";
    case OAK_NODE_EXPR_EMPTY_MAP:
      return "an empty map literal";
    case OAK_NODE_EXPR_ARRAY_LITERAL:
      return "an array literal";
    case OAK_NODE_EXPR_MAP_LITERAL:
      return "a map literal";
    case OAK_NODE_INDEX_ACCESS:
      return "an index expression";
    case OAK_NODE_EXPR_CAST:
      return "a cast expression";
    case OAK_NODE_EXPR_RECORD_LITERAL:
      return "a record literal";
    case OAK_NODE_ENUM_VARIANTS:
      return "enum variants";
    case OAK_NODE_BLOCK:
      return "a block";
    case OAK_NODE_IMPORT_DECL:
    case OAK_NODE_IMPORT_SELECTIVE:
    case OAK_NODE_IMPORT_WILDCARD:
      return "an import declaration";
    case OAK_NODE_IMPORT_PATH:
      return "an import path";
    case OAK_NODE_IMPORT_NAMES:
      return "import names";
    case OAK_NODE_IMPORT_NAME:
      return "an import name";
    case OAK_NODE_IMPORT_ALIAS:
      return "an import alias";
    case OAK_NODE_TRAIT_DECL:
      return "a trait declaration";
    case OAK_NODE_TRAIT_MEMBERS:
      return "trait members";
    case OAK_NODE_METHOD_DECL:
    case OAK_NODE_METHOD_PROTO:
      return "a method declaration";
    case OAK_NODE_METHOD_HEAD:
      return "a method name";
    case OAK_NODE_ATTR:
      return "an attribute";
    case OAK_NODE_ATTR_DECL:
      return "an attributed declaration";
    case OAK_NODE_TYPE_GENERIC:
      return "a generic type";
    case OAK_NODE_TYPE_ARGS:
      return "type arguments";
    case OAK_NODE_EXPR_FN:
      return "an anonymous function";
    case OAK_NODE_TYPE_FN:
      return "a function type";
    case OAK_NODE_TYPE_FN_PARAMS:
      return "function type parameters";
    default:
      return "valid syntax";
  }
}

static void oak_parser_set_detail(struct oak_parser_t* p,
                                  const enum oak_node_kind_t context,
                                  const enum oak_token_kind_t expected_token,
                                  const enum oak_node_kind_t expected_node)
{
  const struct oak_token_t* token = oak_parser_current_token(p);
  if (!oak_parser_should_replace_detail(p, token))
    return;
  p->detail_token = token;
  p->detail_valid = 1;
  p->detail_has_expected_token = expected_node == OAK_NODE_NONE;
  p->detail_expected_token = expected_token;
  p->detail_expected_node = expected_node;
  p->detail_context = context;
}

void oak_parser_detail_expected_token(struct oak_parser_t* p,
                                      const enum oak_node_kind_t context,
                                      const enum oak_token_kind_t expected)
{
  oak_parser_set_detail(p, context, expected, OAK_NODE_NONE);
}

void oak_parser_detail_expected_node(struct oak_parser_t* p,
                                     const enum oak_node_kind_t context,
                                     const enum oak_node_kind_t expected)
{
  oak_parser_set_detail(p, context, (enum oak_token_kind_t)0, expected);
}

static void oak_parser_emit_detail(struct oak_parser_t* parser,
                                   struct oak_parser_result_t* out)
{
  if (out->error_count >= OAK_MAX_DIAGNOSTICS)
    return;

  const struct oak_token_t* token =
      parser->detail_valid ? parser->detail_token
                           : oak_parser_current_token(parser);
  struct oak_diagnostic_t* d = &out->errors[out->error_count++];
  if (token)
  {
    d->line = oak_token_line(token);
    d->column = oak_token_column(token);
  }
  else
  {
    const struct oak_token_t* last = oak_parser_last_token(parser);
    d->line = last ? oak_token_line(last) : 0;
    if (last)
    {
      const int size = oak_token_size(last);
      d->column = oak_token_column(last) + (size > 0 ? size : 1);
    }
    else
    {
      d->column = 0;
    }
  }

  if (parser->detail_has_expected_token)
  {
    snprintf(d->message,
             sizeof(d->message),
             "expected %s while parsing %s, got %s",
             oak_parser_token_kind_display(parser->detail_expected_token),
             oak_parser_node_display(parser->detail_context),
             oak_parser_token_display(token));
    return;
  }

  if (parser->detail_expected_node)
  {
    snprintf(d->message,
             sizeof(d->message),
             "expected %s while parsing %s, got %s",
             oak_parser_node_display(parser->detail_expected_node),
             oak_parser_node_display(parser->detail_context),
             oak_parser_token_display(token));
    return;
  }

  snprintf(d->message,
           sizeof(d->message),
           "unexpected %s",
           oak_parser_token_display(token));
}

struct oak_ast_node_t* oak_parser_parse_rule(struct oak_parser_t* p,
                                             const enum oak_node_kind_t kind)
{
  if (kind == OAK_NODE_NONE)
    return null;
  if (p->curr == p->head)
    return null;

  switch (oak_grammar[kind].op)
  {
    case OAK_GRAMMAR_TOKEN:
      return oak_parser_parse_token(p, kind);
    case OAK_GRAMMAR_CHOICE:
      return oak_parser_parse_choice(p, kind);
    case OAK_GRAMMAR_PRATT:
      return oak_parser_parse_pratt(p, kind, 0);
    case OAK_GRAMMAR_BINARY:
    case OAK_GRAMMAR_UNARY:
    case OAK_GRAMMAR_SEQUENCE:
      return oak_parser_parse_rules(p, kind);
  }
  return null;
}

void oak_parse(const struct oak_lexer_result_t* lexer,
               const enum oak_node_kind_t kind,
               struct oak_parser_result_t* out,
               struct oak_allocator_t* allocator)
{
  oak_arena_init(&out->arena, 0, allocator);

  const struct oak_list_entry_t* tokens = oak_lexer_tokens(lexer);
  struct oak_parser_t parser = {
    .head = tokens,
    .curr = tokens->next,
    .arena = &out->arena,
  };

  out->root = oak_parser_parse_rule(&parser, kind);

  if (parser.curr != parser.head)
  {
    oak_parser_emit_detail(&parser, out);
    out->root = null;
  }
}

struct oak_ast_node_t* oak_parser_root(const struct oak_parser_result_t* result)
{
  return result ? result->root : null;
}

int oak_parser_error_count(const struct oak_parser_result_t* result)
{
  return result ? result->error_count : 0;
}

const struct oak_diagnostic_t*
oak_parser_errors(const struct oak_parser_result_t* result)
{
  return result ? result->errors : null;
}

void oak_parser_free(struct oak_parser_result_t* result)
{
  if (!result)
    return;
  oak_arena_free(&result->arena);
}
