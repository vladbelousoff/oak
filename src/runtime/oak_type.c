#include "oak_type.h"

#include "oak_allocator.h"
#include "oak_dynarr.h"
#include "oak_log.h"
#include "oak_str.h"

#include <string.h>

struct oak_builtin_type_t
{
  oak_type_id_t id;
  const char* name;
};

/* void is pre-registered at slot 0 (OAK_TYPE_VOID) before the loop.
 * The remaining builtins start at slot 1 and must appear in sequential order.
 */
static const struct oak_builtin_type_t builtin_types[] = {
  { OAK_TYPE_NUMBER, "number" },
  { OAK_TYPE_STRING, "string" },
  { OAK_TYPE_BOOL, "bool" },
  { OAK_TYPE_FN, "fn" },
};

#define OAK_BUILTIN_COUNT                                                      \
  ((int)(sizeof(builtin_types) / sizeof(builtin_types[0])))

void oak_type_registry_init(struct oak_type_registry_t* reg,
                            struct oak_allocator_t* allocator)
{
  reg->allocator = allocator;
  oak_dynarr_init(&reg->entries, &reg->count, &reg->capacity);

  /* Slot 0 is OAK_TYPE_VOID; pre-register it so name lookup finds "void". */
  struct oak_type_entry_t void_entry = { .name = "void", .len = 4 };
  oak_dynarr_push(allocator,
      &reg->entries, &reg->count, &reg->capacity, &void_entry, sizeof(void_entry));

  for (int i = 0; i < OAK_BUILTIN_COUNT; ++i)
  {
    const struct oak_builtin_type_t* b = &builtin_types[i];
    oak_assert(b->id == reg->count);
    struct oak_type_entry_t entry = {
      .name = b->name,
      .len = (int)strlen(b->name),
    };
    oak_dynarr_push(allocator,
        &reg->entries, &reg->count, &reg->capacity, &entry, sizeof(entry));
  }
  oak_assert(reg->count == OAK_TYPE_FIRST_USER);
  for (int i = reg->count; i < reg->capacity; ++i)
  {
    reg->entries[i].name = null;
    reg->entries[i].len = 0;
  }
}

void oak_type_registry_free(struct oak_type_registry_t* reg)
{
  oak_dynarr_free(reg->allocator, &reg->entries, &reg->count, &reg->capacity);
}

oak_type_id_t oak_type_registry_lookup(const struct oak_type_registry_t* reg,
                                       const char* name,
                                       const int len)
{
  if (!name || len == 0)
    return -1;

  /* Include slot 0 (void) so that the name "void" is resolvable. */
  for (int i = 0; i < reg->count; ++i)
  {
    const struct oak_type_entry_t* e = &reg->entries[i];
    if (e->name && oak_name_eq(e->name, name))
      return (oak_type_id_t)i;
  }
  return -1;
}

oak_type_id_t oak_type_registry_intern(struct oak_type_registry_t* reg,
                                       const char* name,
                                       const int len)
{
  const oak_type_id_t existing = oak_type_registry_lookup(reg, name, len);
  if (existing >= 0)
    return existing;

  /* The pointer is borrowed from the source buffer (lexer arena outlives
   * compilation); the registry never frees it. */
  struct oak_type_entry_t entry = { .name = name, .len = len };
  oak_dynarr_push(reg->allocator, &reg->entries, &reg->count, &reg->capacity, &entry, sizeof(entry));
  const oak_type_id_t id = (oak_type_id_t)(reg->count - 1);
  return id;
}

static void oak_type_registry_ensure_slot(struct oak_type_registry_t* reg,
                                          const oak_type_id_t id)
{
  if (id < reg->capacity)
    return;

  int new_capacity = reg->capacity < 8 ? 8 : reg->capacity;
  while (id >= new_capacity)
    new_capacity *= 2;

  reg->entries = OAK_REALLOC(reg->allocator, reg->entries,
                             (usize)new_capacity * sizeof(*reg->entries));
  for (int i = reg->capacity; i < new_capacity; ++i)
  {
    reg->entries[i].name = null;
    reg->entries[i].len = 0;
  }
  reg->capacity = new_capacity;
}

oak_type_id_t oak_type_registry_intern_with_id(struct oak_type_registry_t* reg,
                                               const char* name,
                                               const int len,
                                               const oak_type_id_t id)
{
  if (!name || len == 0)
    return -1;
  if (id < OAK_TYPE_FIRST_USER)
    return -1;

  /* If already registered under the same name, return it. */
  const oak_type_id_t existing = oak_type_registry_lookup(reg, name, len);
  if (existing >= 0)
    return existing == id ? existing : -1;

  oak_type_registry_ensure_slot(reg, id);
  if (id >= reg->count)
  {
    for (int i = reg->count; i <= id; ++i)
    {
      reg->entries[i].name = null;
      reg->entries[i].len = 0;
    }
  }

  /* The target slot must be empty. */
  if (reg->entries[id].name != null)
    return -1;

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
  if (id < 0 || id >= reg->count)
    return "<unknown>";
  return reg->entries[id].name ? reg->entries[id].name : "<unknown>";
}
