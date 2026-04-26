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

OAK_TEST_DECL(FnArgRecord)
{
  /* Pass a record to a function that reads its fields. */
  const char* source = "record Vec2 { x: number; y: number; }\n"
                       "fn manhattan(v: Vec2) -> number { return v.x + v.y; }\n"
                       "let p = new Vec2 { x: 3, y: 4 };\n"
                       "print(manhattan(p));";
  OAK_CHECK(run_ok(source) == OAK_TEST_OK);

  /* Multiple record args. */
  const char* source_two =
      "record Rect { w: number; h: number; }\n"
      "fn area(r: Rect) -> number { return r.w * r.h; }\n"
      "fn perimeter(r: Rect) -> number { return r.w + r.w + r.h + r.h; }\n"
      "let r = new Rect { w: 5, h: 3 };\n"
      "print(area(r));\n"
      "print(perimeter(r));";
  OAK_CHECK(run_ok(source_two) == OAK_TEST_OK);

  /* Passing the wrong record type must fail at compile time. */
  const char* bad = "record A { x: number; }\n"
                    "record B { y: number; }\n"
                    "fn take_a(v: A) -> number { return v.x; }\n"
                    "let b = new B { y: 1 };\n"
                    "print(take_a(b));";
  OAK_CHECK(compile_fails(bad) == OAK_TEST_OK);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(FnArgRecord)
