#include "oak_count_of.h"
#include "oak_test_pipeline.h"

/* Compile-time acyclicity rules (no runtime cycle collector):
 *  - strong fields on a type-graph cycle are write-once (record literal only)
 *  - stores into containers whose element type can own the container back are
 *    rejected
 *  - records may not strongly own trait objects
 * See src/compiler/oak_compiler_cycles.c. */

/* =========================================================================
 * Write-once recursive fields
 * ========================================================================= */

#define RECORD_NODE \
  "record Node {\n" \
  "  value : number;\n" \
  "  next : Node;\n" \
  "}\n"

OAK_TEST_DECL(RecursiveFieldReassignRejected)
{
  /* Bodies compile even when never called; the assignment alone must error. */
  return expect_compile_error(
      RECORD_NODE
      "fn relink(mut n: Node, other: Node) { n.next = other; }\n");
}

OAK_TEST_DECL(MutuallyRecursiveFieldReassignRejected)
{
  return expect_compile_error("record A { b : B; }\n"
                              "record B { a : A; }\n"
                              "fn set_b(mut x: A, y: B) { x.b = y; }\n");
}

OAK_TEST_DECL(RecursiveContainerFieldReassignRejected)
{
  return expect_compile_error(
      "record Tree {\n"
      "  value : number;\n"
      "  children : Tree[];\n"
      "}\n"
      "let leaf = new Tree { value: 1, children: new Tree[] };\n"
      "let mut root = new Tree { value: 2, children: [leaf] };\n"
      "root.children = new Tree[];\n");
}

OAK_TEST_DECL(AcyclicRecordFieldStaysMutable)
{
  return expect_ok("record Address { city : string; }\n"
                   "record Person { home : Address; }\n"
                   "let mut p = new Person {\n"
                   "  home: new Address { city: 'x' }\n"
                   "};\n"
                   "p.home = new Address { city: 'y' };\n"
                   "print(p.home.city);\n");
}

OAK_TEST_DECL(WeakRecursiveFieldStaysMutable)
{
  return expect_ok("record Node {\n"
                   "  value : number;\n"
                   "  next : Node weak;\n"
                   "}\n"
                   "let mut a = new Node { value: 1, next: none };\n"
                   "let mut b = new Node { value: 2, next: none };\n"
                   "b.next = a;\n"
                   "print(b.value);\n");
}

OAK_TEST_DECL(WriteOnceAppliesInsideMethods)
{
  return expect_compile_error(
      RECORD_NODE
      "fn Node.relink(mut self, other: Node) { self.next = other; }\n");
}

/* =========================================================================
 * Container stores whose element type is on a strong cycle
 * ========================================================================= */

#define RECORD_TREE \
  "record Tree {\n" \
  "  value : number;\n" \
  "  children : Tree[];\n" \
  "}\n"

OAK_TEST_DECL(CyclicElementArrayLiteralConstructionOk)
{
  return expect_ok(RECORD_TREE
                   "let leaf = new Tree { value: 1, children: new Tree[] };\n"
                   "let root = new Tree { value: 2, children: [leaf] };\n"
                   "print(root.value);\n");
}

OAK_TEST_DECL(CyclicElementArrayPushRejected)
{
  return expect_compile_error(
      RECORD_TREE
      "let mut root = new Tree { value: 1, children: new Tree[] };\n"
      "root.children.push(root);\n");
}

OAK_TEST_DECL(CyclicElementLocalArrayPushRejected)
{
  /* The array is only held by a local, but a Tree constructed from it could
   * own it, so pushes are rejected wherever the array lives. */
  return expect_compile_error(RECORD_TREE
                              "let mut arr = new Tree[];\n"
                              "let root = new Tree { value: 1, children: arr };\n"
                              "arr.push(root);\n");
}

OAK_TEST_DECL(CyclicElementIndexAssignRejected)
{
  return expect_compile_error(
      RECORD_TREE
      "let a = new Tree { value: 1, children: new Tree[] };\n"
      "let mut root = new Tree { value: 2, children: [a] };\n"
      "root.children[0] = root;\n");
}

OAK_TEST_DECL(AcyclicElementArrayPushOk)
{
  return expect_ok("record Point { x : number; }\n"
                   "let mut pts = new Point[];\n"
                   "pts.push(new Point { x: 1 });\n"
                   "pts.push(new Point { x: 2 });\n"
                   "print(pts.size());\n");
}

