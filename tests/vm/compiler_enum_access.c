#include "oak_compiler.h"
#include "oak_count_of.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <string.h>

static enum oak_test_status_t expect_ok(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK(root != null);

  struct oak_chunk_t* chunk = oak_compile(root);
  OAK_CHECK(chunk != null);

  struct oak_vm_t vm;
  oak_vm_init(&vm);
  const enum oak_vm_result_t r = oak_vm_run(&vm, chunk);
  oak_vm_free(&vm);
  oak_chunk_free(chunk);
  oak_parser_free(result);
  oak_lexer_free(lexer);

  OAK_CHECK(r == OAK_VM_OK);
  return OAK_TEST_OK;
}

static enum oak_test_status_t expect_compile_error(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  OAK_CHECK(root != null);

  struct oak_chunk_t* chunk = oak_compile(root);
  OAK_CHECK(chunk == null);

  oak_parser_free(result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

/* Passes when the source fails at either the parse or compile stage. */
static enum oak_test_status_t expect_parse_or_compile_error(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);

  if (!root)
  {
    oak_parser_free(result);
    oak_lexer_free(lexer);
    return OAK_TEST_OK;
  }

  struct oak_chunk_t* chunk = oak_compile(root);
  OAK_CHECK(chunk == null);

  oak_parser_free(result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

/* Comma-separated variants (canonical style). */
OAK_TEST_DECL(QualifiedVariantAccessOk)
{
  return expect_ok(
      "type Color enum { Red, Green, Blue }\n"
      "let c = Color.Green;\n");
}

/* Space-separated variants (no commas) must be rejected. */
OAK_TEST_DECL(SpaceSeparatedVariantsRejected)
{
  return expect_parse_or_compile_error(
      "type Dir enum { North South East West }\n"
      "let a = Dir.North;\n");
}

/* Trailing comma is accepted. */
OAK_TEST_DECL(TrailingCommaOk)
{
  return expect_ok(
      "type Status enum { Off, On, }\n"
      "let s = Status.On;\n");
}

/* Enum variant used in an expression. */
OAK_TEST_DECL(VariantInExpressionOk)
{
  return expect_ok(
      "type Status enum { Off, On }\n"
      "let s = Status.On;\n"
      "let x = s + 10;\n");
}

/* Enum variant used as a function argument. */
OAK_TEST_DECL(VariantAsFnArgOk)
{
  return expect_ok(
      "type Color enum { Red, Green, Blue }\n"
      "fn use_color(c : number) -> number { return c; }\n"
      "use_color(Color.Blue);\n");
}

/* Multiple enums with independent ordinals. */
OAK_TEST_DECL(MultipleEnumsOk)
{
  return expect_ok(
      "type A enum { X, Y }\n"
      "type B enum { P, Q }\n"
      "let x = A.X;\n"
      "let q = B.Q;\n");
}

/* Bare variant identifier must be rejected. */
OAK_TEST_DECL(BareVariantRejected)
{
  return expect_compile_error(
      "type Color enum { Red, Green, Blue }\n"
      "let c = Green;\n");
}

/* Unknown variant on a valid enum is rejected. */
OAK_TEST_DECL(UnknownVariantRejected)
{
  return expect_compile_error(
      "type Color enum { Red, Green, Blue }\n"
      "let c = Color.Purple;\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(QualifiedVariantAccessOk),
    OAK_TEST_ENTRY(SpaceSeparatedVariantsRejected),
    OAK_TEST_ENTRY(TrailingCommaOk),
    OAK_TEST_ENTRY(VariantInExpressionOk),
    OAK_TEST_ENTRY(VariantAsFnArgOk),
    OAK_TEST_ENTRY(MultipleEnumsOk),
    OAK_TEST_ENTRY(BareVariantRejected),
    OAK_TEST_ENTRY(UnknownVariantRejected),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
