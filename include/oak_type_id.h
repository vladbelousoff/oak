#pragma once

/* Stable, monotonically increasing integer identifier for a type. Built-in
 * ids are reserved (see below) so they never need to be looked up at runtime;
 * user-defined names are interned lazily into the per-compilation registry.
 *
 * Id 0 is reserved for "unknown" so that a default-initialized oak_type_t
 * represents an unknown type. Native types registered via oak_bind_type() are
 * pre-assigned ids starting at OAK_TYPE_FIRST_USER. */
typedef int oak_type_id_t;

#define OAK_TYPE_UNKNOWN    ((oak_type_id_t)0)
#define OAK_TYPE_NUMBER     ((oak_type_id_t)1)
#define OAK_TYPE_STRING     ((oak_type_id_t)2)
#define OAK_TYPE_BOOL       ((oak_type_id_t)3)
#define OAK_TYPE_VOID       ((oak_type_id_t)4)
#define OAK_TYPE_FIRST_USER ((oak_type_id_t)5)
