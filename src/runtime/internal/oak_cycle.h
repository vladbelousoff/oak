#pragma once

#include "oak_value.h"

/* oak_obj_t::cycle_flags bits, shared between oak_value.c and oak_cycle.c. */
#define OAK_CYCLE_REGISTERED 0x1u /* threaded onto allocator->cycle_objects */
#define OAK_CYCLE_COLLECTING 0x2u /* in the dead set during a collection pass */

/*
 * Internal interface between the reference-counting core (oak_value.c) and the
 * cycle collector (oak_cycle.c).
 *
 * Collector model (trial deletion / synchronous mark-sweep over a candidate
 * set):
 *
 *   - A "cycle-capable" object is one that can contain owning references back
 *     into the heap: arrays, maps, records, and trait objects. Every such
 *     object is threaded onto the allocator's `cycle_objects` list at
 *     construction (see oak_obj_register_cycle_capable) and removed when it is
 *     destroyed. Strings, functions, and native records are leaves and never
 *     participate.
 *
 *   - An "external root" is a reference held from outside the candidate set:
 *     the VM stack, a chunk constant table, a native binding, etc. The
 *     collector computes each candidate's external reference count by starting
 *     from its real refcount and subtracting one for every owning edge that
 *     originates at another candidate. Anything left with external_refs > 0,
 *     and everything reachable from it, is live.
 *
 *   - Weak references are deliberately NOT traversed as edges: they do not keep
 *     their target alive, so including them would wrongly preserve dead cycles.
 *     A weak ref simply expires (its target is freed) once the strong cycle is
 *     reclaimed; the backing oak_obj_t is kept until weak_refcount hits zero.
 *
 *   - A trait object's vtable is an array stored as a chunk constant, so it
 *     always carries the constant table's external reference. After internal-
 *     edge subtraction its external_refs stays >= 1, which is why a vtable is
 *     always marked reachable and never collected even though it is itself a
 *     cycle-capable array.
 *
 *   - Automatic collection may run from oak_obj_decref once a retained decref
 *     threshold is crossed, and at allocator shutdown. It never runs while
 *     already collecting (guarded by allocator->collecting_cycles).
 */

/* True for object types that can own references and thus form cycles. */
int oak_obj_is_cycle_capable(const struct oak_obj_t* obj);

/* Unlink an object from the allocator's cycle-candidate list. Idempotent. */
void oak_obj_unregister_cycle_capable(struct oak_obj_t* obj);

/* Release the object's owned references and free any owned buffers, without
 * freeing the oak_obj_t header itself. Called both by the final decref and by
 * the collector's destroy pass. */
void oak_obj_destroy_payload(struct oak_obj_t* obj);
