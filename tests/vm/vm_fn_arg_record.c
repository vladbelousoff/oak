#include "oak_test_pipeline.h"

OAK_TEST_DECL(FnArgRecord)
{
  /* Pass a record to a function that reads its fields. */
  const char* source = "record Vec2 { x: number; y: number; }\n"
                       "fn manhattan(v: Vec2) -> number { return v.x + v.y; }\n"
                       "let p = new Vec2 { x: 3, y: 4 };\n"
                       "print(manhattan(p));";
  OAK_CHECK(expect_ok(source) == OAK_TEST_OK);

  /* Multiple record args. */
  const char* source_two =
      "record Rect { w: number; h: number; }\n"
      "fn area(r: Rect) -> number { return r.w * r.h; }\n"
      "fn perimeter(r: Rect) -> number { return r.w + r.w + r.h + r.h; }\n"
      "let r = new Rect { w: 5, h: 3 };\n"
      "print(area(r));\n"
      "print(perimeter(r));";
  OAK_CHECK(expect_ok(source_two) == OAK_TEST_OK);

  /* Passing the wrong record type must fail at compile time. */
  const char* bad = "record A { x: number; }\n"
                    "record B { y: number; }\n"
                    "fn take_a(v: A) -> number { return v.x; }\n"
                    "let b = new B { y: 1 };\n"
                    "print(take_a(b));";
  OAK_CHECK(expect_compile_error(bad) == OAK_TEST_OK);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(FnArgRecord)
