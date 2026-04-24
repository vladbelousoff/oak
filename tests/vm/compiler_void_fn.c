#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_vm.h"

#include <string.h>

static enum oak_test_status_t expect_ok(const char* source)
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

static enum oak_test_status_t expect_compile_error(const char* source)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize(source, strlen(source));
  struct oak_parser_result_t result = {0};
  oak_parse(lexer, OAK_NODE_PROGRAM, &result);
  const struct oak_ast_node_t* root = oak_parser_root(&result);
  OAK_CHECK(root != null);

  struct oak_compile_result_t cr = {0};
  oak_compile(root, &cr);
  OAK_CHECK(cr.chunk == null);

  oak_parser_free(&result);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

OAK_TEST_DECL(VoidFnOmittedArrowOk)
{
  return expect_ok(
      "fn side() { return; }\n"
      "fn main() { side(); }\n"
      "main();\n");
}

OAK_TEST_DECL(ExplicitVoidReturnTypeRejected)
{
  return expect_compile_error("fn f() -> void { return; }\n");
}

OAK_TEST_DECL(VoidFnImplicitReturnAtEndOk)
{
  return expect_ok(
      "fn noop() { }\n"
      "noop();\n");
}

OAK_TEST_DECL(VoidFnCannotReturnValue)
{
  return expect_compile_error(
      "fn bad() { return 1; }\n");
}

OAK_TEST_DECL(NonVoidFnMustReturnValue)
{
  return expect_compile_error(
      "fn need() -> number { return; }\n");
}

OAK_TEST_DECL(CannotUseVoidInLet)
{
  return expect_compile_error(
      "fn v() { }\n"
      "let x = v();\n");
}

OAK_TEST_DECL(VoidCallAsStmtOk)
{
  return expect_ok(
      "fn v() { }\n"
      "v();\n");
}

OAK_TEST_DECL(PrintTreatedAsVoid)
{
  return expect_compile_error(
      "let x = print(1);\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(VoidFnOmittedArrowOk),
    OAK_TEST_ENTRY(ExplicitVoidReturnTypeRejected),
    OAK_TEST_ENTRY(VoidFnImplicitReturnAtEndOk),
    OAK_TEST_ENTRY(VoidFnCannotReturnValue),
    OAK_TEST_ENTRY(NonVoidFnMustReturnValue),
    OAK_TEST_ENTRY(CannotUseVoidInLet),
    OAK_TEST_ENTRY(VoidCallAsStmtOk),
    OAK_TEST_ENTRY(PrintTreatedAsVoid),
  };
  return oak_test_run(
      tests, (int)(sizeof(tests) / sizeof(tests[0])));
}
