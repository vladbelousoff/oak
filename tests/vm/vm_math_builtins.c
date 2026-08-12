#include "oak_count_of.h"
#include "oak_test_pipeline.h"

/* Math functions are global built-ins (registered by the compiler), so they are
 * callable with no import. This locks in the contract after the old importable
 * `math` module was removed. The `if BAD { print([1][3]); }` idiom forces a
 * runtime out-of-bounds error when a result is wrong, so expect_ok passes only
 * when every check holds. */
OAK_TEST_DECL(MathBuiltinsAreGlobalNoImport)
{
  OAK_CHECK(expect_ok(
      "if to_int(sqrt(16.0)) != 4 { print([1][3]); }\n"
      "if to_int(sqrt(9.0)) != 3 { print([1][3]); }\n"
      "if to_int(abs(-5.0)) != 5 { print([1][3]); }\n"
      "if to_int(abs(5.0)) != 5 { print([1][3]); }\n"
      "if to_int(min(3.0, 7.0)) != 3 { print([1][3]); }\n"
      "if to_int(max(3.0, 7.0)) != 7 { print([1][3]); }\n"
      "if to_int(fmod(7.0, 3.0)) != 1 { print([1][3]); }\n"
      "if to_int(sin(0.0)) != 0 { print([1][3]); }\n"
      "if to_int(cos(0.0)) != 1 { print([1][3]); }\n"
      "if to_int(tan(0.0)) != 0 { print([1][3]); }\n"
      "if random() < 0.0 { print([1][3]); }\n"
      "if random() > 1.0 { print([1][3]); }\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

/* The math built-ins are usable everywhere a number expression is: inside
 * functions, arithmetic, and as call arguments. */
OAK_TEST_DECL(MathBuiltinsComposeWithUserCode)
{
  OAK_CHECK(expect_ok(
      "fn hypot(a : number, b : number) -> number {\n"
      "  return sqrt(a * a + b * b);\n"
      "}\n"
      "if to_int(hypot(3.0, 4.0)) != 5 { print([1][3]); }\n"
      "if to_int(max(min(10.0, 2.0), 1.0)) != 2 { print([1][3]); }\n"
      "print(hypot(6.0, 8.0));\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

/* Rounding, power, and logarithm built-ins. floor/ceil/round return integers;
 * pow/log/exp/atan2 return floats. */
OAK_TEST_DECL(MathBuiltinsRoundingAndPowers)
{
  OAK_CHECK(expect_ok(
      "if floor(3.7) != 3 { print([1][3]); }\n"
      "if floor(-2.5) != -3 { print([1][3]); }\n"
      "if ceil(3.2) != 4 { print([1][3]); }\n"
      "if ceil(-2.5) != -2 { print([1][3]); }\n"
      "if round(2.5) != 3 { print([1][3]); }\n"
      "if round(2.4) != 2 { print([1][3]); }\n"
      "if floor(5) != 5 { print([1][3]); }\n"
      "if to_int(pow(2.0, 10.0)) != 1024 { print([1][3]); }\n"
      "if to_int(exp(0.0)) != 1 { print([1][3]); }\n"
      "if to_int(log(1.0)) != 0 { print([1][3]); }\n"
      "if sign(-8.0) != -1 { print([1][3]); }\n"
      "if sign(8.0) != 1 { print([1][3]); }\n"
      "if sign(0.0) != 0 { print([1][3]); }\n"
      "if to_int(atan2(0.0, 1.0)) != 0 { print([1][3]); }\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

/* log of a non-positive number is undefined and reported as a runtime error
 * rather than yielding NaN/-inf. */
OAK_TEST_DECL(MathLogRejectsNonPositive)
{
  OAK_CHECK(expect_runtime_error("print(log(0.0));\n") == OAK_TEST_OK);
  OAK_CHECK(expect_runtime_error("print(log(-1.0));\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static oak_test_t tests[] = {
    OAK_TEST_ENTRY(MathBuiltinsAreGlobalNoImport),
    OAK_TEST_ENTRY(MathBuiltinsComposeWithUserCode),
    OAK_TEST_ENTRY(MathBuiltinsRoundingAndPowers),
    OAK_TEST_ENTRY(MathLogRejectsNonPositive),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
