#include "oak_type.h"

#include "oak_log.h"
#include "oak_str.h"

#include <string.h>

struct oak_builtin_type_t
{
  oak_type_id_t id;
  const char* name;
};

static const struct oak_builtin_type_t builtin_types[] = {
  { OAK_TYPE_NUMBER, "number" },
  { OAK_TYPE_STRING, "string" },
  { OAK_TYPE_BOOL, "bool" },
  { OAK_TYPE_VOID, "void" },
};

#define OAK_BUILTIN_COUNT                                                      \
  ((int)(sizeof(builtin_types) / sizeof(builtin_types[0])))

void oak_type_registry_init(struct oak_type_registry_t* reg)
{
  for (int i = 0; i < OAK_MAX_TYPES; ++i)
  {
    reg->entries[i].name = null;
    reg->entries[i].len = 0;
  }

  /* Slot 0 is reserved for OAK_TYPE_UNKNOWN; the count starts at 1 so that
   * the first interned id is OAK_TYPE_FIRST_USER once the built-ins are
   * placed. */
  reg->count = 1;

  for (int i = 0; i < OAK_BUILTIN_COUNT; ++i)
  {
    const struct oak_builtin_type_t* b = &builtin_types[i];
    oak_assert(b->id == reg->count);
    reg->entries[b->id].name = b->name;
    reg->entries[b->id].len = strlen(b->name);
    reg->count++;
  }
  oak_assert(reg->count == OAK_TYPE_FIRST_USER);
}

oak_type_id_t oak_type_registry_lookup(const struct oak_type_registry_t* reg,
                                       const char* name,
                                       const usize len)
{
  if (!name || len == 0)
    return OAK_TYPE_UNKNOWN;

  for (int i = 1; i < reg->count; ++i)
  {
    const struct oak_type_entry_t* e = &reg->entries[i];
    if (oak_name_eq(e->name, e->len, name, len))
      return (oak_type_id_t)i;
  }
  return OAK_TYPE_UNKNOWN;
}

oak_type_id_t oak_type_registry_intern(struct oak_type_registry_t* reg,
                                       const char* name,
                                       const usize len)
{
  const oak_type_id_t existing = oak_type_registry_lookup(reg, name, len);
  if (existing != OAK_TYPE_UNKNOWN)
    return existing;

  if (reg->count >= OAK_MAX_TYPES)
    return OAK_TYPE_UNKNOWN;

  /* The pointer is borrowed from the source buffer (lexer arena outlives
   * compilation); the registry never frees it. */
  const oak_type_id_t id = (oak_type_id_t)reg->count;
  reg->entries[id].name = name;
  reg->entries[id].len = len;
  reg->count++;
  return id;
}

oak_type_id_t oak_type_registry_intern_with_id(struct oak_type_registry_t* reg,
                                               const char* name,
                                               const usize len,
                                               const oak_type_id_t id)
{
  if (!name || len == 0)
    return OAK_TYPE_UNKNOWN;
  if (id <= OAK_TYPE_UNKNOWN || id >= OAK_MAX_TYPES)
    return OAK_TYPE_UNKNOWN;

  /* If already registered under the same name, return it. */
  const oak_type_id_t existing = oak_type_registry_lookup(reg, name, len);
  if (existing != OAK_TYPE_UNKNOWN)
    return existing == id ? existing : OAK_TYPE_UNKNOWN;

  /* The target slot must be empty. */
  if (reg->entries[id].name != null)
    return OAK_TYPE_UNKNOWN;

  reg->entries[id].name = name;
  reg->entries[id].len = len;

  /* Advance the sequential counter so that subsequent oak_type_registry_intern
   * calls assign ids strictly after all pre-assigned ones. */
  if (reg->count <= id)
    reg->count = id + 1;

  return id;
}

const char* oak_type_registry_name(const struct oak_type_registry_t* reg,
                                   const oak_type_id_t id)
{
  if (id <= OAK_TYPE_UNKNOWN || id >= reg->count)
    return "<unknown>";
  return reg->entries[id].name;
}
