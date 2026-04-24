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
  struct oak_parser_result_t result = {0};
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = {0};
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
  struct oak_parser_result_t result = {0};
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  if (!root)
  {
    oak_parser_free(&result);
    oak_lexer_free(lexer);
    return null;
  }
  struct oak_compile_result_t cr = {0};
  oak_compile(root, &cr);
  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return cr.chunk;
}

OAK_TEST_DECL(MapHasDelete)
{
  /* has() returns bool, delete() removes and returns whether it existed. */
  const char* source = "let mut m = [:] as [string:number];\n"
                       "m['a'] = 1;\n"
                       "m['b'] = 2;\n"
                       "print(m.has('a'));\n"
                       "print(m.has('c'));\n"
                       "print(m.delete('a'));\n"
                       "print(m.delete('a'));\n"
                       "print(m.has('a'));\n"
                       "print(m.size());\n";
  return run_program(source);
}

OAK_TEST_DECL(MapForIn)
{
  /* `for k, v in map` iterates entries; `for v in map` iterates keys. */
  const char* source = "let mut m = [:] as [string:number];\n"
                       "m['a'] = 1;\n"
                       "m['b'] = 2;\n"
                       "m['c'] = 3;\n"
                       "let mut sum = 0;\n"
                       "for k, v in m {\n"
                       "  sum = sum + v;\n"
                       "}\n"
                       "print(sum);\n"
                       "let mut count = 0;\n"
                       "for k in m {\n"
                       "  count = count + 1;\n"
                       "}\n"
                       "print(count);\n";
  return run_program(source);
}

OAK_TEST_DECL(ArrayForIn)
{
  /* `for v in arr` iterates values; `for i, v in arr` exposes the index. */
  const char* source = "let mut a = [] as number[];\n"
                       "a.push(10);\n"
                       "a.push(20);\n"
                       "a.push(30);\n"
                       "let mut sum = 0;\n"
                       "for v in a {\n"
                       "  sum = sum + v;\n"
                       "}\n"
                       "print(sum);\n"
                       "let mut paired = 0;\n"
                       "for i, v in a {\n"
                       "  paired = paired + i + v;\n"
                       "}\n"
                       "print(paired);\n";
  return run_program(source);
}

OAK_TEST_DECL(ForInBreakContinue)
{
  /* break/continue work inside for-in loops and clean up the stack. */
  const char* source = "let mut a = [] as number[];\n"
                       "a.push(1);\n"
                       "a.push(2);\n"
                       "a.push(3);\n"
                       "a.push(4);\n"
                       "let mut s = 0;\n"
                       "for v in a {\n"
                       "  if v == 2 { continue; }\n"
                       "  if v == 4 { break; }\n"
                       "  s = s + v;\n"
                       "}\n"
                       "print(s);\n";
  return run_program(source);
}

OAK_TEST_DECL(MapMethodTypes)
{
  /* Wrong key type for has() is rejected at compile time. */
  struct oak_chunk_t* chunk =
      try_compile("let mut m = [:] as [string:number];\n"
                  "print(m.has(1));\n");
  OAK_CHECK(chunk == null);

  /* Wrong key type for delete() is rejected at compile time. */
  chunk = try_compile("let mut m = [:] as [string:number];\n"
                      "print(m.delete(1));\n");
  OAK_CHECK(chunk == null);

  /* Unknown method on a map is rejected. */
  chunk = try_compile("let mut m = [:] as [string:number];\n"
                      "m.nope();\n");
  OAK_CHECK(chunk == null);

  /* Iterating a non-collection is rejected at compile time. */
  chunk = try_compile("let n = 5;\n"
                      "for v in n { print(v); }\n");
  OAK_CHECK(chunk == null);

  /* Well-typed program compiles. */
  chunk = try_compile("let mut m = [:] as [string:number];\n"
                      "m['a'] = 1;\n"
                      "if m.has('a') { print(m['a']); }\n"
                      "if m.delete('a') { print(m.size()); }\n");
  OAK_CHECK(chunk != null);
  oak_chunk_free(chunk);

  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(MapHasDelete),
    OAK_TEST_ENTRY(MapForIn),
    OAK_TEST_ENTRY(ArrayForIn),
    OAK_TEST_ENTRY(ForInBreakContinue),
    OAK_TEST_ENTRY(MapMethodTypes),
  };
  return oak_test_run(tests, sizeof(tests) / sizeof(tests[0]));
}
