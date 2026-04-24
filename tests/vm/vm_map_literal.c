#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <string.h>

static enum oak_test_status_t run_program(const char* source)
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

static struct oak_chunk_t* try_compile(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t* result = oak_parse(lexer, OAK_NODE_PROGRAM);
  const struct oak_ast_node_t* root = oak_parser_root(result);
  if (!root)
  {
    oak_parser_free(result);
    oak_lexer_free(lexer);
    return null;
  }
  struct oak_chunk_t* chunk = oak_compile(root);
  oak_parser_free(result);
  oak_lexer_free(lexer);
  return chunk;
}

OAK_TEST_DECL(MapLiteral)
{
  /* string -> number, with a function call inside a value expression. */
  const enum oak_test_status_t r1 = run_program(
      "fn dbl(x: number) -> number { return x * 2; }\n"
      "let scores = ['alice': 10, 'bob': 20 + dbl(5), 'carol': 7];\n"
      "print(scores.size());\n"
      "print(scores['alice']);\n"
      "print(scores['bob']);\n"
      "print(scores['carol']);\n");
  OAK_CHECK(r1 == OAK_TEST_OK);

  /* number -> string keys/values inferred from first entry. */
  const enum oak_test_status_t r2 = run_program(
      "let by_id = [1: 'alpha', 2: 'beta', 3: 'gamma'];\n"
      "print(by_id.size());\n"
      "print(by_id[1]);\n"
      "print(by_id[3]);\n");
  OAK_CHECK(r2 == OAK_TEST_OK);

  /* Mutable literal: indexed assign and insert work. */
  const enum oak_test_status_t r3 = run_program(
      "let mut m = ['x': 1, 'y': 2];\n"
      "m['x'] = m['x'] + 10;\n"
      "m['z'] = 99;\n"
      "print(m['x']);\n"
      "print(m['z']);\n"
      "print(m.size());\n");
  OAK_CHECK(r3 == OAK_TEST_OK);

  /* Mismatched value type rejected at compile time. */
  struct oak_chunk_t* bad = try_compile("let bad = ['a': 1, 'b': 'two'];");
  OAK_CHECK(bad == null);

  /* Mismatched key type rejected at compile time. */
  bad = try_compile("let bad = ['a': 1, 2: 3];");
  OAK_CHECK(bad == null);

  /* Indexed assignment with a wrong-typed key is rejected at compile time. */
  bad = try_compile("let mut m = ['a': 1, 'b': 2];\n"
                    "m[3] = 4;\n");
  OAK_CHECK(bad == null);

  /* Indexed assignment with a wrong-typed value is rejected at compile time. */
  bad = try_compile("let mut m = ['a': 1, 'b': 2];\n"
                    "m['c'] = 'oops';\n");
  OAK_CHECK(bad == null);

  /* Empty literal still requires a cast. */
  bad = try_compile("let mut m = [:];");
  OAK_CHECK(bad == null);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(MapLiteral)
