/*
 * Compiler: the acyclicity rules that stand in for a cycle collector.
 *
 * Oak reference-counts and has no runtime cycle collector, so the compiler
 * refuses to compile programs that could form a strong reference cycle. Three
 * rules do the work (src/compiler/oak_compiler_cycles.c):
 *
 *   1. A strong field whose type lies on a cycle in the type graph is
 *      write-once -- set in the record literal, never reassigned.
 *   2. Storing into a container is rejected when the element type could own
 *      that container back.
 *   3. A record may not strongly own an interface object, because a later
 *      implementation could close a cycle the compiler cannot see yet.
 *
 * Each rule is paired here with the shape it must NOT reject, since a cycle
 * checker that rejects everything is just as broken as one that rejects
 * nothing.
 */

#include "oak_test_support.h"

OAK_TEST_SUITE(compiler_cycles);

#define RECORD_NODE                                                            \
  "record Node {\n"                                                            \
  "  value : number;\n"                                                        \
  "  next : Node;\n"                                                           \
  "}\n"

#define RECORD_TREE                                                            \
  "record Tree {\n"                                                            \
  "  value : number;\n"                                                        \
  "  children : Tree[];\n"                                                     \
  "}\n"

#define INTERFACE_SHAPE                                                        \
  "interface IShape {\n"                                                       \
  "  fn area(self) -> number;\n"                                               \
  "}\n"

#define RECORD_CIRCLE                                                          \
  "record Circle { radius : number; }\n"                                       \
  "fn Circle.area(self) -> number { return self.radius * self.radius; }\n"

/* ------------------------------------------------------------------ */
/* Rule 1: write-once fields on a type-graph cycle                     */
/* ------------------------------------------------------------------ */

