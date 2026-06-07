#pragma once

#include "oak_types.h"

/* Module-qualified integer identifier for a type.
 *
 * Builtins occupy the small fixed IDs below. User types declared in a source
 * module encode the owning module and its local type slot. Standalone
 * compilation uses small local IDs. */
typedef int oak_type_id_t;

#define OAK_TYPE_VOID       ((oak_type_id_t)0)
#define OAK_TYPE_NUMBER     ((oak_type_id_t)1)
#define OAK_TYPE_STRING     ((oak_type_id_t)2)
#define OAK_TYPE_BOOL       ((oak_type_id_t)3)
#define OAK_TYPE_FN         ((oak_type_id_t)4)
#define OAK_TYPE_NONE       ((oak_type_id_t)-1)
#define OAK_TYPE_FIRST_USER ((oak_type_id_t)5)

#define OAK_TYPE_ID_MODULE_NONE ((u16)0xFFFF)
#define OAK_TYPE_ID_SLOT_MASK   ((oak_type_id_t)0xFFFF)

static inline oak_type_id_t oak_type_id_make(u16 module_id, u16 local_slot)
{
  return (oak_type_id_t)(((u32)module_id + 1u) << 16u) | local_slot;
}

static inline int oak_type_id_is_qualified(oak_type_id_t id)
{
  return id > OAK_TYPE_ID_SLOT_MASK;
}

static inline u16 oak_type_id_module(oak_type_id_t id)
{
  return oak_type_id_is_qualified(id)
             ? (u16)(((u32)id >> 16u) - 1u)
             : OAK_TYPE_ID_MODULE_NONE;
}

static inline u16 oak_type_id_local_slot(oak_type_id_t id)
{
  return (u16)((u32)id & (u32)OAK_TYPE_ID_SLOT_MASK);
}
