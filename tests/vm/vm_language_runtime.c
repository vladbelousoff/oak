#include "oak_count_of.h"
#include "oak_test_pipeline.h"

OAK_TEST_DECL(RuntimeScalarFunctionsAndStrings)
{
  OAK_CHECK(expect_ok("fn add(a : number, b : number) -> number { return a + b; }\n"
                      "let s = 'hello' + ' ' + 'world';\n"
                      "print(s);\n"
                      "print(1 + 2.5);\n"
                      "print(7.0 / 2);\n"
                      "print(add(1, 2));\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(RuntimeNumberConversionsAndIntDiv)
{
  OAK_CHECK(expect_ok("let div = 7 / 2;\n"
                      "if !is_float(div) { print([1][3]); }\n"
                      "if to_int(div) != 3 { print([1][3]); }\n"
                      "if 7 // 2 != 3 { print([1][3]); }\n"
                      "if !is_int(7.9 // 2.0) { print([1][3]); }\n"
                      "if !is_float(to_float(7)) { print([1][3]); }\n"
                      "if to_int(sqrt(9)) != 3 { print([1][3]); }\n"
                      "if to_int(sin(0)) != 0 { print([1][3]); }\n"
                      "if to_int(cos(0)) != 1 { print([1][3]); }\n"
                      "if to_int(tan(0)) != 0 { print([1][3]); }\n"
                      "if is_int('x') { print([1][3]); }\n"
                      "if is_float('x') { print([1][3]); }\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(RuntimeArrays)
{
  OAK_CHECK(expect_ok("fn first(arr : number[]) -> number { return arr[0]; }\n"
                      "fn sub(a : number, b : number) -> number { return a - b; }\n"
                      "fn append(mut arr : number[]) -> number {\n"
                      "  arr.push(99);\n"
                      "  return arr.size();\n"
                      "}\n"
                      "let mut nums = [1, 2, 45 - sub(5, 3)];\n"
                      "nums.push(4);\n"
                      "nums[0] = 10;\n"
                      "let mut total = 0;\n"
                      "for i, value in nums { total += i + value; }\n"
                      "print(first(nums));\n"
                      "print(append(nums));\n"
                      "print(total);\n") == OAK_TEST_OK);

  OAK_CHECK(expect_compile_error("let bad = [1, 'two'];\n") == OAK_TEST_OK);
  OAK_CHECK(expect_compile_error("let mut arr = [];\n") == OAK_TEST_OK);
  OAK_CHECK(expect_compile_error("let mut arr = [] as number[];\n"
                                 "arr.push('oops');\n") == OAK_TEST_OK);
  OAK_CHECK(expect_compile_error("let mut arr = [1, 2, 3];\n"
                                 "arr[0] = 'oops';\n") == OAK_TEST_OK);
  OAK_CHECK(expect_compile_error("fn first(arr : number[]) -> number { return arr[0]; }\n"
                                 "first(42);\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(RuntimeMaps)
{
  OAK_CHECK(expect_ok("fn get_x(m : [string:number]) -> number { return m['x']; }\n"
                      "fn dbl(x : number) -> number { return x * 2; }\n"
                      "fn insert(mut m : [string:number]) -> number {\n"
                      "  m['z'] = 100;\n"
                      "  return m['z'];\n"
                      "}\n"
                      "let mut scores = ['x': 42, 'y': 7 + dbl(5)];\n"
                      "scores['x'] = scores['x'] + 1;\n"
                      "print(scores.size());\n"
                      "print(get_x(scores));\n"
                      "print(insert(scores));\n"
                      "print(scores.has('z'));\n"
                      "print(scores.delete('y'));\n"
                      "let mut total = 0;\n"
                      "for key, value in scores { total += value; }\n"
                      "print(total);\n") == OAK_TEST_OK);

  OAK_CHECK(expect_compile_error("let bad = ['a': 1, 'b': 'two'];\n") == OAK_TEST_OK);
  OAK_CHECK(expect_compile_error("let bad = ['a': 1, 2: 3];\n") == OAK_TEST_OK);
  OAK_CHECK(expect_compile_error("let mut m = [:];\n") == OAK_TEST_OK);
  OAK_CHECK(expect_compile_error("let mut m = [:] as [string:number];\n"
                                 "m[1] = 2;\n") == OAK_TEST_OK);
  OAK_CHECK(expect_compile_error("let mut m = [:] as [string:number];\n"
                                 "m['c'] = 'oops';\n") == OAK_TEST_OK);
  OAK_CHECK(expect_compile_error("let mut m = [:] as [string:number];\n"
                                 "m.has(1);\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(RuntimeRecords)
{
  OAK_CHECK(expect_ok("record Vec2 { x : number; y : number; }\n"
                      "fn manhattan(v : Vec2) -> number { return v.x + v.y; }\n"
                      "let x = 3;\n"
                      "let y = 4;\n"
                      "let mut p = new Vec2 { x, y };\n"
                      "p.x = p.x + 1;\n"
                      "print(p.x);\n"
                      "print(manhattan(p));\n") == OAK_TEST_OK);

  OAK_CHECK(expect_compile_error("record A { x : number; }\n"
                                 "record B { y : number; }\n"
                                 "fn take_a(v : A) -> number { return v.x; }\n"
                                 "let b = new B { y : 1 };\n"
                                 "take_a(b);\n") == OAK_TEST_OK);
  OAK_CHECK(expect_compile_error("record Foo { x : number; }\n"
                                 "let f = new Foo { x };\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(RuntimeIterationBreakContinue)
{
  OAK_CHECK(expect_ok("let mut nums = [1, 2, 3, 4];\n"
                      "let mut total = 0;\n"
                      "for value in nums {\n"
                      "  if value == 2 { continue; }\n"
                      "  if value == 4 { break; }\n"
                      "  total += value;\n"
                      "}\n"
                      "print(total);\n") == OAK_TEST_OK);
  OAK_CHECK(expect_compile_error("let n = 5;\n"
                                 "for value in n { print(value); }\n") == OAK_TEST_OK);
  return OAK_TEST_OK;
}

OAK_TEST_DECL(RuntimeTraits)
{
  /* Two concrete types dispatched through a trait parameter. */
  OAK_CHECK(expect_ok(
      "trait Shape {\n"
      "  fn area(self) -> number;\n"
      "  fn label(self) -> string;\n"
      "}\n"
      "record Circle { radius : number; }\n"
      "fn Circle.area(self) -> number { return self.radius * self.radius; }\n"
      "fn Circle.label(self) -> string { return 'circle'; }\n"
      "record Rect { w : number; h : number; }\n"
      "fn Rect.area(self) -> number { return self.w * self.h; }\n"
      "fn Rect.label(self) -> string { return 'rect'; }\n"
      "fn printShape(s: Shape) { print(s.label()); print(s.area()); }\n"
      "printShape(new Circle { radius: 3 });\n"
      "printShape(new Rect { w: 4, h: 5 });\n") == OAK_TEST_OK);

  /* Heterogeneous trait array with for-in and index access. */
  OAK_CHECK(expect_ok(
      "trait Shape { fn area(self) -> number; }\n"
      "record Circle { radius : number; }\n"
      "fn Circle.area(self) -> number { return self.radius * self.radius; }\n"
      "record Rect { w : number; h : number; }\n"
      "fn Rect.area(self) -> number { return self.w * self.h; }\n"
      "let mut shapes = [] as Shape[];\n"
      "shapes.push(new Circle { radius: 2 });\n"
      "shapes.push(new Rect { w: 3, h: 4 });\n"
      "let mut total = 0;\n"
      "for s in shapes { total += s.area(); }\n"
      "print(total);\n"
      "print(shapes[0].area());\n") == OAK_TEST_OK);

  /* Passing a trait object on to another trait-typed parameter. */
  OAK_CHECK(expect_ok(
      "trait Shape { fn area(self) -> number; }\n"
      "record Circle { radius : number; }\n"
      "fn Circle.area(self) -> number { return self.radius * self.radius; }\n"
      "fn inner(s: Shape) -> number { return s.area(); }\n"
      "fn outer(s: Shape) -> number { return inner(s); }\n"
      "let c = new Circle { radius: 5 };\n"
      "print(outer(c));\n") == OAK_TEST_OK);

  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(RuntimeScalarFunctionsAndStrings),
    OAK_TEST_ENTRY(RuntimeNumberConversionsAndIntDiv),
    OAK_TEST_ENTRY(RuntimeArrays),
    OAK_TEST_ENTRY(RuntimeMaps),
    OAK_TEST_ENTRY(RuntimeRecords),
    OAK_TEST_ENTRY(RuntimeIterationBreakContinue),
    OAK_TEST_ENTRY(RuntimeTraits),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
