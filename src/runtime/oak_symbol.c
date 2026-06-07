#include "oak_symbol.h"

#include "oak_dynarr.h"
#include "oak_log.h"

#include <string.h>

void oak_symbol_registry_init(struct oak_symbol_registry_t* registry,
                              struct oak_allocator_t* allocator)
{
  registry->allocator = allocator;
  oak_htable_init(&registry->by_name, allocator);
  oak_assert(oak_dynarr_init(
      allocator, &registry->symbols, sizeof *registry->symbols));
}

void oak_symbol_registry_free(struct oak_symbol_registry_t* registry)
{
  oak_htable_free(&registry->by_name);
  oak_dynarr_free(&registry->symbols);
}

struct oak_symbol_t* oak_symbol_registry_insert(
    struct oak_symbol_registry_t* registry, const struct oak_symbol_t* symbol)
{
  if (!symbol || !symbol->name || !symbol->name[0])
    return null;
  if (oak_symbol_registry_find(registry, symbol->name))
    return null;
  oak_assert(oak_dynarr_push(&registry->symbols, symbol));
  const int index = oak_dynarr_count(registry->symbols) - 1;
  oak_htable_insert(&registry->by_name, symbol->name, strlen(symbol->name), index);
  return &registry->symbols[index];
}

const struct oak_symbol_t* oak_symbol_registry_find(
    const struct oak_symbol_registry_t* registry, const char* name)
{
  const int index = oak_htable_get(&registry->by_name, name, strlen(name));
  if (index < 0)
    return null;
  return &registry->symbols[index];
}
