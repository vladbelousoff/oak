# C Object and Container Interface Design

## Overview

This design provides a lightweight object model for C with:

- a minimal root `object` abstraction,
- generic `container` semantics,
- specialized interfaces such as `sequence`, `map`, and `set`,
- capability-style interfaces such as `iterable`, `hashable`, and `comparable`,
- hidden vtables and implementation details,
- runtime type information,
- concrete implementations such as vectors, linked lists, hash maps, and tree maps.

The central design rule is:

> **Name interfaces after what callers can do, and implementations after how they do it.**

For example, `sequence` is an interface while `vector` and `linked_list` are implementations; `map` is an interface while `hash_map` and `tree_map` are implementations.

---

## 1. High-Level Model

```text
object
│
├── container
│   ├── sequence
│   │   ├── vector
│   │   ├── deque
│   │   └── linked_list
│   │
│   ├── map
│   │   ├── hash_map
│   │   └── tree_map
│   │
│   └── set
│       ├── hash_set
│       └── tree_set
│
├── string
├── stream
├── file
└── other object types
```

This hierarchy describes broad semantic relationships, but not every useful relationship should be modeled as inheritance. For cross-cutting behavior, use capability interfaces:

```text
vector   → container, sequence, iterable, random_access
hash_map → container, map, iterable
tree_map → container, map, ordered_map, iterable
string   → iterable, comparable, hashable
```

---

## 2. Root Object

The root `object` should remain very small.

### Public declaration

```c
typedef struct object object;
typedef struct type_info type_info;

void object_destroy(object *obj);

const type_info *object_type(
    const object *obj
);

bool object_is(
    const object *obj,
    const type_info *type
);

void *object_query_interface(
    object *obj,
    unsigned interface_id
);
```

The public header keeps `object` opaque.

### Private representation

```c
typedef struct object_vtable {
    void (*destroy)(object *obj);

    const type_info *(*type)(
        const object *obj
    );

    void *(*query_interface)(
        object *obj,
        unsigned interface_id
    );
} object_vtable;

struct object {
    const object_vtable *vt;
};
```

The application never sees the vtable layout.

---

## 3. Runtime Type Information

A small `type_info` structure is enough for lightweight runtime type checks.

```c
struct type_info {
    const char *name;
    const struct type_info *parent;
};
```

Example declarations:

```c
static const type_info object_type_info = {
    .name = "object",
    .parent = NULL
};

static const type_info container_type_info = {
    .name = "container",
    .parent = &object_type_info
};

static const type_info vector_type_info = {
    .name = "vector",
    .parent = &container_type_info
};
```

A simple type check can walk the parent chain:

```c
bool object_is(
    const object *obj,
    const type_info *wanted
)
{
    const type_info *type = object_type(obj);

    while (type) {
        if (type == wanted)
            return true;

        type = type->parent;
    }

    return false;
}
```

---

## 4. Container

`container` is the common abstraction for objects that own or manage collections of elements. It should contain only operations that genuinely apply to all containers.

```c
typedef struct container container;

size_t container_size(
    const container *c
);

void container_clear(
    container *c
);
```

Destruction can remain inherited from `object`, or be wrapped for convenience.

A private container vtable can extend the root object vtable:

```c
typedef struct container_vtable {
    object_vtable object;

    size_t (*size)(
        const container *c
    );

    void (*clear)(
        container *c
    );
} container_vtable;
```

---

## 5. Sequence

A `sequence` represents an ordered collection with positional operations.

Examples include vectors, dynamic arrays, deques, and linked lists.

```c
typedef struct sequence sequence;

size_t sequence_size(
    const sequence *seq
);

void *sequence_get(
    sequence *seq,
    size_t index
);

const void *sequence_cget(
    const sequence *seq,
    size_t index
);

int sequence_insert(
    sequence *seq,
    size_t index,
    const void *value
);

int sequence_erase(
    sequence *seq,
    size_t index
);

int sequence_push_back(
    sequence *seq,
    const void *value
);
```

A linked list may implement `sequence_get()`, but callers should not assume indexed access is efficient. If performance guarantees matter, introduce a separate `random_access` capability.

---

## 6. Map

A `map` represents key-to-value lookup.

