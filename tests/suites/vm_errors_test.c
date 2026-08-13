/*
 * VM: runtime errors.
 *
 * Every case asserts the error message, not just that the run failed. The
 * support layer captures stderr around the run to get it, because the VM
 * formats runtime errors straight into oak_log and keeps no copy (see
 * src/vm/oak_vm_error.c). Without that, "the program failed" would be
 * satisfied by any failure at all -- including the wrong one.
 */

#include "oak_test_support.h"

OAK_TEST_SUITE(vm_errors);

/* ------------------------------------------------------------------ */
/* Bounds and lookups                                                  */
/* ------------------------------------------------------------------ */

UTEST_F(vm_errors, out_of_range_indexing_is_caught)
{
  static const oak_case_t cases[] = {
    { "let arr = [1, 2, 3];\n"
      "print(arr[100]);\n",
      "array index 100 out of bounds" },
    { "let arr = [1, 2, 3];\n"
      "print(arr[-1]);\n",
      "array index -1 out of bounds" },
    { "let mut m = new [string:number];\n"
      "m['a'] = 1;\n"
      "print(m['nope']);\n",
      "key not found in map" },
  };

  OAK_EXPECT_RUNTIME_ERROR_CASES(cases);
}

/* ------------------------------------------------------------------ */
/* Arithmetic                                                          */
/* ------------------------------------------------------------------ */

UTEST_F(vm_errors, division_and_modulo_by_zero_are_caught)
{
  static const oak_case_t cases[] = {
    { "let x = 1 / 0;\n"
      "print(x);\n",
      "division by zero" },
    { "let x = 1 // 0;\n"
      "print(x);\n",
      "integer division by zero" },
    { "let x = 7 % 0;\n"
      "print(x);\n",
      "integer remainder by zero" },
  };

  OAK_EXPECT_RUNTIME_ERROR_CASES(cases);
}

/*
 * These are the cases where the natural C implementation would trap with
 * SIGFPE or invoke undefined behaviour. They must produce a reported error
 * instead, which is the whole point of testing them.
 */
UTEST_F(vm_errors, integer_division_edge_cases_are_reported_not_trapped)
{
  static const oak_case_t cases[] = {
    /* INT_MIN // -1 overflows i32. */
    { "let x = (0 - 2147483647 - 1) // (0 - 1);\n"
      "print(x);\n",
      "integer division overflow" },
    /* A float operand outside the i32 range is an error, not a wrap. */
    { "let x = 1.0e30 // 1;\n"
      "print(x);\n",
      "does not fit in an integer" },
  };

  OAK_EXPECT_RUNTIME_ERROR_CASES(cases);
}

/* INT_MIN % -1 is mathematically zero and must simply compute, not trap. */
UTEST_F(vm_errors, int_min_modulo_minus_one_is_zero)
{
  static const oak_case_t cases[] = {
    { "let a = 0 - 2147483647 - 1;\n"
      "let b = 0 - 1;\n"
      "print(a % b);\n",
      null },
  };

  OAK_EXPECT_OK_CASES(cases);
}

UTEST_F(vm_errors, arithmetic_on_non_numbers_is_caught)
{
  static const oak_case_t cases[] = {
    { "print(to_int('x'));\n", "native function 'to_int' failed" },
    { "print(sqrt(-1));\n", "native function 'sqrt' failed" },
    { "print(sin('x'));\n", "native function 'sin' failed" },
  };

  OAK_EXPECT_RUNTIME_ERROR_CASES(cases);
}

/* ------------------------------------------------------------------ */
/* Stack limits                                                        */
/* ------------------------------------------------------------------ */

/* Unbounded recursion must hit the frame limit and report it rather than
 * running the native stack into the ground. */
UTEST_F(vm_errors, runaway_recursion_hits_the_frame_limit)
{
  static const oak_case_t cases[] = {
    { "fn recurse() -> number { return recurse() + 1; }\n"
      "print(recurse());\n",
      "call stack overflow" },
  };

  OAK_EXPECT_RUNTIME_ERROR_CASES(cases);
}

/* ------------------------------------------------------------------ */
/* Short-circuit evaluation                                            */
/* ------------------------------------------------------------------ */

/*
 * Short-circuiting is invisible from the outside unless evaluating the
 * right-hand side has an observable effect, so these pair a skipping case with
 * a non-skipping one over the same expression: the rhs is an out-of-bounds
 * index, which errors if and only if it is actually evaluated.
 */
UTEST_F(vm_errors, and_or_skip_their_right_hand_side)
{
  static const oak_case_t skipped[] = {
    { "let arr = [1, 2, 3];\n"
      "if false && arr[100] == 0 { print(1); } else { print(0); }\n",
      null },
    { "let arr = [1, 2, 3];\n"
      "if true || arr[100] == 0 { print(1); } else { print(0); }\n",
      null },
  };

  static const oak_case_t evaluated[] = {
    { "let arr = [1, 2, 3];\n"
      "if true && arr[100] == 0 { print(1); } else { print(0); }\n",
      "array index 100 out of bounds" },
    { "let arr = [1, 2, 3];\n"
      "if false || arr[100] == 0 { print(1); } else { print(0); }\n",
      "array index 100 out of bounds" },
  };

  OAK_EXPECT_OK_CASES(skipped);
  OAK_EXPECT_RUNTIME_ERROR_CASES(evaluated);
}
