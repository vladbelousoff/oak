#include "oak_count_of.h"
#include "oak_test_pipeline.h"

/* String instance methods are registered by the compiler as builtin string
 * methods, so they are callable with no import. The `if BAD { print([1][3]); }`
 * idiom forces a runtime out-of-bounds error when a result is wrong, so
 * expect_ok passes only when every check holds. */
OAK_TEST_DECL(StringCaseAndTrim)
{
  OAK_CHECK(expect_ok(
      "if 'Hello'.upper() != 'HELLO' { print([1][3]); }\n"
      "if 'Hello'.lower() != 'hello' { print([1][3]); }\n"
      "if '  hi  '.trim() != 'hi' { print([1][3]); }\n"
      "if ''.upper() != '' { print([1][3]); }\n"
      "if '   '.trim() != '' { print([1][3]); }\n"
      "if '\\t x \\n'.trim() != 'x' { print([1][3]); }\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(StringSearchPredicates)
{
  OAK_CHECK(expect_ok(
      "if !'hello world'.contains('world') { print([1][3]); }\n"
      "if 'hello world'.contains('xyz') { print([1][3]); }\n"
      "if !'hello'.starts_with('he') { print([1][3]); }\n"
      "if 'hello'.starts_with('lo') { print([1][3]); }\n"
      "if !'hello'.ends_with('lo') { print([1][3]); }\n"
      "if 'hello'.ends_with('he') { print([1][3]); }\n"
      "if 'hello'.index_of('l') != 2 { print([1][3]); }\n"
      "if 'hello'.index_of('z') != -1 { print([1][3]); }\n"
      "if !'anything'.contains('') { print([1][3]); }\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(StringTransforms)
{
  OAK_CHECK(expect_ok(
      "if 'a,b,c'.replace(',', '-') != 'a-b-c' { print([1][3]); }\n"
      "if 'aaa'.replace('a', 'bb') != 'bbbbbb' { print([1][3]); }\n"
      "if 'hello'.replace('z', 'y') != 'hello' { print([1][3]); }\n"
      "if 'abc'.replace('', 'x') != 'abc' { print([1][3]); }\n"
      "if 'ab'.repeat(3) != 'ababab' { print([1][3]); }\n"
      "if 'ab'.repeat(0) != '' { print([1][3]); }\n"
      "if 'ab'.repeat(-1) != '' { print([1][3]); }\n"
      "if 'hello world'.substring(0, 5) != 'hello' { print([1][3]); }\n"
      "if 'hello'.substring(2, 100) != 'llo' { print([1][3]); }\n"
      "if 'hello'.substring(3, 1) != '' { print([1][3]); }\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

/* to_snake_case / to_camel_case normalize across separators, PascalCase,
 * camelCase, acronym runs, and SCREAMING_SNAKE. */
OAK_TEST_DECL(StringCaseStyleConversions)
{
  OAK_CHECK(expect_ok(
      "if 'HelloWorld'.to_snake_case() != 'hello_world' { print([1][3]); }\n"
      "if 'helloWorld'.to_snake_case() != 'hello_world' { print([1][3]); }\n"
      "if 'hello world'.to_snake_case() != 'hello_world' { print([1][3]); }\n"
      "if 'hello-world'.to_snake_case() != 'hello_world' { print([1][3]); }\n"
      "if 'HTTPServer'.to_snake_case() != 'http_server' { print([1][3]); }\n"
      "if 'HELLO_WORLD'.to_snake_case() != 'hello_world' { print([1][3]); }\n"
      "if '  a__b  '.to_snake_case() != 'a_b' { print([1][3]); }\n"
      "if ''.to_snake_case() != '' { print([1][3]); }\n"
      "if 'hello_world'.to_camel_case() != 'helloWorld' { print([1][3]); }\n"
      "if 'hello world'.to_camel_case() != 'helloWorld' { print([1][3]); }\n"
      "if 'hello-world'.to_camel_case() != 'helloWorld' { print([1][3]); }\n"
      "if 'HelloWorld'.to_camel_case() != 'helloWorld' { print([1][3]); }\n"
      "if 'helloWorld'.to_camel_case() != 'helloWorld' { print([1][3]); }\n"
      "if 'HTTPServer'.to_camel_case() != 'httpServer' { print([1][3]); }\n"
      "if 'HELLO_WORLD'.to_camel_case() != 'helloWorld' { print([1][3]); }\n"
      "if '__foo__bar__'.to_camel_case() != 'fooBar' { print([1][3]); }\n"
      "if ''.to_camel_case() != '' { print([1][3]); }\n"
      "if 'fooBar'.to_snake_case().to_camel_case() != 'fooBar' { print([1][3]); }\n")
            == OAK_TEST_OK);
  return OAK_TEST_OK;
}

/* Return types propagate: chained calls and use in typed positions compile. */
OAK_TEST_DECL(StringMethodChaining)
{
  OAK_CHECK(expect_ok(
      "let s = '  Hello World  '.trim().lower().replace(' ', '_');\n"
      "if s != 'hello_world' { print([1][3]); }\n"
      "fn shout(text : string) -> string { return text.upper().repeat(2); }\n"
      "if shout('hi') != 'HIHI' { print([1][3]); }\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

/* ord/chr/parse_number are global builtins. parse_number reads a token with a
 * '.', 'e', or 'E' as a float and any other numeric token as an integer. */
OAK_TEST_DECL(StringGlobalBuiltins)
{
  OAK_CHECK(expect_ok(
      "if ord('A') != 65 { print([1][3]); }\n"
      "if chr(97) != 'a' { print([1][3]); }\n"
      "if chr(ord('z')) != 'z' { print([1][3]); }\n"
      "if parse_number('42') != 42 { print([1][3]); }\n"
      "if !is_int(parse_number('42')) { print([1][3]); }\n"
      "if parse_number('  -17  ') != -17 { print([1][3]); }\n"
      "if parse_number('10') + parse_number('32') != 42 { print([1][3]); }\n"
      "if !is_float(parse_number('3.5')) { print([1][3]); }\n"
      "if to_int(parse_number('3.5') * 2.0) != 7 { print([1][3]); }\n"
      "if to_int(parse_number('1e3')) != 1000 { print([1][3]); }\n") ==
            OAK_TEST_OK);
  return OAK_TEST_OK;
}

/* Invalid conversions surface as runtime errors, not silent zeros. */
OAK_TEST_DECL(StringGlobalBuiltinsRejectBadInput)
{
  OAK_CHECK(expect_runtime_error("print(parse_number('nope'));\n") ==
            OAK_TEST_OK);
  OAK_CHECK(expect_runtime_error("print(parse_number('12x'));\n") ==
            OAK_TEST_OK);
  OAK_CHECK(expect_runtime_error("print(parse_number('1.2.3'));\n") ==
            OAK_TEST_OK);
  OAK_CHECK(expect_runtime_error("print(ord(''));\n") == OAK_TEST_OK);
  OAK_CHECK(expect_runtime_error("print(chr(-1));\n") == OAK_TEST_OK);
  OAK_CHECK(expect_runtime_error("print(chr(256));\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

/* Unknown methods and wrong arity are rejected at compile time. */
OAK_TEST_DECL(StringMethodErrorsAreCompileTime)
{
  OAK_CHECK(expect_compile_error("print('x'.nonexistent());\n") == OAK_TEST_OK);
  OAK_CHECK(expect_compile_error("print('x'.upper('extra'));\n") == OAK_TEST_OK);
  OAK_CHECK(expect_compile_error("print('x'.contains());\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(StringCaseAndTrim),
    OAK_TEST_ENTRY(StringSearchPredicates),
    OAK_TEST_ENTRY(StringTransforms),
    OAK_TEST_ENTRY(StringCaseStyleConversions),
    OAK_TEST_ENTRY(StringMethodChaining),
    OAK_TEST_ENTRY(StringGlobalBuiltins),
    OAK_TEST_ENTRY(StringGlobalBuiltinsRejectBadInput),
    OAK_TEST_ENTRY(StringMethodErrorsAreCompileTime),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
