#include "oak_test_pipeline.h"

OAK_TEST_DECL(ArrayBasic)
{
  /* Push elements, read them back via indexing, mutate via index assignment,
   * and call .size() / .push() as array methods. */
  const char* source = "let mut arr = [] as number[];\n"
                       "arr.push(10);\n"
                       "arr.push(20);\n"
                       "arr.push(30);\n"
                       "print(arr.size());\n"
                       "print(arr[0]);\n"
                       "print(arr[1]);\n"
                       "print(arr[2]);\n"
                       "arr[1] = 99;\n"
                       "print(arr[1]);\n";
  return expect_ok(source);
}

OAK_TEST_MAIN(ArrayBasic)