```c
typedef struct map map;

void *map_get(
    map *m,
    const void *key
);

const void *map_cget(
    const map *m,
    const void *key
);

int map_put(
    map *m,
    const void *key,
    const void *value
);

int map_remove(
    map *m,
    const void *key
);

bool map_contains(
    const map *m,
    const void *key
);
```

Concrete implementations include `hash_map` and `tree_map`. The caller normally depends on `map`, not on hash-table-specific internals.

---

## 7. Set

A `set` represents a collection of unique values.

```c
typedef struct set set;

int set_add(
    set *s,
    const void *value
);

int set_remove(
    set *s,
    const void *value
);

bool set_contains(
    const set *s,
    const void *value
);
```

Concrete implementations include `hash_set` and `tree_set`.

---

## 8. Ordered Map and Ordered Set

Tree-based containers often provide ordering semantics that hash-based containers do not. Model that as an additional semantic capability instead of exposing tree internals.

```text
map
└── ordered_map
```

Possible operations:

```c
map_iter ordered_map_lower_bound(
    ordered_map *m,
    const void *key
);

map_iter ordered_map_upper_bound(
    ordered_map *m,
    const void *key
);

map_iter ordered_map_min(
    ordered_map *m
);

map_iter ordered_map_max(
    ordered_map *m
);
```

Then:

```text
hash_map → map
tree_map → map + ordered_map
```

Likewise:

```text
hash_set → set
tree_set → set + ordered_set
```

---

## 9. Structural Trees

A tree should be a separate interface only when callers need to inspect or manipulate the tree structure itself.

```c
typedef struct tree tree;
typedef struct tree_node tree_node;

tree_node *tree_root(tree *t);
tree_node *tree_parent(tree_node *node);
tree_node *tree_child(tree_node *node, size_t index);
size_t tree_child_count(const tree_node *node);
void *tree_node_value(tree_node *node);
```

This is different from a `tree_map`. A red-black tree used internally to implement a map usually should expose `map` or `ordered_map`, not a structural `tree` interface.

---

## 10. Capability Interfaces

Not all useful relationships fit naturally into one inheritance hierarchy. Useful capability interfaces may include:

```text
iterable
random_access
hashable
comparable
serializable
cloneable
ordered_map
ordered_set
```

An interface identifier can be used to query whether an object supports a capability.

```c
typedef enum interface_id {
    IID_CONTAINER,
    IID_SEQUENCE,
    IID_MAP,
    IID_SET,

    IID_ITERABLE,
    IID_RANDOM_ACCESS,
    IID_HASHABLE,
    IID_COMPARABLE,
    IID_SERIALIZABLE,

    IID_ORDERED_MAP,
    IID_ORDERED_SET
} interface_id;
```

Then:

```c
void *object_query_interface(
    object *obj,
    interface_id iid
);
```

Example:

```c
object *obj = ...;

iterable *iter =
    object_query_interface(
        obj,
        IID_ITERABLE
    );

if (iter) {
    /* object supports iteration */
}
```

---

## 11. Iterable Interface

Iteration is a good example of a capability shared by otherwise unrelated types.

```c
typedef union iterator_state {
    void *ptr;
    size_t index;
    uintptr_t bits[2];
} iterator_state;

typedef struct iterator {
    void *owner;
    iterator_state state;
} iterator;
```

An iterable vtable might provide:

```c
typedef struct iterable_vtable {
    iterator (*begin)(
        void *self
    );

    void *(*get)(
        iterator *it
    );

    bool (*next)(
        iterator *it
    );
} iterable_vtable;
```

A vector iterator can store an index, while a linked-list iterator can store a node pointer. The caller still uses the same iteration API.

---

## 12. Hidden Vtables

Public headers should expose opaque types only:

```c
typedef struct sequence sequence;
```

Do not expose the struct or vtable layout publicly. Put those definitions in private/internal headers instead.

The public API performs dispatch:

```c
void *sequence_get(
    sequence *seq,
    size_t index
)
{
    return seq->vt->get(
        seq,
        index
    );
}
```

This gives C code manual dynamic dispatch similar to C++ virtual functions.

---

## 13. Common Header as First Struct Member

Concrete implementations can embed the base/interface header as the first member.

```c
typedef struct vector {
    sequence base;

    unsigned char *data;
    size_t size;
    size_t capacity;
    size_t elem_size;
} vector;
```

Conceptually:

