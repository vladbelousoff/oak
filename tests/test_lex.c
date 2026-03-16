#include "oak_common.h"
#include "oak_lex.h"
#include "oak_log.h"
#include "oak_mem.h"
#include "oak_tok.h"

#include <math.h>
#include <string.h>

typedef struct
{
  const char* name;
  oak_result_t (*fn)(void);
} oak_test_t;

typedef struct
{
  oak_tok_type_t type;
  int line;
  int column;
  int pos;
  union
  {
    char str[64];
    float f_val;
    int i_val;
  };
} tok_attr_t;

static oak_result_t test_token(const oak_tok_t* tok, const tok_attr_t* attr)
{
  if (tok->type != attr->type)
    return OAK_FAILURE;
  if (tok->line != attr->line)
    return OAK_FAILURE;
  if (tok->column != attr->column)
    return OAK_FAILURE;
  if (tok->pos != attr->pos)
    return OAK_FAILURE;

  if (tok->type == OAK_TOK_INT_NUM)
  {
    if (attr->i_val != *(int*)tok->buf)
    {
      return OAK_FAILURE;
    }
  }

  if (tok->type == OAK_TOK_FLOAT_NUM)
  {
    if (fabsf(attr->f_val - *(float*)tok->buf) > 0.0001f)
    {
      return OAK_FAILURE;
    }
  }

  if (tok->type == OAK_TOK_STRING)
  {
    if (strcmp(tok->buf, attr->str) != 0)
    {
      return OAK_FAILURE;
    }
  }

  return OAK_SUCCESS;
}

static oak_result_t
test_tokens(const oak_lex_t* lex, const tok_attr_t* attrs, const size_t count)
{
  size_t tok_index;
  oak_list_entry_t* tok_entry;
  oak_list_for_each_indexed(tok_index, tok_entry, &lex->tokens)
  {
    if (tok_index >= count)
    {
      return OAK_FAILURE;
    }

    const oak_tok_t* tok = oak_container_of(tok_entry, oak_tok_t, link);
    const tok_attr_t* attr = &attrs[tok_index];
    if (test_token(tok, attr) != OAK_SUCCESS)
    {
      return OAK_FAILURE;
    }
  }

  return OAK_SUCCESS;
}

static oak_result_t empty_string(void)
{
  oak_lex_t lex;
  oak_lex_tokenize("", &lex);
  const oak_result_t result =
      oak_list_empty(&lex.tokens) ? OAK_SUCCESS : OAK_FAILURE;
  oak_lex_cleanup(&lex);
  return result;
}

static oak_result_t one_integer(void)
{
  oak_lex_t lex;
  oak_lex_tokenize("1000", &lex);

  static tok_attr_t tok_attr[] = {
    {
        .type = OAK_TOK_INT_NUM,
        .line = 1,
        .column = 1,
        .pos = 1,
        .i_val = 1000,
    },
  };

  const oak_result_t result = test_tokens(&lex, tok_attr, 1);
  oak_lex_cleanup(&lex);

  return result;
}

static oak_result_t one_float(void)
{
  oak_lex_t lex;
  oak_lex_tokenize("77.23", &lex);

  static tok_attr_t tok_attr[] = {
    {
        .type = OAK_TOK_FLOAT_NUM,
        .line = 1,
        .column = 1,
        .pos = 1,
        .f_val = 77.23f,
    },
  };

  const oak_result_t result = test_tokens(&lex, tok_attr, 1);
  oak_lex_cleanup(&lex);

  return result;
}

static oak_result_t arithmetic_expression(void)
{
  oak_lex_t lex;
  oak_lex_tokenize("1 + 2 * 3", &lex);

  static tok_attr_t tok_attr[] = {
    {
        .type = OAK_TOK_INT_NUM,
        .line = 1,
        .column = 1,
        .pos = 1,
        .i_val = 1,
    },
    {
        .type = OAK_TOK_PLUS,
        .line = 1,
        .column = 3,
        .pos = 3,
    },
    {
        .type = OAK_TOK_INT_NUM,
        .line = 1,
        .column = 5,
        .pos = 5,
        .i_val = 2,
    },
    {
        .type = OAK_TOK_STAR,
        .line = 1,
        .column = 7,
        .pos = 7,
    },
    {
        .type = OAK_TOK_INT_NUM,
        .line = 1,
        .column = 9,
        .pos = 9,
        .i_val = 3,
    },
  };

  const size_t n = sizeof(tok_attr) / sizeof(tok_attr[0]);
  const oak_result_t result = test_tokens(&lex, tok_attr, n);
  oak_lex_cleanup(&lex);

  return result;
}

