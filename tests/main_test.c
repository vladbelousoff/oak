/*
 * Entry point for the whole Oak test binary.
 *
 * utest.h registers every UTEST_F in every linked translation unit through a
 * constructor, so there is nothing to list here -- adding a suite file to
 * meson.build is all it takes. Run a single suite with
 * `oak_tests --filter='vm_exec.*'`; meson registers one such invocation per
 * suite so failures and crashes stay attributed to their suite.
 *
 * main() is spelled out rather than using UTEST_MAIN() for one reason: utest
 * treats "the filter matched no tests" as a successful run of zero tests. With
 * meson driving eighteen filters from a list of names, a renamed suite or a
 * typo would silently stop running an entire file's worth of tests while the
 * build stayed green. Refusing to run in that case turns that into a failure.
 */

#include "utest.h"

#include "oak_types.h"

#include <string.h>

UTEST_STATE();

int main(int argc, const char* const argv[])
{
  const char* filter = OAK_NULL;
  int i;

  for (i = 1; i < argc; ++i)
  {
    if (strncmp(argv[i], "--filter=", 9) == 0)
      filter = argv[i] + 9;
  }

  if (filter)
  {
    size_t matched = 0;
    size_t index;
    for (index = 0; index < utest_state.tests_length; ++index)
    {
      if (!utest_should_filter_test(filter, utest_state.tests[index].name))
        matched++;
    }
    if (matched == 0)
    {
      fprintf(stderr,
              "error: --filter=%s matched none of the %u registered tests; "
              "the suite was probably renamed or removed\n",
              filter,
              (unsigned)utest_state.tests_length);
      return 1;
    }
  }

  return utest_main(argc, argv);
}
