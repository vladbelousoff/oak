#include "oak_count_of.h"
#include "oak_test_pipeline.h"

/* Comma-separated variants (canonical style). */
OAK_TEST_DECL(QualifiedVariantAccessOk)
{
  return expect_ok("enum Color { Red, Green, Blue }\n"
                   "let c = Color.Green;\n");
}

/* Space-separated variants (no commas) must be rejected. */
OAK_TEST_DECL(SpaceSeparatedVariantsRejected)
{
  return expect_parse_or_compile_error(
      "enum Dir { North South East West }\n"
      "let a = Dir.North;\n");
}

/* Trailing comma is accepted. */
OAK_TEST_DECL(TrailingCommaOk)
{
  return expect_ok("enum Status { Off, On, }\n"
                   "let s = Status.On;\n");
}

/* Enum variant used in an expression. */
OAK_TEST_DECL(VariantInExpressionOk)
{
  return expect_ok("enum Status { Off, On }\n"
                   "let s = Status.On;\n"
                   "let x = s + 10;\n");
}

/* Enum variant used as a function argument. */
OAK_TEST_DECL(VariantAsFnArgOk)
{
  return expect_ok("enum Color { Red, Green, Blue }\n"
                   "fn use_color(c : number) -> number { return c; }\n"
                   "use_color(Color.Blue);\n");
}

/* Multiple enums with independent ordinals. */
OAK_TEST_DECL(MultipleEnumsOk)
{
  return expect_ok("enum A { X, Y }\n"
                   "enum B { P, Q }\n"
                   "let x = A.X;\n"
                   "let q = B.Q;\n");
}

/* Bare variant identifier must be rejected. */
OAK_TEST_DECL(BareVariantRejected)
{
  return expect_compile_error("enum Color { Red, Green, Blue }\n"
                              "let c = Green;\n");
}

/* Unknown variant on a valid enum is rejected. */
OAK_TEST_DECL(UnknownVariantRejected)
{
  return expect_compile_error("enum Color { Red, Green, Blue }\n"
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