static oak_result_t one_ident(void)
{
  oak_lex_t lex;
  oak_lex_tokenize("variable", &lex);

  static tok_attr_t tok_attr[] = {
    {
        .type = OAK_TOK_IDENT,
        .line = 1,
        .column = 1,
        .pos = 1,
        .str = "variable",
    },
  };

  const oak_result_t result = test_tokens(&lex, tok_attr, 1);
  oak_lex_cleanup(&lex);

  return result;
}

static oak_result_t keywords(void)
{
  oak_lex_t lex;
  oak_lex_tokenize(
      "let mut if else while for break continue return true false and or not",
      &lex);

  static tok_attr_t tok_attr[] = {
    {
        .type = OAK_TOK_LET,
        .line = 1,
        .column = 1,
        .pos = 1,
    },
    {
        .type = OAK_TOK_MUT,
        .line = 1,
        .column = 5,
        .pos = 5,
    },
    {
        .type = OAK_TOK_IF,
        .line = 1,
        .column = 9,
        .pos = 9,
    },
    {
        .type = OAK_TOK_ELSE,
        .line = 1,
        .column = 12,
        .pos = 12,
    },
    {
        .type = OAK_TOK_WHILE,
        .line = 1,
        .column = 17,
        .pos = 17,
    },
    {
        .type = OAK_TOK_FOR,
        .line = 1,
        .column = 23,
        .pos = 23,
    },
    {
        .type = OAK_TOK_BREAK,
        .line = 1,
        .column = 27,
        .pos = 27,
    },
    {
        .type = OAK_TOK_CONTINUE,
        .line = 1,
        .column = 33,
        .pos = 33,
    },
    {
        .type = OAK_TOK_RETURN,
        .line = 1,
        .column = 42,
        .pos = 42,
    },
    {
        .type = OAK_TOK_TRUE,
        .line = 1,
        .column = 49,
        .pos = 49,
    },
    {
        .type = OAK_TOK_FALSE,
        .line = 1,
        .column = 54,
        .pos = 54,
    },
    {
        .type = OAK_TOK_AND,
        .line = 1,
        .column = 60,
        .pos = 60,
    },
    {
        .type = OAK_TOK_OR,
        .line = 1,
        .column = 64,
        .pos = 64,
    },
    {
        .type = OAK_TOK_NOT,
        .line = 1,
        .column = 67,
        .pos = 67,
    },
  };

  const size_t n = sizeof(tok_attr) / sizeof(tok_attr[0]);
  const oak_result_t result = test_tokens(&lex, tok_attr, n);
  oak_lex_cleanup(&lex);

  return result;
}

static oak_test_t tests[] = {
  {
      "Empty",
      empty_string,
  },
  {
      "Integer",
      one_integer,
  },
  {
      "Float",
      one_float,
  },
  {
      "Arithmetic Expression",
      arithmetic_expression,
  },
  {
      "Identifier",
      one_ident,
  },
  {
      "Keywords",
      keywords,
  },
};

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;

  oak_result_t result = OAK_SUCCESS;
  oak_mem_init();

  const int test_count = sizeof(tests) / sizeof(tests[0]);
  for (int i = 0; i < test_count; ++i)
  {
    const oak_test_t* test = &tests[i];
    oak_log(OAK_LOG_INF, "Running test: %s", test->name);
    if (test->fn() != OAK_SUCCESS)
    {
      result = OAK_FAILURE;
      break;
    }
  }

  oak_mem_shutdown();
  return result == OAK_SUCCESS ? 0 : 1;
}