OAK_TEST_DECL(UnownedContainerOfCyclicRecordStaysMutable)
{
  /* Tree is on a strong cycle through 'children', but no record strongly owns
   * a [string:Tree], so a tree can never reach that map and storing into it
   * cannot close a cycle. */
  return expect_ok(RECORD_TREE
                   "let mut leaf = new Tree { value: 7, children: new Tree[] };\n"
                   "let mut index = new [string:Tree];\n"
                   "index['leaf'] = leaf;\n"
                   "print(index.size());\n");
}

OAK_TEST_DECL(CyclicValueMapStoreRejected)
{
  return expect_compile_error(
      "record Group {\n"
      "  members : [string:Group];\n"
      "}\n"
      "let mut g = new Group { members: new [string:Group] };\n"
      "g.members['self'] = g;\n");
}

/* =========================================================================
 * Records may not strongly own trait objects
 * ========================================================================= */

#define TRAIT_SHAPE \
  "trait Shape {\n" \
  "  fn area(self) -> number;\n" \
  "}\n"

#define RECORD_CIRCLE \
  "record Circle { radius : number; }\n" \
  "fn Circle.area(self) -> number { return self.radius * self.radius; }\n"

OAK_TEST_DECL(StrongTraitFieldRejected)
{
  return expect_compile_error(TRAIT_SHAPE
                              RECORD_CIRCLE
                              "record Holder { s : Shape; }\n");
}

OAK_TEST_DECL(StrongTraitArrayFieldRejected)
{
  return expect_compile_error(TRAIT_SHAPE
                              RECORD_CIRCLE
                              "record Holder { shapes : Shape[]; }\n");
}

OAK_TEST_DECL(WeakTraitFieldOk)
{
  return expect_ok(TRAIT_SHAPE
                   RECORD_CIRCLE
                   "record Holder { s : Shape weak; }\n"
                   "fn hold(s: Shape) -> Holder {\n"
                   "  return new Holder { s: s };\n"
                   "}\n"
                   "let mut c = new Circle { radius: 2 };\n"
                   "let h = hold(c);\n"
                   "print(1);\n");
}

OAK_TEST_DECL(RootHeldTraitArrayPushStillOk)
{
  /* Trait containers can only be owned by roots, so they stay mutable. */
  return expect_ok(TRAIT_SHAPE
                   RECORD_CIRCLE
                   "record Rect { w : number; h : number; }\n"
                   "fn Rect.area(self) -> number { return self.w * self.h; }\n"
                   "let mut shapes = new Shape[];\n"
                   "shapes.push(new Circle { radius: 3 });\n"
                   "shapes.push(new Rect { w: 2, h: 5 });\n"
                   "let mut total = 0;\n"
                   "for s in shapes { total += s.area(); }\n"
                   "print(total);\n");
}

/* The program from the old runtime cycle-collection suite: creating the
 * cycle is now a compile error instead of garbage for a collector. */
OAK_TEST_DECL(FormerRuntimeCycleProgramRejected)
{
  return expect_compile_error("record Node {\n"
                              "  links : Node[];\n"
                              "}\n"
                              "let mut node = new Node {\n"
                              "  links : new Node[]\n"
                              "};\n"
                              "node.links.push(node);\n");
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static struct oak_test_t tests[] = {
    OAK_TEST_ENTRY(RecursiveFieldReassignRejected),
    OAK_TEST_ENTRY(MutuallyRecursiveFieldReassignRejected),
    OAK_TEST_ENTRY(RecursiveContainerFieldReassignRejected),
    OAK_TEST_ENTRY(AcyclicRecordFieldStaysMutable),
    OAK_TEST_ENTRY(WeakRecursiveFieldStaysMutable),
    OAK_TEST_ENTRY(WriteOnceAppliesInsideMethods),
    OAK_TEST_ENTRY(CyclicElementArrayLiteralConstructionOk),
    OAK_TEST_ENTRY(CyclicElementArrayPushRejected),
    OAK_TEST_ENTRY(CyclicElementLocalArrayPushRejected),
    OAK_TEST_ENTRY(CyclicElementIndexAssignRejected),
    OAK_TEST_ENTRY(AcyclicElementArrayPushOk),
    OAK_TEST_ENTRY(UnownedContainerOfCyclicRecordStaysMutable),
    OAK_TEST_ENTRY(CyclicValueMapStoreRejected),
    OAK_TEST_ENTRY(StrongTraitFieldRejected),
    OAK_TEST_ENTRY(StrongTraitArrayFieldRejected),
    OAK_TEST_ENTRY(WeakTraitFieldOk),
    OAK_TEST_ENTRY(RootHeldTraitArrayPushStillOk),
    OAK_TEST_ENTRY(FormerRuntimeCycleProgramRejected),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
