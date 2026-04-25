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
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = { 0 };
  oak_compile(root, &cr);
  OAK_CHECK(cr.chunk != null);

  struct oak_vm_t vm;
  oak_vm_init(&vm);
  const enum oak_vm_result_t r = oak_vm_run(&vm, cr.chunk);
  oak_vm_free(&vm);
  oak_compile_result_free(&cr);

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  OAK_CHECK(r == OAK_VM_OK);
  return OAK_TEST_OK;
}

static struct oak_chunk_t* try_compile(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  if (!root)
  {
    oak_parser_free(&result);
    oak_lexer_free(lexer);
    return null;
  }
  struct oak_compile_result_t cr = { 0 };
  oak_compile(root, &cr);
  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return cr.chunk;
}

OAK_TEST_DECL(ArrayLiteral)
{
  /* Numeric literal with a function call inside an element expression. */
  const enum oak_test_status_t r1 =
      run_program("fn sub(a: number, b: number) -> number { return a - b; }\n"
                  "let nums = [1, 54, 13, 45 - sub(5, 3)];\n"
                  "print(nums.size());\n"
                  "print(nums[0]);\n"
                  "print(nums[3]);\n");
  OAK_CHECK(r1 == OAK_TEST_OK);

  /* String literal infers element type from the first element. */
  const enum oak_test_status_t r2 =
      run_program("let words = ['sda', 'ada', 'ert', 'rer'];\n"
                  "print(words.size());\n"
                  "print(words[0]);\n"
                  "print(words[3]);\n");
  OAK_CHECK(r2 == OAK_TEST_OK);

  /* Mutable literal: push and indexed assignment work. */
  const enum oak_test_status_t r3 = run_program("let mut a = [10, 20, 30];\n"
                                                "a.push(40);\n"
                                                "a[0] = 99;\n"
                                                "print(a[0]);\n"
                                                "print(a[3]);\n");
  OAK_CHECK(r3 == OAK_TEST_OK);

  /* Mixed-type literal is rejected at compile time. */
  struct oak_chunk_t* bad = try_compile("let bad = [1, 'two', 3];");
  OAK_CHECK(bad == null);

  /* Pushing a wrong-typed value to a literal-inferred array is rejected. */
  bad = try_compile("let mut a = [1, 2, 3];\n"
                    "a.push('oops');\n");
  OAK_CHECK(bad == null);

  /* Indexing a literal-inferred array with a wrong-typed value is rejected. */
  bad = try_compile("let mut a = [1, 2, 3];\n"
                    "a[0] = 'oops';\n");
  OAK_CHECK(bad == null);

  /* Empty literal still requires a cast. */
  bad = try_compile("let mut a = [];");
  OAK_CHECK(bad == null);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(ArrayLiteral)
