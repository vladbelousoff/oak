#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reference count storage.
 *
 * This header declares the type only, deliberately: the counter is embedded
 * in oak_obj_t and therefore in the layout of every object an embedder can
 * see, so it must not depend on how the library was configured.  `count` is
 * a plain int in every build.  The atomic and non-atomic operations both work
 * on it — see oak_refcount_ops.h, which is internal to the library — and the
 * atomic intrinsics take a `volatile int*` that a plain `int*` converts to
 * implicitly.  Nothing here is affected by OAK_ATOMIC_REFCOUNT.
 *
 * Embedders do not manipulate this directly; use oak_obj_incref /
 * oak_obj_decref.
 */

typedef struct oak_refcount oak_refcount_t;
struct oak_refcount
{
  int count;
};

#ifdef __cplusplus
}
#endif
