#include "oak_count_of.h"
#include "oak_test_pipeline.h"

/* =========================================================================
 * Generic functions — basic
 * ========================================================================= */

OAK_TEST_DECL(GenericIdentityNumberOk)
{
  return expect_ok("fn identity<T>(x: T) -> T { return x; }\n"
                   "print(identity(42));\n");
}

OAK_TEST_DECL(GenericIdentityStringOk)
{
  return expect_ok("fn identity<T>(x: T) -> T { return x; }\n"
                   "print(identity('hello'));\n");
}

OAK_TEST_DECL(GenericIdentityBoolOk)
{
  return expect_ok("fn identity<T>(x: T) -> T { return x; }\n"
                   "print(identity(true));\n");
}

OAK_TEST_DECL(GenericMultiParamOk)
{
  return expect_ok("fn pick_first<A, B>(a: A, b: B) -> A { return a; }\n"
                   "fn pick_second<A, B>(a: A, b: B) -> B { return b; }\n"
                   "print(pick_first(42, 'hello'));\n"
                   "print(pick_second(42, 'hello'));\n");
}

/* =========================================================================
 * Generic functions — type param consistency
 * ========================================================================= */

OAK_TEST_DECL(GenericSameParamConsistentOk)
{
  return expect_ok("fn same<T>(a: T, b: T) -> T { return a; }\n"
                   "print(same(1, 2));\n"
                   "print(same('a', 'b'));\n");
}

OAK_TEST_DECL(GenericSameParamConflictFails)
{
  return expect_compile_error(
      "fn same<T>(a: T, b: T) -> T { return a; }\n"
      "same(1, 'hello');\n");
}

OAK_TEST_DECL(GenericThreeArgConflictFails)
{
  return expect_compile_error(
      "fn triple<T>(a: T, b: T, c: T) -> T { return a; }\n"
      "triple(1, 2, 'oops');\n");
}

/* =========================================================================
 * Generic functions — array parameters
 * ========================================================================= */

OAK_TEST_DECL(GenericArrayParamOk)
{
  return expect_ok("fn first<T>(arr: T[]) -> T { return arr[0]; }\n"
                   "let nums = [10, 20, 30];\n"
                   "print(first(nums));\n");
}

/* =========================================================================
 * Generic functions — void return
 * ========================================================================= */

OAK_TEST_DECL(GenericVoidReturnOk)
{
  return expect_ok("fn consume<T>(x: T) { let y = x; }\n"
                   "consume(42);\n"
                   "consume('hello');\n");
}

/* =========================================================================
 * Generic functions — invalid function head (Fix 1)
 * ========================================================================= */

OAK_TEST_DECL(InvalidFnHeadArrayFails)
{
  return expect_parse_or_compile_error("fn Foo[](x: number) { return x; }\n");
}

/* =========================================================================
 * Generic records — basic
 * ========================================================================= */

OAK_TEST_DECL(GenericRecordBasicOk)
{
  return expect_ok("record Box<T> { value: T; }\n"
                   "let b = new Box { value: 42 };\n"
                   "print(b.value);\n");
}

OAK_TEST_DECL(GenericRecordStringFieldOk)
{
  return expect_ok("record Box<T> { value: T; }\n"
                   "let b = new Box { value: 'hello' };\n"
                   "print(b.value);\n");
}

OAK_TEST_DECL(GenericRecordMultiFieldOk)
{
  return expect_ok("record Pair<A, B> { first: A; second: B; }\n"
                   "let p = new Pair { first: 42, second: 'hello' };\n"
                   "print(p.first);\n"
                   "print(p.second);\n");
}

/* =========================================================================
 * Generic records — with methods
 * ========================================================================= */

OAK_TEST_DECL(GenericRecordWithMethodOk)
{
  return expect_ok("record Box<T> { value: T; }\n"
                   "fn Box.get(self) -> T { return self.value; }\n"
                   "let b = new Box { value: 42 };\n"
                   "print(b.get());\n");
}

/* =========================================================================
 * Generic functions — with generic record params
 * ========================================================================= */

OAK_TEST_DECL(GenericFnWithGenericRecordOk)
{
  return expect_ok("record Box<T> { value: T; }\n"
                   "fn unwrap<T>(b: Box<T>) -> T { return b.value; }\n"
                   "let b = new Box { value: 99 };\n"
                   "print(unwrap(b));\n");
}

/* =========================================================================
 * Generic functions — nested calls
 * ========================================================================= */

OAK_TEST_DECL(GenericNestedCallsOk)
{
  return expect_ok("fn id<T>(x: T) -> T { return x; }\n"
                   "print(id(id(id(42))));\n");
}

/* =========================================================================
 * Generic records — array-typed field
 * ========================================================================= */

OAK_TEST_DECL(GenericRecordArrayFieldOk)
{
  return expect_ok("record Container<T> { items: T[]; }\n"
                   "let c = new Container { items: [1, 2, 3] };\n"
                   "print(c.items[0]);\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(GenericIdentityNumberOk),
    OAK_TEST_ENTRY(GenericIdentityStringOk),
    OAK_TEST_ENTRY(GenericIdentityBoolOk),
    OAK_TEST_ENTRY(GenericMultiParamOk),
    OAK_TEST_ENTRY(GenericSameParamConsistentOk),
    OAK_TEST_ENTRY(GenericSameParamConflictFails),
    OAK_TEST_ENTRY(GenericThreeArgConflictFails),
    OAK_TEST_ENTRY(GenericArrayParamOk),
    OAK_TEST_ENTRY(GenericVoidReturnOk),
    OAK_TEST_ENTRY(InvalidFnHeadArrayFails),
    OAK_TEST_ENTRY(GenericRecordBasicOk),
    OAK_TEST_ENTRY(GenericRecordStringFieldOk),
    OAK_TEST_ENTRY(GenericRecordMultiFieldOk),
    OAK_TEST_ENTRY(GenericRecordWithMethodOk),
    OAK_TEST_ENTRY(GenericFnWithGenericRecordOk),
    OAK_TEST_ENTRY(GenericNestedCallsOk),
    OAK_TEST_ENTRY(GenericRecordArrayFieldOk),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
