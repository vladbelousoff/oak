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

OAK_TEST_DECL(RecordFieldShorthand)
{
  /* Basic shorthand: { foo } expands to { foo: foo }. */
  const char* source = "record Vec2 { x: number; y: number; }\n"
                       "let x = 3;\n"
                       "let y = 4;\n"
                       "let v = new Vec2 { x, y };\n"
                       "print(v.x);\n"
                       "print(v.y);";
  OAK_CHECK(run_ok(source) == OAK_TEST_OK);

  /* Mixed: some fields use shorthand, others explicit. */
  const char* mixed = "record Rect { w: number; h: number; }\n"
                      "let w = 5;\n"
                      "let r = new Rect { w, h: 3 };\n"
                      "print(r.w);\n"
                      "print(r.h);";
  OAK_CHECK(run_ok(mixed) == OAK_TEST_OK);

  /* Shorthand with mutable binding. */
  const char* mut_source = "record Point { x: number; y: number; }\n"
                           "let x = 10;\n"
                           "let y = 20;\n"
                           "let mut p = new Point { x, y };\n"
                           "p.x = p.x + 1;\n"
                           "print(p.x);";
  OAK_CHECK(run_ok(mut_source) == OAK_TEST_OK);

  /* Shorthand with undefined variable must fail at compile time. */
  const char* bad = "record Foo { x: number; }\n"
                    "let f = new Foo { x };\n"
                    "print(f.x);";
  OAK_CHECK(compile_fails(bad) == OAK_TEST_OK);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(RecordFieldShorthand)
