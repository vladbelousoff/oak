#include "oak_test_pipeline.h"

OAK_TEST_DECL(MapBasic)
{
  /* Construct a typed map, insert and update entries, look them up by key,
   * and call .size() as a method on the map receiver. */
  const char* source = "let mut a = [:] as [string:number];\n"
                       "a['one'] = 1;\n"
                       "a['two'] = 2;\n"
                       "a['three'] = 3;\n"
                       "print(a.size());\n"
                       "print(a['one']);\n"
                       "print(a['two']);\n"
                       "print(a['three']);\n"
                       "a['two'] = 22;\n"
                       "print(a['two']);\n";
  return expect_ok(source);
}

OAK_TEST_MAIN(MapBasic)
