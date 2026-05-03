#include "oak_test_pipeline.h"

OAK_TEST_DECL(RecordFieldShorthand)
{
  /* Basic shorthand: { foo } expands to { foo: foo }. */
  const char* source = "record Vec2 { x: number; y: number; }\n"
                       "let x = 3;\n"
                       "let y = 4;\n"
                       "let v = new Vec2 { x, y };\n"
                       "print(v.x);\n"
                       "print(v.y);";
  OAK_CHECK(expect_ok(source) == OAK_TEST_OK);

  /* Mixed: some fields use shorthand, others explicit. */
  const char* mixed = "record Rect { w: number; h: number; }\n"
                      "let w = 5;\n"
                      "let r = new Rect { w, h: 3 };\n"
                      "print(r.w);\n"
                      "print(r.h);";
  OAK_CHECK(expect_ok(mixed) == OAK_TEST_OK);

  /* Shorthand with mutable binding. */
  const char* mut_source = "record Point { x: number; y: number; }\n"
                           "let x = 10;\n"
                           "let y = 20;\n"
                           "let mut p = new Point { x, y };\n"
                           "p.x = p.x + 1;\n"
                           "print(p.x);";
  OAK_CHECK(expect_ok(mut_source) == OAK_TEST_OK);

  /* Shorthand with undefined variable must fail at compile time. */
  const char* bad = "record Foo { x: number; }\n"
                    "let f = new Foo { x };\n"
                    "print(f.x);";
  OAK_CHECK(expect_compile_error(bad) == OAK_TEST_OK);

  return OAK_TEST_OK;
}

OAK_TEST_MAIN(RecordFieldShorthand)
