#pragma once

/* Stable, monotonically increasing integer identifier for a type.
 *
 * 0 is OAK_TYPE_VOID and is the zero / absent type: a zero-initialised
 * oak_type_t represents void (no type), and oak_type_clear() sets a slot back
 * to void.  Native types registered via oak_bind_type() are pre-assigned ids
 * starting at OAK_TYPE_FIRST_USER. */
typedef int oak_type_id_t;

#define OAK_TYPE_VOID       ((oak_type_id_t)0)
#define OAK_TYPE_NUMBER     ((oak_type_id_t)1)
#define OAK_TYPE_STRING     ((oak_type_id_t)2)
#define OAK_TYPE_BOOL       ((oak_type_id_t)3)
#define OAK_TYPE_FIRST_USER ((oak_type_id_t)4)
