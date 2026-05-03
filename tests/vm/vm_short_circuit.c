#include "oak_count_of.h"
#include "oak_test_pipeline.h"

/* `&&` must skip evaluation of the rhs when the lhs is false; `||` must skip
 * when the lhs is true. We surface the difference by making the rhs a
 * runtime-erroring expression: if it were evaluated, the program would trip
 * a runtime error; if it isn't, the program completes cleanly. */

/* false && (would-fail) → must not evaluate rhs → runs OK. */
OAK_TEST_DECL(AndShortCircuitsOnFalse)
{
  return expect_ok("let arr = [1, 2, 3];\n"
                   "if false && arr[100] == 0 { print(1); } else { print(0); }\n");
}

/* true || (would-fail) → must not evaluate rhs → runs OK. */
OAK_TEST_DECL(OrShortCircuitsOnTrue)
{
  return expect_ok("let arr = [1, 2, 3];\n"
                   "if true || arr[100] == 0 { print(1); } else { print(0); }\n");
}

/* true && (would-fail) → DOES evaluate rhs → expect runtime error. */
OAK_TEST_DECL(AndDoesNotShortCircuitOnTrue)
{
  return expect_runtime_error(
      "let arr = [1, 2, 3];\n"
      "if true && arr[100] == 0 { print(1); } else { print(0); }\n");
}

/* false || (would-fail) → DOES evaluate rhs → expect runtime error. */
OAK_TEST_DECL(OrDoesNotShortCircuitOnFalse)
{
  return expect_runtime_error(
      "let arr = [1, 2, 3];\n"
      "if false || arr[100] == 0 { print(1); } else { print(0); }\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(AndShortCircuitsOnFalse),
    OAK_TEST_ENTRY(OrShortCircuitsOnTrue),
    OAK_TEST_ENTRY(AndDoesNotShortCircuitOnTrue),
    OAK_TEST_ENTRY(OrDoesNotShortCircuitOnFalse),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
