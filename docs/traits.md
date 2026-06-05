# Traits in Oak

This note documents how traits are compiled, dispatched, imported, and managed
at runtime. It is the design contract for the trait code that is otherwise
spread across the compiler, VM, and runtime. Keep it in sync when changing any
of the files listed at the end.

## Concepts

A **trait** is a named set of method signatures:

```oak
trait Shape {
  fn area(self) -> number;
  fn describe(self) -> string;
}
```

A concrete **record** conforms to a trait by implementing every method as a
`fn Record.method(self) ...` declaration. A value of a record type is coerced
to a **trait object** when it is used where the trait type is expected (passing
to a `fn f(s: Shape)`, pushing into a `Shape[]`, etc.).

## Method resolution order

Trait methods are numbered by **declaration order** in the trait. That ordinal
is the method's *slot*:

- `oak_trait_method_slot()` returns the slot for a method name.
- For each conforming record, `oak_trait_impl_t.vtable[slot]` holds the chunk
  constant index of the function implementing that slot.
- `oak_compiler_build_vtables()` materializes, per `(record, trait)` impl, an
  `OAK_OBJ_ARRAY` chunk constant whose element `slot` is the function value for
  that slot. Its constant index is stored in `vtable_array_const_idx`.

The slot numbering must be identical in three places: the trait declaration, the
vtable array built for each impl, and the `slot` operand baked into
`OAK_OP_CALL_VIRTUAL`. Changing one without the others silently calls the wrong
method.

## Conformance and default methods

A trait method may have a **default body**:

```oak
trait Greet {
  fn name(self) -> string;
  fn greet(self) -> string { return 'hi ' + self.name(); }
}
```

**Current behavior:** a default body does *not* satisfy conformance. A record
must still explicitly implement *every* method, including ones with a default,
or it is rejected as not implementing the trait. Defaults are not auto-filled
into vtables. This is pinned by
`TraitDefaultMethodDoesNotSatisfyConformance` in `tests/compiler/compiler_traits.c`;
update that test deliberately if the policy changes.

## Runtime layout and dispatch

A trait object is `struct oak_obj_trait_object_t { value; vtable; }`:

- `value` — an owning reference to the wrapped concrete value.
- `vtable` — an owning reference to the impl's vtable array (the chunk
  constant described above).

`OAK_OP_MAKE_TRAIT_OBJECT vtable_idx` pops the concrete value, reads the vtable
array from `chunk->constants[vtable_idx]`, and constructs the trait object
(increffing both `value` and `vtable`).

`OAK_OP_CALL_VIRTUAL slot arity` dispatches a call: the receiver at
`sp - arity` must be a trait object; the target function is
`trait_obj->vtable->items[slot]`; the wrapped `value` is unwrapped back into the
`self` slot before the call. See `src/vm/oak_vm_call.c`.

## Cross-module traits

A trait can be declared in one module and implemented/dispatched in another.
`import_trait_from_dep()` copies the trait's method signatures into the local
trait registry, translating each type id through the importing module's type
registry. For an imported trait method the following must stay in sync with the
source module:

- arity (including `self`),
- `self` mutability (`self_is_mut`),
- return type and parameter types (translated, not raw ids),
- `source_module_id` / `source_const_idx`, used to resolve the implementing
  function's chunk constant across modules at link/run time.

Imported trait methods have `sig_decl == null` (no local AST); local traits keep
their `sig_decl` for re-export. Covered by `CrossModuleTraitDispatch` in
`tests/compiler/compiler_imports.c`.

## Runtime ownership and cycle collection

- A trait object owns one strong reference to its `value` and one to its
  `vtable` array; both are released in `oak_obj_destroy_payload`.
- Trait objects are **cycle-capable**: their `value` edge is traversed by the
  collector, so a trait object caught in a reference cycle is reclaimed.
- The vtable array is a **chunk constant**, i.e. always externally rooted. After
  the collector subtracts internal edges its `external_refs` stays `>= 1`, so a
  vtable is always marked reachable and never collected even though it is itself
  a cycle-capable array. See `src/runtime/internal/oak_cycle.h` and the
  `TraitVtableWithExternalOwnerSurvives` test in
  `tests/vm/vm_cycle_collection.c`.

## Files

- `src/compiler/internal/oak_trait_registry.h` — registry, impl, and vtable
  data structures and lookups.
- `src/compiler/oak_compiler_traits.c` — trait declaration, conformance
  checking, vtable building.
- `src/compiler/oak_compiler_call_method.c`,
  `src/compiler/oak_compiler_type_infer_call.c` — method-call lowering and
  virtual-call type inference.
- `src/compiler/oak_compiler_imports.c` — cross-module trait import.
- `src/vm/oak_vm_call.c`, `src/vm/oak_vm_object.c` — `MAKE_TRAIT_OBJECT` and
  `CALL_VIRTUAL` execution.
- `src/runtime/oak_value.c`, `src/runtime/oak_value.h` — trait object
  representation and destruction.
- `src/runtime/oak_cycle.c` — cycle collection over trait objects.
