#include "oak_type.h"

#include "oak_allocator.h"
#include "oak_log.h"
#include "oak_str.h"

#include <string.h>

typedef struct oak_builtin_type oak_builtin_type_t;
struct oak_builtin_type
{
  oak_type_id_t id;
  const char* name;
};

/* void is pre-registered at slot 0 (OAK_TYPE_VOID) before the loop.
 * The remaining builtins start at slot 1 and must appear in sequential order.
 */
static const oak_builtin_type_t builtin_types[] = {
  { OAK_TYPE_NUMBER, "number" },
  { OAK_TYPE_STRING, "string" },
  { OAK_TYPE_BOOL, "bool" },
  { OAK_TYPE_FN, "fn" },
};

#define OAK_BUILTIN_COUNT                                                      \
  ((int)(sizeof(builtin_types) / sizeof(builtin_types[0])))

void oak_type_registry_init(oak_type_registry_t* reg,
                            oak_allocator_t* allocator)
{
  reg->allocator = allocator;
  reg->owner_module_id = OAK_TYPE_ID_MODULE_NONE;
  reg->next_local_slot = OAK_TYPE_FIRST_USER;
  reg->entries = oak_vector_new(allocator, sizeof(oak_type_entry_t));
  oak_assert(reg->entries);

  /* Slot 0 is OAK_TYPE_VOID; pre-register it so name lookup finds "void". */
  oak_type_entry_t void_entry = {
    .name = "void", .id = OAK_TYPE_VOID
  };
  oak_assert(oak_push_back(reg->entries, &void_entry));

  for (int i = 0; i < OAK_BUILTIN_COUNT; ++i)
  {
    const oak_builtin_type_t* b = &builtin_types[i];
    oak_assert(b->id == (oak_type_id_t)oak_size(reg->entries));
    oak_type_entry_t entry = {
      .name = b->name,
      .id = b->id,
    };
    oak_assert(oak_push_back(reg->entries, &entry));
  }
  oak_assert(oak_size(reg->entries) == OAK_TYPE_FIRST_USER);
}

void oak_type_registry_set_owner(oak_type_registry_t* reg,
                                 u16 module_id)
{
  reg->owner_module_id = module_id;
}

void oak_type_registry_free(oak_type_registry_t* reg)
{
  oak_destroy(reg->entries);
}

oak_type_id_t oak_type_registry_lookup(const oak_type_registry_t* reg,
                                       const char* name)
{
  if (!name || !name[0])
    return -1;

  /* Include slot 0 (void) so that the name "void" is resolvable. */
  const oak_type_entry_t* entries =
      OAK_CDATA(oak_type_entry_t, reg->entries);
  for (usize i = 0; i < oak_size(reg->entries); ++i)
  {
    const oak_type_entry_t* e = &entries[i];
    if (e->name && oak_name_eq(e->name, name))
      return e->id;
  }
  return -1;
}

oak_type_id_t oak_type_registry_intern(oak_type_registry_t* reg,
                                       const char* name)
{
  const oak_type_id_t existing = oak_type_registry_lookup(reg, name);
  if (existing >= 0)
    return existing;

  /* The pointer is borrowed from the source buffer (lexer arena outlives
   * compilation); the registry never frees it. */
  const u16 slot = reg->next_local_slot++;
  const oak_type_id_t id =
      reg->owner_module_id == OAK_TYPE_ID_MODULE_NONE
          ? (oak_type_id_t)slot
          : oak_type_id_make(reg->owner_module_id, slot);
  oak_type_entry_t entry = { .name = name, .id = id };
  oak_assert(oak_push_back(reg->entries, &entry));
  return id;
}

oak_type_id_t oak_type_registry_intern_with_id(oak_type_registry_t* reg,
                                               const char* name,
                                               const oak_type_id_t id)
{
  if (!name || !name[0])
    return -1;
  if (id < OAK_TYPE_FIRST_USER)
    return -1;

  /* If this exact name/ID pair is already cataloged, return it. Different
   * modules may legitimately contribute distinct IDs with the same name. */
  const oak_type_id_t existing = oak_type_registry_lookup(reg, name);
  if (existing == id)
    return existing;

  const oak_type_entry_t* entries =
      OAK_CDATA(oak_type_entry_t, reg->entries);
  for (usize i = 0; i < oak_size(reg->entries); ++i)
    if (entries[i].id == id)
      return -1;

  oak_type_entry_t entry = { .name = name, .id = id };
  oak_assert(oak_push_back(reg->entries, &entry));
  const u16 slot = oak_type_id_local_slot(id);
  if (oak_type_id_module(id) == reg->owner_module_id &&
      slot >= reg->next_local_slot)
    reg->next_local_slot = (u16)(slot + 1u);

  return id;
}

const char* oak_type_registry_name(const oak_type_registry_t* reg,
                                   const oak_type_id_t id)
{
  const oak_type_entry_t* entries =
      OAK_CDATA(oak_type_entry_t, reg->entries);
  for (usize i = 0; i < oak_size(reg->entries); ++i)
    if (entries[i].id == id)
      return entries[i].name ? entries[i].name : "<unknown>";
  return "<unknown>";
}