```text
vector
┌─────────────────────┐
│ sequence base       │
│   object/vtable     │
├─────────────────────┤
│ vector state        │
│   data              │
│   size              │
│   capacity          │
└─────────────────────┘
```

Then the base pointer can be returned directly:

```c
return &v->base;
```

Inside vector-specific functions:

```c
vector *v = (vector *)seq;
```

This is cleaner and safer than manually allocating a metadata block immediately before an arbitrary returned pointer.

---

## 14. Recommended Constructors

Return the most specific public interface the caller normally needs:

```c
sequence *vector_create(size_t elem_size);
sequence *linked_list_create(size_t elem_size);

map *hash_map_create(...);
map *tree_map_create(...);

set *hash_set_create(...);
set *tree_set_create(...);
```

If ordered operations are central to the API, a tree-backed map may instead return:

```c
ordered_map *tree_map_create(...);
```

Avoid making every constructor return only `object *`, because the more specific interface provides useful compile-time information.

---

## 15. Recommended Naming

### General abstractions

```text
object
container
sequence
map
set
iterable
ordered_map
ordered_set
random_access
```

### Concrete implementations

```text
vector
linked_list
deque
hash_map
tree_map
hash_set
tree_set
rb_tree_map
avl_tree_map
```

Prefer interface names for generic operations:

```c
map_get(...)
sequence_get(...)
```

rather than implementation-specific names such as:

```c
hash_table_get(...)
vector_get(...)
```

when generic behavior is intended.

---

## 16. Recommended API Layering

```text
object
│
├── runtime type information
├── lifetime
└── interface query

container
│
├── size
└── clear

sequence
│
├── positional get
├── insert
└── erase

map
│
├── key lookup
├── put
└── remove

set
│
├── add
├── contains
└── remove

capabilities
│
├── iterable
├── random_access
├── comparable
├── hashable
├── serializable
├── ordered_map
└── ordered_set
```

Concrete objects implement whichever interfaces make sense.

---

## 17. Design Principles

### Keep `object` small

Good root responsibilities:

```text
destroy
type information
query_interface
```

Only add `clone`, `equal`, or `hash` if their semantics are consistent across the library.

### Keep `container` small

Good universal container operations include `size` and `clear`. Do not put sequence-specific indexed access into `container`.

### Model capabilities separately

Do not force every behavior into inheritance. A `string` may be iterable, comparable, and hashable without necessarily being treated as a container.

### Return the most useful interface

Prefer:

```c
sequence *vector_create(...);
map *hash_map_create(...);
set *hash_set_create(...);
```

Upcast to `object *` only when fully generic handling is needed.

### Hide implementation details

Public headers should contain opaque declarations only. Private headers contain struct layouts, vtable layouts, type metadata, and implementation helpers.

### Iterator invalidation

Unless an implementation explicitly guarantees stronger behavior, use this common rule:

> `insert`, `erase`, `clear`, and other structural mutations invalidate existing iterators.

---

## 18. Final Recommended Architecture

```text
                         object
                           │
                ┌──────────┴──────────┐
                │                     │
            container             other objects
                │                 string, file,
       ┌────────┼────────┐         stream, ...
       │        │        │
   sequence    map      set
       │        │        │
   ┌───┴───┐  ┌─┴──┐   ┌─┴──┐
 vector  list hash tree hash tree


Independent capabilities:

 iterable
 random_access
 comparable
 hashable
 serializable
 ordered_map
 ordered_set
```

Examples:

```text
vector
  concrete type: vector
  semantic interface: sequence
  base interface: container
  root: object
  capabilities: iterable, random_access
```

```text
hash_map
  concrete type: hash_map
  semantic interface: map
  base interface: container
  root: object
  capabilities: iterable
```

```text
tree_map
  concrete type: tree_map
  semantic interface: map
  base interface: container
  root: object
  capabilities: iterable, ordered_map
```

This preserves a simple inheritance spine while using interface queries for capabilities that do not naturally belong in a single tree.

---

## Core Rule

> **Use `object` for lifetime and identity, `container` for common collection behavior, semantic interfaces such as `sequence`, `map`, and `set` for how callers interact with data, capability interfaces for cross-cutting behavior, and concrete names such as `vector`, `hash_map`, and `tree_map` for implementation strategies.**

This provides a flexible object model while keeping the C API explicit, opaque, and reasonably type-safe.