UTEST_F(compiler_cycles, self_referential_strong_fields_are_write_once)
{
  static const oak_case_t cases[] = {
    /* Function bodies are checked even when never called. */
    { RECORD_NODE "fn relink(mut n: Node, other: Node) { n.next = other; }\n",
      "write-once" },
    { "record A { b : B; }\n"
      "record B { a : A; }\n"
      "fn set_b(mut x: A, y: B) { x.b = y; }\n",
      "write-once" },
    { RECORD_TREE
      "let mut leaf = new Tree { value: 1, children: new Tree[] };\n"
      "let mut root = new Tree { value: 2, children: [leaf] };\n"
      "root.children = new Tree[];\n",
      "write-once" },
    /* The rule follows the field, so methods get no exemption. */
    { RECORD_NODE
      "fn Node.relink(mut self, other: Node) { self.next = other; }\n",
      "write-once" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_cycles, fields_off_the_cycle_stay_freely_mutable)
{
  static const oak_case_t cases[] = {
    /* Person -> Address is acyclic, so the field is an ordinary one. */
    { "record Address { city : string; }\n"
      "record Person { home : Address; }\n"
      "let mut p = new Person { home: new Address { city: 'x' } };\n"
      "p.home = new Address { city: 'y' };\n"
      "print(p.home.city);\n",
      null },
    /* A weak link cannot own its target, so it breaks the cycle and the field
     * is mutable again -- this is the escape hatch the rule is built around. */
    { "record Node {\n"
      "  value : number;\n"
      "  next : Node weak;\n"
      "}\n"
      "let mut a = new Node { value: 1, next: none };\n"
      "let mut b = new Node { value: 2, next: none };\n"
      "b.next = a;\n"
      "print(b.value);\n",
      null },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/* ------------------------------------------------------------------ */
/* Rule 2: container stores that could close a cycle                   */
/* ------------------------------------------------------------------ */

UTEST_F(compiler_cycles, stores_into_self_owning_containers_are_rejected)
{
  static const oak_case_t cases[] = {
    { RECORD_TREE
      "let mut root = new Tree { value: 1, children: new Tree[] };\n"
      "root.children.push(root);\n",
      "strong reference cycle" },
    /* The array is only held by a local here, but a Tree built from it could
     * own it, so the push is rejected wherever the array happens to live. */
    { RECORD_TREE
      "let mut arr = new Tree[];\n"
      "let root = new Tree { value: 1, children: arr };\n"
      "arr.push(root);\n",
      "strong reference cycle" },
    { RECORD_TREE
      "let mut a = new Tree { value: 1, children: new Tree[] };\n"
      "let mut root = new Tree { value: 2, children: [a] };\n"
      "root.children[0] = root;\n",
      "strong reference cycle" },
    { "record Group {\n"
      "  members : [string:Group];\n"
      "}\n"
      "let mut g = new Group { members: new [string:Group] };\n"
      "g.members['self'] = g;\n",
      "strong reference cycle" },
    /* The program the old runtime cycle-collector suite used to exercise; it
     * is now rejected at compile time rather than leaked at runtime. */
    { "record Node {\n"
      "  links : Node[];\n"
      "}\n"
      "let mut node = new Node { links : new Node[] };\n"
      "node.links.push(node);\n",
      "strong reference cycle" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_cycles, containers_that_cannot_close_a_cycle_stay_mutable)
{
  static const oak_case_t cases[] = {
    /* Building the structure in the literal is always allowed -- the cycle
     * rule restricts later mutation, not construction. */
    { RECORD_TREE
      "let leaf = new Tree { value: 1, children: new Tree[] };\n"
      "let root = new Tree { value: 2, children: [leaf] };\n"
      "print(root.value);\n",
      null },
    { "record Point { x : number; }\n"
      "let mut pts = new Point[];\n"
      "pts.push(new Point { x: 1 });\n"
      "pts.push(new Point { x: 2 });\n"
      "print(pts.size());\n",
      null },
    /* Tree lies on a cycle through `children`, but no record strongly owns a
     * [string:Tree], so a Tree can never reach this map and storing into it
     * cannot close anything. */
    { RECORD_TREE
      "let mut leaf = new Tree { value: 7, children: new Tree[] };\n"
      "let mut index = new [string:Tree];\n"
      "index['leaf'] = leaf;\n"
      "print(index.size());\n",
      null },
  };

  OAK_EXPECT_OK_CASES(cases);
}

/* ------------------------------------------------------------------ */
/* Rule 3: strong ownership of interface objects                       */
/* ------------------------------------------------------------------ */

/* Any record could later implement the interface, including one that reaches
 * back to the owner, so strong ownership is refused outright. */
UTEST_F(compiler_cycles, records_may_not_strongly_own_interface_objects)
{
  static const oak_case_t cases[] = {
    { INTERFACE_SHAPE RECORD_CIRCLE "record Holder { s : IShape; }\n",
      "interface" },
    { INTERFACE_SHAPE RECORD_CIRCLE "record Holder { shapes : IShape[]; }\n",
      "interface" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

UTEST_F(compiler_cycles, weak_and_root_held_interface_values_are_allowed)
{
  static const oak_case_t cases[] = {
    { INTERFACE_SHAPE RECORD_CIRCLE
      "record Holder { s : IShape weak; }\n"
      "fn hold(s: IShape) -> Holder { return new Holder { s: s }; }\n"
      "let mut c = new Circle { radius: 2 };\n"
      "let h = hold(c);\n"
      "print(1);\n",
      null },
    /* An interface container owned only by a root can never be reached from
     * an element, so it stays mutable. */
    { INTERFACE_SHAPE RECORD_CIRCLE
      "record Rect { w : number; h : number; }\n"
      "fn Rect.area(self) -> number { return self.w * self.h; }\n"
      "let mut shapes = new IShape[];\n"
      "shapes.push(new Circle { radius: 3 });\n"
      "shapes.push(new Rect { w: 2, h: 5 });\n"
      "let mut total = 0;\n"
      "for s in shapes { total += s.area(); }\n"
      "print(total);\n",
      null },
  };

  OAK_EXPECT_OK_CASES(cases);
}
