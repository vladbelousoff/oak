#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <string.h>

static enum oak_test_status_t run_ok(const char* source)
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

static enum oak_test_status_t compile_fails(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t result = { 0 };
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = { 0 };
  oak_compile(root, &cr);
  OAK_CHECK(cr.chunk == null);

  oak_parser_free(&result);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(FnArgMap)
{
  /* Pass a [string:number] map to a function that looks up a key. */
  const char* source =
      "fn get_x(m: [string:number]) -> number { return m['x']; }\n"
      "let m = ['x': 42, 'y': 7];\n"
      "print(get_x(m));";
  OAK_CHECK(run_ok(source) == OAK_TEST_OK);

  /* Pass a mutable map and insert into it from inside the function. */
  const char* source_mut = "fn insert(mut m: [string:number]) -> number {\n"
                           "  m['z'] = 100;\n"
                           "  return m['z'];\n"
                           "}\n"
                           "let mut scores = ['a': 1];\n"
                           "print(insert(scores));";
  OAK_CHECK(run_ok(source_mut) == OAK_TEST_OK);

  /* Passing a number[] where [string:number] is expected must fail. */
  const char* bad =
      "fn get_x(m: [string:number]) -> number { return m['x']; }\n"
      "let a = [1, 2, 3];\n"
      "print(get_x(a));";
  OAK_CHECK(compile_fails(bad) == OAK_TEST_OK);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(FnArgMap)
