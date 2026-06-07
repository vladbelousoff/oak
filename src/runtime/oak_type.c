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
  reg->owner_module_id = OAK_TYPE_ID_MODULE_NONE;
  reg->next_local_slot = OAK_TYPE_FIRST_USER;
  oak_assert(oak_dynarr_init(allocator, &reg->entries, sizeof *reg->entries));

  /* Slot 0 is OAK_TYPE_VOID; pre-register it so name lookup finds "void". */
  struct oak_type_entry_t void_entry = {
    .name = "void", .len = 4, .id = OAK_TYPE_VOID
  };
  oak_assert(oak_dynarr_push(&reg->entries, &void_entry));

  for (int i = 0; i < OAK_BUILTIN_COUNT; ++i)
  {
    const struct oak_builtin_type_t* b = &builtin_types[i];
    oak_assert(b->id == oak_dynarr_count(reg->entries));
    struct oak_type_entry_t entry = {
      .name = b->name,
      .len = (int)strlen(b->name),
      .id = b->id,
    };
    oak_assert(oak_dynarr_push(&reg->entries, &entry));
  }
  oak_assert(oak_dynarr_count(reg->entries) == OAK_TYPE_FIRST_USER);
}

void oak_type_registry_set_owner(struct oak_type_registry_t* reg,
                                 u16 module_id)
{
  reg->owner_module_id = module_id;
}

void oak_type_registry_free(struct oak_type_registry_t* reg)
{
  oak_dynarr_free(&reg->entries);
}

oak_type_id_t oak_type_registry_lookup(const struct oak_type_registry_t* reg,
                                       const char* name,
                                       const int len)
{
  if (!name || len == 0)
    return -1;

  /* Include slot 0 (void) so that the name "void" is resolvable. */
  for (int i = 0; i < oak_dynarr_count(reg->entries); ++i)
  {
    const struct oak_type_entry_t* e = &reg->entries[i];
    if (e->name && oak_name_eq(e->name, name))
      return e->id;
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
  const u16 slot = reg->next_local_slot++;
  const oak_type_id_t id =
      reg->owner_module_id == OAK_TYPE_ID_MODULE_NONE
          ? (oak_type_id_t)slot
          : oak_type_id_make(reg->owner_module_id, slot);
  struct oak_type_entry_t entry = { .name = name, .len = len, .id = id };
  oak_assert(oak_dynarr_push(&reg->entries, &entry));
  return id;
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

  /* If this exact name/ID pair is already cataloged, return it. Different
   * modules may legitimately contribute distinct IDs with the same name. */
  const oak_type_id_t existing = oak_type_registry_lookup(reg, name, len);
  if (existing == id)
    return existing;

  for (int i = 0; i < oak_dynarr_count(reg->entries); ++i)
    if (reg->entries[i].id == id)
      return -1;

  struct oak_type_entry_t entry = { .name = name, .len = len, .id = id };
  oak_assert(oak_dynarr_push(&reg->entries, &entry));
  const u16 slot = oak_type_id_local_slot(id);
  if (oak_type_id_module(id) == reg->owner_module_id &&
      slot >= reg->next_local_slot)
    reg->next_local_slot = (u16)(slot + 1u);

  return id;
}

const char* oak_type_registry_name(const struct oak_type_registry_t* reg,
                                   const oak_type_id_t id)
{
  for (int i = 0; i < oak_dynarr_count(reg->entries); ++i)
    if (reg->entries[i].id == id)
      return reg->entries[i].name ? reg->entries[i].name : "<unknown>";
  return "<unknown>";
}
