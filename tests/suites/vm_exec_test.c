/*
 * VM: end-to-end execution.
 *
 * Merges what were five files (language runtime, string methods, math
 * builtins, short-circuit arithmetic, anonymous functions). Wherever a test
 * cares about a computed value it prints it and asserts the printed text,
 * which says what the value actually was when it is wrong.
 */

#include "oak_test_support.h"

OAK_TEST_SUITE(vm_exec);

UTEST_F(vm_exec, arithmetic_and_string_concatenation)
{
  static const oak_case_t cases[] = {
    { "print('hello' + ' ' + 'world');\n", "hello world" },
    { "print(1 + 2);\n", "3" },
    { "fn add(a : number, b : number) -> number { return a + b; }\n"
      "print(add(1, 2));\n",
      "3" },
    { "print(10 - 4);\n", "6" },
    { "print(6 * 7);\n", "42" },
    { "print(7 // 2);\n", "3" },
    { "print(7 % 3);\n", "1" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/*
 * `/` is true division and yields a float; `//` truncates to an integer. The
 * distinction is easy to regress, so both the value and the int/float tag are
 * checked.
 */
UTEST_F(vm_exec, division_distinguishes_float_and_integer)
{
  static const oak_case_t cases[] = {
    { "print(7 / 2);\n", "3.5" },
    { "print(is_float(7 / 2));\n", "true" },
    { "print(7 // 2);\n", "3" },
    { "print(is_int(7 // 2));\n", "true" },
    { "print(is_int(7.9 // 2.0));\n", "true" },
    { "print(to_int(7 / 2));\n", "3" },
    { "print(is_float(to_float(7)));\n", "true" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

UTEST_F(vm_exec, number_predicates_reject_non_numbers)
{
  static const oak_case_t cases[] = {
    { "print(is_int('x'));\n", "false" },
    { "print(is_float('x'));\n", "false" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* Math functions are global builtins needing no import; the importable `math`
 * module was removed and must not come back by accident. */
UTEST_F(vm_exec, math_builtins_need_no_import)
{
  static const oak_case_t cases[] = {
    { "print(to_int(sqrt(9)));\n", "3" },
    { "print(abs(0 - 5));\n", "5" },
    { "print(min(3, 8));\n", "3" },
    { "print(max(3, 8));\n", "8" },
    { "print(to_int(fmod(7.0, 3.0)));\n", "1" },
    { "print(to_int(sin(0)));\n", "0" },
    { "print(to_int(cos(0)));\n", "1" },
    { "print(to_int(tan(0)));\n", "0" },
    { "print(to_int(pow(2, 10)));\n", "1024" },
    { "print(to_int(floor(3.7)));\n", "3" },
    { "print(to_int(ceil(3.2)));\n", "4" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

UTEST_F(vm_exec, arrays_index_push_and_iterate)
{
  static const oak_case_t cases[] = {
    { "let mut nums = [1, 2, 3];\n"
      "nums.push(4);\n"
      "nums[0] = 10;\n"
      "print(nums[0]);\n"
      "print(nums.size());\n",
      "10\n4" },
    { "let mut nums = [1, 2, 3, 4];\n"
      "let mut total = 0;\n"
      "for value in nums {\n"
      "  if value == 2 { continue; }\n"
      "  if value == 4 { break; }\n"
      "  total += value;\n"
      "}\n"
      "print(total);\n",
      "4" },
    /* The indexed form of for-in yields position and value. */
    { "let nums = [10, 20, 30];\n"
      "let mut total = 0;\n"
      "for i, value in nums { total += i + value; }\n"
      "print(total);\n",
      "63" },
    /* An array reaches a `mut` parameter as a reference, so the push is seen
     * by the caller. */
    { "fn append(mut arr : number[]) -> number {\n"
      "  arr.push(99);\n"
      "  return arr.size();\n"
      "}\n"
      "let mut nums = [1, 2];\n"
      "print(append(nums));\n"
      "print(nums.size());\n",
      "3\n3" },
    { "let mut nums = [1, 5, 3];\n"
      "nums[0] += 4;\n"
      "nums[1] -= 1;\n"
      "nums[2] *= 3;\n"
      "print(nums[0]);\n"
      "print(nums[1]);\n"
      "print(nums[2]);\n",
      "5\n4\n9" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

UTEST_F(vm_exec, array_typing_is_enforced_at_compile_time)
{
  static const oak_case_t cases[] = {
    { "let bad = [1, 'two'];\n", "element type mismatch" },
    { "let mut arr = new number[];\n"
      "arr.push('oops');\n",
      "cannot push value of type 'string'" },
    { "let mut arr = [1, 2, 3];\n"
      "arr[0] = 'oops';\n",
      "cannot assign value of type 'string'" },
    /* An immutable array rejects both mutating forms. */
    { "let arr = [1, 2, 3];\n"
      "arr.push(4);\n",
      "immutable" },
    { "let arr = [1, 2, 3];\n"
      "arr[0] = 4;\n",
      "immutable" },
    { "fn first(arr : number[]) -> number { return arr[0]; }\n"
      "first(42);\n",
      "expected type" },
    { "let n = 5;\n"
      "for value in n { print(value); }\n",
      "requires an array or map" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* An empty literal carries no element type to infer from. */
UTEST_F(vm_exec, untyped_empty_collection_literals_are_rejected)
{
  static const oak_case_t cases[] = {
    { "let mut arr = [];\n", OAK_NULL },
    { "let mut m = [:];\n", OAK_NULL },
  };

  OAK_EXPECT_REJECTED_CASES(cases);
}

UTEST_F(vm_exec, maps_store_lookup_and_delete)
{
  static const oak_case_t cases[] = {
    { "let mut scores = ['x': 42, 'y': 17];\n"
      "scores['x'] = scores['x'] + 1;\n"
      "print(scores.size());\n"
      "print(scores['x']);\n"
      "print(scores.has('y'));\n"
      "print(scores.delete('y'));\n"
      "print(scores.size());\n",
      "2\n43\ntrue\ntrue\n1" },
    { "fn insert(mut m : [string:number]) -> number {\n"
      "  m['z'] = 100;\n"
      "  return m['z'];\n"
      "}\n"
      "let mut scores = ['x': 1];\n"
      "print(insert(scores));\n"
      "print(scores.has('z'));\n",
      "100\ntrue" },
    { "let scores = ['a': 1, 'b': 2];\n"
      "let mut total = 0;\n"
      "for key, value in scores { total += value; }\n"
      "print(total);\n",
      "3" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

UTEST_F(vm_exec, map_typing_is_enforced_at_compile_time)
{
  static const oak_case_t cases[] = {
    { "let bad = ['a': 1, 'b': 'two'];\n", "value type mismatch" },
    { "let bad = ['a': 1, 2: 3];\n", "key type mismatch" },
    { "let mut m = new [string:number];\n"
      "m[1] = 2;\n",
      "map key must be of type 'string'" },
    { "let mut m = new [string:number];\n"
      "m['c'] = 'oops';\n",
      "cannot assign value of type 'string'" },
    { "let mut m = new [string:number];\n"
      "m.has(1);\n",
      "map key must be of type 'string'" },
    { "let m = ['x': 1];\n"
      "m['x'] = 2;\n",
      "immutable" },
    { "let m = ['x': 1];\n"
      "m.delete('x');\n",
      "immutable" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(vm_exec, records_construct_read_and_mutate)
{
  static const oak_case_t cases[] = {
    /* Shorthand field init: `{ x, y }` takes the values of locals x and y. */
    { "record Vec2 { x : number; y : number; }\n"
      "fn manhattan(v : Vec2) -> number { return v.x + v.y; }\n"
      "let x = 3;\n"
      "let y = 4;\n"
      "let mut p = new Vec2 { x, y };\n"
      "p.x = p.x + 1;\n"
      "print(p.x);\n"
      "print(manhattan(p));\n",
      "4\n8" },
    /* Every compound assignment operator against record fields. */
    { "record Counter { value : number; scale : number; whole : number; }\n"
      "fn update(mut counter : Counter, amount : number) {\n"
      "  counter.value += amount;\n"
      "  counter.value -= 2;\n"
      "  counter.value *= counter.scale;\n"
      "  counter.value /= 3;\n"
      "  counter.whole %= 5;\n"
      "}\n"
      "let mut c = new Counter { value : 10, scale : 3, whole : 17 };\n"
      "update(c, 4);\n"
      "print(c.value);\n"
      "print(c.whole);\n",
      "12\n2" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* Shorthand initialization still requires the local to exist. */
UTEST_F(vm_exec, shorthand_record_fields_need_a_binding_in_scope)
{
  static const oak_case_t cases[] = {
    { "record Foo { x : number; }\n"
      "let f = new Foo { x };\n",
      OAK_NULL },
  };

  OAK_EXPECT_REJECTED_CASES(cases);
}

UTEST_F(vm_exec, virtual_dispatch_selects_the_concrete_implementation)
{
  static const oak_case_t cases[] = {
    { "interface IShape {\n"
      "  fn area() -> number;\n"
      "  fn label() -> string;\n"
      "}\n"
      "record Circle implements IShape {\n"
      "  radius : number;\n"
      "  fn area() -> number { return self.radius * self.radius; }\n"
      "  fn label() -> string { return 'circle'; }\n"
      "}\n"
      "record Rect implements IShape {\n"
      "  w : number;\n"
      "  h : number;\n"
      "  fn area() -> number { return self.w * self.h; }\n"
      "  fn label() -> string { return 'rect'; }\n"
      "}\n"
      "fn show(s: IShape) { print(s.label()); print(s.area()); }\n"
      "show(new Circle { radius: 3 });\n"
      "show(new Rect { w: 4, h: 5 });\n",
      "circle\n9\nrect\n20" },
    /* A heterogeneous array dispatches per element. */
    { "interface IShape { fn area() -> number; }\n"
      "record Circle implements IShape {\n"
      "  radius : number;\n"
      "  fn area() -> number { return self.radius * self.radius; }\n"
      "}\n"
      "record Rect implements IShape {\n"
      "  w : number;\n"
      "  h : number;\n"
      "  fn area() -> number { return self.w * self.h; }\n"
      "}\n"
      "let mut shapes = new IShape[];\n"
      "shapes.push(new Circle { radius: 2 });\n"
      "shapes.push(new Rect { w: 3, h: 4 });\n"
      "let mut total = 0;\n"
      "for s in shapes { total += s.area(); }\n"
      "print(total);\n"
      "print(shapes[0].area());\n",
      "16\n4" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

UTEST_F(vm_exec, functions_are_values)
{
  static const oak_case_t cases[] = {
    { "let double = (x : number) -> number { return x * 2; };\n"
      "print(double(21));\n",
      "42" },
    { "fn named(x : number) -> number { return x + 1; }\n"
      "let alias = named;\n"
      "print(alias(41));\n",
      "42" },
    { "fn apply(f : (number) -> number, v : number) -> number {\n"
      "  return f(v);\n"
      "}\n"
      "print(apply((x : number) -> number { return x * 3; }, 14));\n",
      "42" },
    /* Immediately invoked. */
    { "print(((x : number) -> number { return x + 2; })(40));\n", "42" },
    /* A void anonymous function. */
    { "let shout = (s : string) { print(s); };\n"
      "shout('hi');\n",
      "hi" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* Oak has no closures: an anonymous function may not capture enclosing
 * locals, which is what keeps function values free of captured ownership and
 * therefore free of reference cycles. */
UTEST_F(vm_exec, anonymous_functions_cannot_close_over_locals)
{
  static const oak_case_t cases[] = {
    { "fn outer() -> number {\n"
      "  let captured = 5;\n"
      "  let f = () -> number { return captured; };\n"
      "  return f();\n"
      "}\n"
      "print(outer());\n",
      OAK_NULL },
  };

  OAK_EXPECT_REJECTED_CASES(cases);
}

/* String methods are builtins on the string type; no import is needed. */
UTEST_F(vm_exec, string_case_and_trimming)
{
  static const oak_case_t cases[] = {
    { "print('Hello'.upper());\n", "HELLO" },
    { "print('Hello'.lower());\n", "hello" },
    { "print('[' + '  hi  '.trim() + ']');\n", "[hi]" },
    { "print('[' + ''.upper() + ']');\n", "[]" },
    { "print('[' + '   '.trim() + ']');\n", "[]" },
    { "print('[' + '\\t x \\n'.trim() + ']');\n", "[x]" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

UTEST_F(vm_exec, string_search_predicates)
{
  static const oak_case_t cases[] = {
    { "print('hello world'.contains('world'));\n", "true" },
    { "print('hello world'.contains('xyz'));\n", "false" },
    { "print('hello'.starts_with('he'));\n", "true" },
    { "print('hello'.starts_with('lo'));\n", "false" },
    { "print('hello'.ends_with('lo'));\n", "true" },
    { "print('hello'.ends_with('he'));\n", "false" },
    { "print('hello'.index_of('l'));\n", "2" },
    { "print('hello'.index_of('z'));\n", "-1" },
    /* Every string contains the empty string. */
    { "print('anything'.contains(''));\n", "true" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

UTEST_F(vm_exec, string_transforms_and_slicing)
{
  static const oak_case_t cases[] = {
    { "print('a,b,c'.replace(',', '-'));\n", "a-b-c" },
    /* The replacement is not rescanned. */
    { "print('aaa'.replace('a', 'bb'));\n", "bbbbbb" },
    { "print('hello'.replace('z', 'y'));\n", "hello" },
    { "print('abc'.replace('', 'x'));\n", "abc" },
    { "print('ab'.repeat(3));\n", "ababab" },
    { "print('[' + 'ab'.repeat(0) + ']');\n", "[]" },
    /* A negative count is clamped rather than being an error. */
    { "print('[' + 'ab'.repeat(-1) + ']');\n", "[]" },
    { "print('hello world'.substring(0, 5));\n", "hello" },
    /* Out-of-range ends clamp; an inverted range is empty. */
    { "print('hello'.substring(2, 100));\n", "llo" },
    { "print('[' + 'hello'.substring(3, 1) + ']');\n", "[]" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* Case-style conversion normalizes across separators, camelCase, PascalCase,
 * acronym runs, and SCREAMING_SNAKE. */
UTEST_F(vm_exec, string_case_style_conversions)
{
  static const oak_case_t cases[] = {
    { "print('HelloWorld'.to_snake_case());\n", "hello_world" },
    { "print('helloWorld'.to_snake_case());\n", "hello_world" },
    { "print('hello world'.to_snake_case());\n", "hello_world" },
    { "print('hello-world'.to_snake_case());\n", "hello_world" },
    { "print('HTTPServer'.to_snake_case());\n", "http_server" },
    { "print('HELLO_WORLD'.to_snake_case());\n", "hello_world" },
    { "print('  a__b  '.to_snake_case());\n", "a_b" },
    { "print('[' + ''.to_snake_case() + ']');\n", "[]" },
    { "print('hello_world'.to_camel_case());\n", "helloWorld" },
    { "print('hello world'.to_camel_case());\n", "helloWorld" },
    { "print('hello-world'.to_camel_case());\n", "helloWorld" },
    { "print('HelloWorld'.to_camel_case());\n", "helloWorld" },
    { "print('helloWorld'.to_camel_case());\n", "helloWorld" },
    { "print('HTTPServer'.to_camel_case());\n", "httpServer" },
    { "print('HELLO_WORLD'.to_camel_case());\n", "helloWorld" },
    { "print('__foo__bar__'.to_camel_case());\n", "fooBar" },
    { "print('[' + ''.to_camel_case() + ']');\n", "[]" },
    /* snake and camel round-trip. */
    { "print('fooBar'.to_snake_case().to_camel_case());\n", "fooBar" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* Method return types propagate, so calls chain and fit typed positions. */
UTEST_F(vm_exec, string_methods_chain)
{
  static const oak_case_t cases[] = {
    { "print('  Hello World  '.trim().lower().replace(' ', '_'));\n",
      "hello_world" },
    { "fn shout(text : string) -> string { return text.upper().repeat(2); }\n"
      "print(shout('hi'));\n",
      "HIHI" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

UTEST_F(vm_exec, character_and_number_parsing_builtins)
{
  static const oak_case_t cases[] = {
    { "print(ord('A'));\n", "65" },
    { "print(chr(97));\n", "a" },
    { "print(chr(ord('z')));\n", "z" },
    { "print(parse_number('42'));\n", "42" },
    { "print(is_int(parse_number('42')));\n", "true" },
    { "print(parse_number('  -17  '));\n", "-17" },
    /* A token containing '.', 'e', or 'E' parses as a float. */
    { "print(is_float(parse_number('3.5')));\n", "true" },
    { "print(to_int(parse_number('1e3')));\n", "1000" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* Bad conversions are errors, not silent zeros. */
UTEST_F(vm_exec, parsing_builtins_reject_bad_input)
{
  /* Each message names the offending input, so the three parse_number rows are
   * distinguishable from one another rather than all reading alike. */
  static const oak_case_t cases[] = {
    { "print(parse_number('nope'));\n", "'nope' is not a number" },
    { "print(parse_number('12x'));\n", "'12x' is not a number" },
    { "print(parse_number('1.2.3'));\n", "'1.2.3' is not a number" },
    { "print(ord(''));\n", "the string is empty" },
    { "print(chr(-1));\n", "-1 is not a byte value" },
    { "print(chr(256));\n", "256 is not a byte value" },
  };

  OAK_EXPECT_RUNTIME_ERROR_CASES(cases);
}

/* String methods are resolved statically, so misuse never reaches the VM. */
UTEST_F(vm_exec, string_method_misuse_is_a_compile_error)
{
  static const oak_case_t cases[] = {
    { "print('x'.nonexistent());\n", "no method 'nonexistent'" },
    { "print('x'.upper('extra'));\n", "expects" },
    { "print('x'.contains());\n", "expects" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}
